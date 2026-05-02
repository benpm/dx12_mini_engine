# Remaining Migration Tasks — `gfx::` Abstraction Layer

_Last updated: 2026-05-02_

This document covers the unfinished work in the `gfx::` backend-agnostic migration, ordered by dependency.
Each section describes what changes, why, common failure modes, and how to verify.

---

## Current State Summary

| Phase | Status | What it covers |
|---|---|---|
| P0–P12 | ✅ Done | gfx skeleton, swap chain, render graph, all subsystems, scene mega-buffers |
| P3 | ✅ Done | `importTexture` → `gfx::TextureHandle`; `execute()` barriers via `cmd.barrier()` |
| P13 (partial) | 🔄 In progress | PSOs, shaders, depth/cubemap/back buffers are gfx handles; heaps and root sigs still ComPtr |
| P2 | ⏳ Pending | Bindless descriptor heap + root sig rewrite — high-risk, requires shader changes |
| P14 | ⏳ Pending | Final cleanup verification |

---

## P13 Remainder — Application Core

### What still leaks D3D12

These fields in `application.ixx` still use ComPtr or raw D3D12 types:

```
rtvHeap            ComPtr<ID3D12DescriptorHeap>   — back-buffer RTVs
dsvHeap            ComPtr<ID3D12DescriptorHeap>   — main depth DSV
cubemapRtvHeap     ComPtr<ID3D12DescriptorHeap>   — 6 cubemap face RTVs
cubemapDsvHeap     ComPtr<ID3D12DescriptorHeap>   — 6 cubemap face DSVs
rootSignature      ComPtr<ID3D12RootSignature>    — scene + gizmo + ID + cubemap passes
gridRootSig        ComPtr<ID3D12RootSignature>    — grid pass
device             ComPtr<ID3D12Device2>          — legacy alias (P1 refcount alias)
swapChain          ComPtr<IDXGISwapChain4>        — legacy alias (P1 refcount alias)
```

Inside `render.cpp` pass lambdas, every `cmdRef.nativeHandle()` cast and every
`D3D12_CPU_DESCRIPTOR_HANDLE` / `D3D12_GPU_VIRTUAL_ADDRESS` usage is a leak that
persists until the above heaps and root sigs are modelled by gfx.

### Why most of P13 blocks on P2

The main `rootSignature` is deeply embedded in:
- `cmd->SetGraphicsRootSignature(rootSignature.Get())` in every render pass lambda
- All `rootParam` slot constants (`app_slots::rootPerFrameCB` etc.)
- All `SetGraphicsRootDescriptorTable` calls against the fixed 8-param layout

These cannot be abstracted without replacing the fixed descriptor layout with
the bindless model (P2). There is no benefit to wrapping just the ComPtr.

The descriptor heaps (`rtvHeap`, `dsvHeap`, etc.) are also only meaningful alongside
their corresponding root sig layout. Moving them to `gfx::` makes sense once the
bindless heap allocator owns all descriptor slots.

### What CAN be done now (without P2)

1. **Remove legacy ComPtr aliases** — `Application::device` and `Application::swapChain`
   are commented as "P1 refcount aliases". Any remaining callers after P13 partial can
   use `gfxDevice->nativeHandle()` directly. Safe to remove once verified no subsystem
   still requires the field by name (grep: `this->device->` in application files).

2. **Replace `createDescHeap` helper** — `createDescHeap()` in `setup.cpp` returns a
   `ComPtr<ID3D12DescriptorHeap>`. This is trivially replaceable with inline calls since
   it's only called during init. No gfx API change needed; just inline the three calls
   (rtvHeap, sceneSrvHeap allocation is already via Scene, the cubemap heaps are tiny).

3. **Wire `updateRenderTargetViews` to `gfxSwapChain`** — currently queries
   `swapChain->GetBuffer(i)` to build RTV entries. After P1, `gfxSwapChain->backBufferAt(i)`
   returns `gfx::TextureHandle`; `nativeResource(handle)` gives the `ID3D12Resource*` for
   the `CreateRenderTargetView` call. The legacy `swapChain` field can then be dropped.

4. **`transitionResource()` helper** — currently takes `ComPtr<ID3D12GraphicsCommandList2>`
   and raw `D3D12_RESOURCE_STATES`. If the engine switches to creating a `gfx::ICommandList`
   wrapper early in `render()` (before the render graph), pass `gfx::ICommandList&` instead.
   This is a cosmetic change while the barriers are still emitted D3D12-style inside the lambda.

---

## P2 — Bindless Descriptor Heap + Root Signature Rewrite

This is the highest-risk single change in the entire migration.

### What changes

**Backend side (`src/gfx/`):**
- `BindlessHeap` in `d3d12_command.cpp` already exists (global SRV/UAV heap, 65k slots).
- Add `IDevice::bindlessSrvIndex(TextureHandle)` (already declared in `gfx.h`).
- Build the **bindless root signature** once at device init:
  ```
  [0] 16 root constants  b0  — per-draw indices (albedoIdx, normalIdx, etc.)
  [1] CBV                b1  — PerFrameCB
  [2] CBV                b2  — PerPassCB
  [3] SRV unbounded[]   t0, space0  — bindless texture array
  [4] Sampler unbounded[] s0, space0 — bindless sampler array
  ```
  The current engine root sig has 8 params and uses descriptor tables per resource type.
  The bindless sig has 5 params and a single unbounded array for all SRVs.

**Shader side (`src/shaders/`):**
- Replace `Texture2D shadowMap : register(t1)` / `SamplerComparisonState shadowSampler : register(s0)` etc.
  with `Texture2D textures[] : register(t0, space0)` + bindless index passed via root constants.
- Every shader that samples a texture must accept an index root constant and use
  `textures[idx].Sample(...)` instead of a named resource binding.
- `PerPassCB` and `PerFrameCB` HLSL structs stay the same; just the binding registers change
  (b0 → b1/b2 with the new layout).
- This is the **largest shader-side change** and the most likely source of subtle visual regressions.

**Application side:**
- `rootSignature` and `gridRootSig` become `gfx::PipelineHandle`-internal (the backend
  builds and owns the root sig; callers only call `cmdRef.bindPipeline(pso)`).
- All `SetGraphicsRootSignature`, `SetGraphicsRootDescriptorTable`,
  `SetGraphicsRootConstantBufferView`, `SetGraphicsRoot32BitConstant` calls in render
  pass lambdas are replaced with `cmd.setRootConstants(slot, data, count)` and
  `cmd.bindPipeline(handle)`.
- `sceneSrvHeap` in `Scene` (currently 6+ SRV slots for per-object data, shadow, cubemap,
  SSAO) is replaced by bindless indices. The per-object structured buffer gets a bindless
  SRV slot; shadow map, cubemap, SSAO RT each get a bindless slot.
- All descriptor heap `SetDescriptorHeaps` calls disappear — the bindless heap is
  set once at frame start (or never, if the device always has it bound).

### Recommended approach

1. **Feature flag** — add `USE_BINDLESS` CMake option (default OFF). Implement the
   bindless path in a `#ifdef` block. Keep old descriptor-table path compilable until
   bindless is visually verified. Flip the default to ON once test scene matches.

2. **Shader-first** — write and test bindless HLSL before touching C++ binding code.
   Use `--spirv` DXC flag to check SPIR-V compatibility in parallel.

3. **One shader at a time** — start with `pixel_shader.hlsl` (most complex), verify
   with the integration test scene, then port `gbuffer_ps.hlsl`, `shadow_vs.hlsl`, etc.

4. **Validation layers** — run with the D3D12 debug layer and GPU-based validation
   (GBV) enabled during the shader port. GBV catches out-of-bounds bindless index
   accesses that would otherwise silently read garbage.

### High-risk areas

| Risk | Mitigation |
|---|---|
| Wrong bindless index → reads garbage → wrong lighting/shadows | Add `assert(idx < maxDescriptors)` in debug shader builds |
| Root sig layout mismatch between C++ and HLSL | Keep `PerFrameCB` / `PerPassCB` HLSL structs unchanged; only the register numbers change |
| Stencil ref and outline params lost | Outline pass uses `SetGraphicsRoot32BitConstants(rootOutlineParams, ...)` — port to a root constant slot explicitly |
| Grid pass uses its own `gridRootSig` | Port grid to the bindless sig at the same time or leave it on a separate "legacy" sig temporarily |
| ImGui DX12 backend bypasses gfx | `imgui_impl_dx12` needs the real `ID3D12Device*` and `ID3D12DescriptorHeap*`. Create a `gfx::ImGuiBridge` shim that exposes those two raw pointers only to ImGui init code |

---

## P14 — Final Cleanup

Once P2 is done, run the verification sweep:

```bash
# Should return hits ONLY under src/gfx/
grep -rn "ID3D12\|IDXGI" src/ --include="*.cpp" --include="*.ixx" --include="*.h"

# Subsystem headers should no longer include d3d12.h
grep -rn "#include <d3d12.h>" src/ --include="*.ixx"
```

Expected survivors (legitimate):
- `src/gfx/d3d12_*.cpp` / `d3d12_internal.h` — the entire backend
- `src/modules/render_graph.ixx` — still has `#include <d3d12.h>` for
  `D3D12_CPU_DESCRIPTOR_HANDLE` in `writeRenderTarget`/`writeDepthStencil`.
  Eliminate by replacing those parameters with a gfx equivalent (e.g. `gfx::RtvHandle`)
  once the bindless rewrite makes descriptor heaps gfx-owned.
- `src/application/application.cpp` — ImGui bridge will expose one raw pointer

Remove `include/d3dx12_clean.h` from includes outside `src/gfx/` once `render_graph.ixx`
no longer needs `CD3DX12_*` helpers.

Add a `MockDevice` smoke test in `tests/gfx_tests.cpp` that implements every `IDevice`
virtual to confirm the interface is fully overridable (a Vulkan backend slot-in test).

---

## Debugging Playbook for Remaining Work

### GPU validation for bindless (P2)

Enable GPU-based validation (GBV) for the bindless port — normal validation won't
catch out-of-bounds bindless index reads at the shader level.

```cpp
// In gfx_d3d12_device.cpp or Application::loadContent, add:
ComPtr<ID3D12Debug3> debug3;
if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug3)))) {
    debug3->SetEnableGPUBasedValidation(TRUE);
    // SetGPUBasedValidationFlags can reduce overhead for initial investigation:
    debug3->SetGPUBasedValidationFlags(D3D12_GPU_BASED_VALIDATION_FLAGS_NONE);
}
```

GBV emits a device-removed + informational message with the offending shader instruction
when a bindless read is out of range. Check the debug output window or spdlog for the
`D3D12 ERROR: ...` line.

**Note**: GBV roughly 10× slows GPU work and massively inflates VRAM. Only enable it
when actively debugging a visual corruption.

### TDR diagnosis pattern

`DXGI_ERROR_DEVICE_REMOVED` (`0x887a0005`) is always reported at the first D3D12 call
**after** the GPU fault, not at the faulty call itself. When you see it:

1. Enable the debug layer (`IsDebuggerPresent()` gate is already in `window.cpp`).
2. Attach a debugger (VS or lldb) and run — the debug layer breaks at the actual offending
   draw/dispatch. The call stack will show the exact render pass.
3. Alternatively, check `GetDeviceRemovedReason()` immediately after any HRESULT failure:
   ```cpp
   if (FAILED(hr)) {
       HRESULT reason = device->GetDeviceRemovedReason();
       spdlog::error("Device removed reason: {:#010x}", static_cast<uint32_t>(reason));
   }
   ```
4. Common reasons for TDR during migration: descriptor heap not bound
   (`SetDescriptorHeaps` forgotten), root sig mismatch (C++ sets 5-param sig, shader
   compiled for 8-param sig), typeless resource used as SRV without typed view.

### Root signature / PSO mismatch (`0x80070057`)

`HRESULT 0x80070057 "The parameter is incorrect"` from `CreateGraphicsPipelineState`
means the PSO desc has invalid field values — most often:
- `StencilEnable = TRUE` but `FrontFace.StencilFunc` is 0 (invalid enum). Always
  populate both `FrontFace` and `BackFace` when stencil is on.
- `InputLayout.NumElements` doesn't match the shader's expected semantic list.
- Root sig parameter count in HLSL (implied by register slots) doesn't match the
  C++ `D3D12_ROOT_SIGNATURE_DESC`. Enable the debug layer — it will name the mismatched
  parameter.
- Format mismatch between DSV in PSO desc and the actual resource format.

### Descriptor heap binding gaps

When switching from the current per-pass heap-swap model to bindless, forgetting to call
`SetDescriptorHeaps` for the bindless heap before the first draw produces a silent
validation error (not a TDR). Symptoms: shadow map samples return 0 (white), SSAO
returns 1 (no occlusion), cubemap returns black. The values are wrong but the app
doesn't crash.

Bindless setup:
```cpp
// Once per frame, before any draw:
ID3D12DescriptorHeap* heaps[] = { bindlessHeap->heap.Get() };
cmdList->SetDescriptorHeaps(1, heaps);
```

After the heap is set, every `SetGraphicsRootDescriptorTable` for the unbounded range
only needs to be called once at the start of the frame, not per-pass. This is a common
source of "works in pass 1, broken in pass 2" bugs when partially migrating.

### Stale `.pcm` module files

If the build fails with "module file built from a different branch" or
"Microsoft Visual C++ Version differs in precompiled file", clear module cache:

```bash
find build -name "*.pcm" -delete
cmake --build build --config Debug
```

This happens after any Clang upgrade or Windows SDK bump. It is not related to whatever
code change you just made.

### Validation layer fatal breakpoint without a debugger

The D3D12 validation layer emits `STATUS_BREAKPOINT (0x80000003)` fatal exceptions when
it detects errors in a process without a debugger attached. The engine gates
`enableDebugging()` on `IsDebuggerPresent()` to prevent this. If the integration test
scene (`test.json`) produces a crash you can't reproduce with `--attach` debugging, check:

```cpp
// window.cpp — should be gated:
if (IsDebuggerPresent()) {
    enableDebugging(); // D3D12 debug layer + DXGI debug
}
```

If you need validation without an interactive debugger, use DRED (Device Removed
Extended Data) instead — it logs faults to a buffer you can read after the fact:

```cpp
ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings));
dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
// After device removal:
ComPtr<ID3D12DeviceRemovedExtendedData> dred;
device->QueryInterface(IID_PPV_ARGS(&dred));
D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
dred->GetAutoBreadcrumbsOutput(&breadcrumbs);
```

### Fence / synchronization debugging

Fence stalls (`waitForFenceVal`) that hang indefinitely usually mean:
- The command list was never submitted (`execCmdList` not called after the last work).
- The wrong fence value is being waited on (off-by-one from a prior frame's value).
- GPU TDR occurred before the signal — the fence never advances, wait spins forever.

Add a timeout-based poll and `GetDeviceRemovedReason` check:

```cpp
// Instead of blocking wait:
while (!cmdQueue.isFenceComplete(fenceVal)) {
    if (fence->GetCompletedValue() == UINT64_MAX) {
        // GPU removed
        spdlog::error("GPU fence never signalled — device removed");
        break;
    }
    Sleep(1);
}
```

### Tracy GPU profiling gaps after barriers move to `cmd.barrier()`

After the render graph switches to `cmd.barrier()` (P3 done), each barrier is its own
`ResourceBarrier(1, ...)` call. Tracy's GPU zones wrap the command list and timestamp
zone entry/exit — individual barriers between zones don't affect zone timing, but a
large number of one-at-a-time barriers (vs. batched) can inflate the "between pass"
gap visible in the Tracy timeline. If profiling shows unexplained time between zones,
check that the barrier calls aren't dominating:

```cpp
// To batch manually (if profiling shows it matters):
std::vector<D3D12_RESOURCE_BARRIER> barriers;
// ... collect ...
cmdList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
```

The render graph's `execute()` currently calls `cmd.barrier()` one at a time.
For P2 performance work, batch them inside the render graph by collecting the
gfx barriers and emitting a single D3D12 `ResourceBarrier` call per pass.

---

## Recommended Order of Attack

```
1. P13 remainder — Remove legacy device/swapChain ComPtr aliases (small, safe, no risk)
2. P2 — Bindless (one sprint: backend heap + root sig → shader port → visual verify)
3. P14 — grep sweep + MockDevice test
```

Do not attempt P2 while the engine is mid-feature-work — bindless is a whole-frame
change that invalidates the visual baseline across all passes simultaneously.
Schedule it as a dedicated migration sprint with `test.json` as the sole acceptance test.
```
