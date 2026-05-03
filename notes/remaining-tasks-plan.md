# Remaining Migration Tasks — `gfx::` Abstraction Layer

_Last updated: 2026-05-03_

This document captures where the `gfx::` migration stands after the P14 cleanup sprint,
and what must happen next. Ordered by dependency.

---

## Current State Summary

| Phase | Status | What it covers |
|---|---|---|
| P0–P12 | ✅ Done | gfx skeleton, swap chain, render graph, all subsystems, scene mega-buffers |
| P13 | ✅ Done | PSOs → `gfx::PipelineHandle`; VS/PS → `gfx::ShaderHandle`; `depthBuffer`, cubemap, back buffers → `gfx::TextureHandle`; RTV/DSV heaps fully removed from Application (backend auto-allocates); Application's `device`/`swapChain` ComPtr aliases removed |
| P14 | ✅ Done (final) | All D3D12 types removed from `.ixx` module interfaces that are not blocked on bindless. `ID3D12RootSignature*` params purged from `shadow.ixx`, `outline.ixx`, `object_picking.ixx` — subsystems now call `dev.bindlessRootSigNative()` internally. Application's legacy `rootSignature` ComPtr removed; all render passes use bindless root sig. `bloom.ixx` PSO/rootsig ComPtrs moved to private. `scene.ixx` BLAS/TLAS + RT methods moved to private. All `#ifdef USE_BINDLESS`/`#else` branches removed from `render.cpp`, `shadow.cpp`, `outline.cpp`, `object_picking.cpp`. |
| P2 | ✅ Done | Bindless descriptor heap + root sig rewrite + shader rewrite. `USE_BINDLESS` defaults ON and is verified by `test.json` + gfx/unit tests. Root signature uses 32 DWORD root constants plus CBVs and descriptor tables to stay within the D3D12 root-signature DWORD budget. |

### What still leaks D3D12 in `.ixx` interfaces

| Location | Leak | Blocked on |
|---|---|---|
| `application.ixx` | `ComPtr<ID3D12RootSignature> gridRootSig` | grid-specific root sig (incompatible layout) |
| All subsystems | `ComPtr<ID3D12RootSignature>`/`ComPtr<ID3D12PipelineState>` private fields | remaining gfx PSO/root-signature ownership migration |
| `ssao.ixx` | `D3D12_PLACED_SUBRESOURCE_FOOTPRINT noiseFp`, `ComPtr<ID3D12Resource> noiseUploadBuf` | gfx texture upload API |
| `scene.ixx` | BLAS/TLAS `ComPtr<ID3D12Resource>` (private, capability-gated) | RT backend API |
| `command_queue.ixx` | `D3D12_COMMAND_LIST_TYPE`, all ComPtrs | CommandQueue is a D3D12-specific class |
| `imgui_layer.ixx` | `ID3D12CommandQueue*` param, `ComPtr<ID3D12DescriptorHeap> srvHeap` | imgui_impl_dx12 requirement |

---

## P2 — Bindless Descriptor Heap + Root Signature Rewrite

Status: completed and verified on 2026-05-03. Keep this section as a maintenance note
for the current layout and common failure modes.

### What changes

**Backend side (`src/gfx/`):**
- `BindlessHeap` in `d3d12_command.cpp` already exists (global SRV/UAV heap, 65k slots).
- `IDevice::bindlessSrvIndex(TextureHandle)` already declared in `gfx.h`.
- The **bindless root signature** is built once at device init:
  ```
  [0] 32 root constants  b0  — per-draw indices / small bindless payloads
  [1] CBV                b1  — PerFrameCB
  [2] CBV                b2  — PerPassCB
  [3] SRV table          t0, space0/1/2 + UAV u0, space0 — bindless resource arrays
  [4] Sampler table      s0, space0/1 — bindless sampler arrays
  ```

**Shader side (`src/shaders/`):**
- Replace named resource bindings (`Texture2D shadowMap : register(t1)`) with
  `Texture2D textures[] : register(t0, space0)` + bindless index via root constants.
- `PerPassCB` and `PerFrameCB` HLSL structs stay the same; only binding registers change.
- Largest shader-side change — most likely source of subtle visual regressions.
- Start with `pixel_shader.hlsl` (most complex), then `gbuffer_ps.hlsl`, etc.

**Application side:**
- `rootSignature` and `gridRootSig` ComPtrs disappear — backend owns the root sig.
- All `SetGraphicsRootSignature`, `SetGraphicsRootDescriptorTable`,
  `SetGraphicsRootConstantBufferView`, `SetGraphicsRoot32BitConstant` calls replaced
  with `cmd.setRootConstants(slot, data, count)` and `cmd.bindPipeline(handle)`.
- `sceneSrvHeap` in `Scene` replaced by bindless indices.
- `SetDescriptorHeaps` called once per frame for the bindless heap; disappears from passes.

### Maintenance notes

1. `USE_BINDLESS` defaults ON. Keep old path compilable until the remaining legacy root signatures are removed.
2. Keep root constant payloads at or below 32 DWORDs unless the root signature budget is recalculated.
3. GPU-based validation (GBV) catches out-of-bounds
   bindless index accesses that normal validation misses.

```cpp
// Enable in gfx d3d12 backend init (debug builds only):
ComPtr<ID3D12Debug3> debug3;
if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug3)))) {
    debug3->SetEnableGPUBasedValidation(TRUE);
}
```
GBV is ~10× slower and inflates VRAM — use only when debugging visual corruption.

### High-risk areas

| Risk | Mitigation |
|---|---|
| Wrong bindless index → reads garbage → broken lighting/shadows | Add `assert(idx < maxDescriptors)` in debug shader builds |
| Root sig layout mismatch between C++ and HLSL | Keep `PerFrameCB`/`PerPassCB` HLSL structs unchanged; only registers change |
| Stencil ref and outline params lost | Outline pass uses root constants — port to an explicit slot |
| Grid pass uses its own `gridRootSig` | Port to bindless sig at the same time, or leave on a separate legacy sig temporarily |
| ImGui DX12 backend bypasses gfx | `imgui_impl_dx12` needs raw `ID3D12Device*` + `ID3D12DescriptorHeap*`. Create a `gfx::ImGuiBridge` shim exposing those two raw pointers only to ImGui init code |

---

## P14 Final Cleanup

Run the verification sweep:

```bash
# Should return hits ONLY under src/gfx/
grep -rn "ID3D12\|IDXGI" src/ --include="*.cpp" --include="*.ixx" --include="*.h"

# Subsystem headers should no longer include d3d12.h
grep -rn "#include <d3d12.h>" src/ --include="*.ixx"
```

Expected legitimate survivors: `src/gfx/d3d12_*.cpp` / `d3d12_internal.h` (the backend),
and `imgui_impl_dx12` shim.

Add a `MockDevice` smoke test in `tests/gfx_tests.cpp` that implements every `IDevice`
virtual — confirms the interface is fully overridable (proof a Vulkan backend would slot in).

---

## Debugging Playbook

### TDR diagnosis

`DXGI_ERROR_DEVICE_REMOVED (0x887a0005)` is always reported at the first D3D12 call
**after** the GPU fault. When you see it:

1. Enable debug layer (already gated on `IsDebuggerPresent()` in `window.cpp`).
2. Attach debugger — debug layer breaks at the actual offending draw.
3. Check `GetDeviceRemovedReason()` immediately after any HRESULT failure.
4. Common causes during migration: descriptor heap not bound, root sig mismatch,
   typeless resource used as SRV without a typed view.

### Root sig / PSO mismatch (`0x80070057`)

`HRESULT 0x80070057 "The parameter is incorrect"` from `CreateGraphicsPipelineState`:
- `StencilEnable = TRUE` but `FrontFace.StencilFunc` is 0 (invalid) — always populate both faces.
- Root sig parameter count implied by HLSL register slots doesn't match C++ desc — enable debug layer.
- Format mismatch between DSV in PSO desc and the actual resource format.

### Descriptor heap binding gaps (bindless)

Forgetting `SetDescriptorHeaps` for the bindless heap before the first draw produces a
silent validation error. Symptoms: shadow samples return 0 (white), SSAO returns 1
(no occlusion), cubemap returns black. App doesn't crash.

Set once per frame before any draw:
```cpp
ID3D12DescriptorHeap* heaps[] = { bindlessHeap->heap.Get() };
cmdList->SetDescriptorHeaps(1, heaps);
```

### Stale `.pcm` module files

If build fails with "module file built from a different branch":
```bash
find build -name "*.pcm" -delete
cmake --build build --config Debug
```

### Fence hangs

Fence stalls that hang indefinitely:
- Command list was never submitted (`execCmdList` not called after last work).
- Wrong fence value being waited on (off-by-one from prior frame).
- GPU TDR occurred — fence never advances, wait spins forever.

Add `GetDeviceRemovedReason()` check with a timeout poll.

---

## Recommended Order of Attack

```
1. P14 final — remove remaining public D3D12 root-sig params where possible.
2. API cleanup — add gfx-owned replacements for ImGui bridge, WIC texture adoption, RT acceleration resources, and SSAO upload helpers.
```
