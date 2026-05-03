# Debugging notes

---

## WARP test scene crashed with TDR; uncovered a cluster of latent bugs (2026-05-03)

**Symptom**: `./main.exe resources/scenes/test.json` (WARP, headless) failed
with `DXGI_ERROR_DEVICE_REMOVED (0x887a0005)` thrown from
`pollOcclusionResults`'s `Map()`. The same scene on hardware GPU rendered an
almost-empty sky (visually broken but didn't crash). The Map call only
*reports* the TDR — the actual GPU fault was earlier in the frame.

### Diagnosis path
The debug layer was gated on `IsDebuggerPresent()`, so unattached WARP runs
gave no info on the cause. Two changes turned diagnosis tractable:

1. Enable the D3D12 debug layer for WARP runs as well (WARP doesn't have the
   "debug layer breakpoint kills unattached process" failure mode that the
   hardware path has).
2. Register an `ID3D12InfoQueue1::RegisterMessageCallback` that routes
   debug-layer messages to spdlog, with `SetBreakOnSeverity(FALSE)` for
   ERROR/WARNING/CORRUPTION so messages don't crash the process.

Once enabled, ~2500 D3D12 validation errors per frame surfaced — across
state-tracking, root-sig/PSO matching, and DSV format compatibility. WARP
escalates these to TDR; hardware drivers silently tolerate them (which is
why the scene "ran" but rendered garbage).

### Root cause cluster (six independent bugs)

1. **Render-graph state did not persist across frames.** `reset()` cleared
   `currentState`, so each frame's `importTexture(name, handle, initial)`
   re-asserted `initial` even though the actual D3D12 resource state was
   wherever the previous frame's last pass had left it. Symptom: ~50%
   of barriers had wrong `before`-state (hdr_rt, gbuffer_*, depth_buffer).
   *Fix*: `RenderGraph` now keeps a `persistentExternalStates` map keyed by
   `(name, handle.id)`. The handle key prevents stale state being reused
   when a resource is recreated (resize). `clearPersistentState()` is
   called from `Application::onResize`.

2. **Bloom did its own `transitionResource` on `hdr_rt` AND the render
   graph did the same.** Result: render graph emits `RT→PSR`, bloom then
   emits `RT→PSR` against an already-PSR resource. *Fix*: drop the bloom
   internal hdr_rt transitions; let the render graph (which already
   declares `readTexture(hHdrRT)` in the bloom pass setup) own them. Mip
   chain transitions stay internal — the mips aren't render-graph
   resources.

3. **Bloom PSOs were created with `bloomRootSignature` (3-param layout)
   but `bloom.render()` binds `bindlessRootSigNative()` at draw time**
   under `USE_BINDLESS` (which is the default). PSO/RS pointer
   mismatch → validation id=201 spam → eventual TDR on WARP. *Fix*:
   under `USE_BINDLESS`, pass the device's bindless root sig to the PSO
   builder. The bloom shaders are compiled with `USE_BINDLESS` so their
   register layout already matches the bindless root sig.

4. **`PipelineRecord::rootSig` always stored the device's bindless root
   sig**, ignoring the `nativeRootSignatureOverride` passed in the desc.
   Field was unused at the time so it didn't matter — until I wanted
   `bindPipeline()` to auto-bind the matching root sig (see #5). *Fix*:
   capture `pd.pRootSignature` (the value actually fed to
   `CreateGraphicsPipelineState`) into the record.

5. **`bindPipeline()` only set the PSO, not the root sig.** Caller had
   to remember to call `SetGraphicsRootSignature` separately. *Fix*:
   `bindPipeline` now also sets the matching root sig (taken from the
   PSO record). Setting the same RS that's already bound is a D3D12
   no-op for bindings; switching to a different RS unbinds prior
   parameters, which is the correct behavior — the caller will rebind
   for the new layout.

6. **Format mismatches between PSO `DSVFormat` and the actual depth
   resource**:
   - `cubemap_depth` was `D32_FLOAT` but the cubemap pass reuses the
     scene PSO (`D32_FLOAT_S8X24_UINT`). Switched cubemap_depth to
     `R32G8X24_TYPELESS` resource + `D32_FLOAT_S8X24_UINT` view.
   - billboard PSO had `DSVFormat = D32_FLOAT` but the billboard pass
     binds the application's main depth buffer
     (`D32_FLOAT_S8X24_UINT`).
   - Outline PSO had `stencilWriteMask = 0xFF` (default) with all
     stencil ops set to `KEEP`. D3D12 still considers any non-zero
     write mask + `StencilEnable = TRUE` as a potential write and
     refuses to draw against a `DEPTH_READ` depth-stencil. *Fix*: set
     `stencilWriteMask = 0` for read-only stencil draws.

### Lessons / heuristics

- **WARP is the canonical validation target.** Hardware drivers silently
  paper over real validation errors that WARP rejects with TDR. If
  hardware rendering looks "wrong" but doesn't crash, run the same
  scene on WARP with the debug layer on — that's where the actual bug
  list comes from.
- **Per-frame `importTexture(initial)` only works if the engine
  guarantees the resource is in `initial` at start of every frame.**
  For resources whose state actually changes across frames (almost
  everything), the render graph or some other layer must remember the
  end-of-frame state.
- **Don't transition the same resource from two layers.** If render
  graph imports it, render graph owns its transitions. If a subsystem
  owns it, don't import it.
- **For typeless depth-stencil + stencil read-only**, `stencilWriteMask
  = 0` is required even if all stencil ops are `KEEP`. D3D12 looks at
  the mask, not the ops, when deciding whether `DEPTH_READ` is legal.
- **PSO `DSVFormat` must match the resource's typed DSV format
  exactly**, not just be "compatible". `D32_FLOAT` ≠
  `D32_FLOAT_S8X24_UINT`.
- **`ID3D12InfoQueue1::RegisterMessageCallback` + `SetBreakOnSeverity
  FALSE`** is the right pattern for headless / unattached debug-layer
  diagnosis. The default break-on-error behavior makes the debug layer
  unusable without a debugger.
- **Auto-bind the root sig in `bindPipeline`.** Decoupling PSO and RS
  binding is a recipe for id=201 errors. Pair them at the wrapper
  level.

---

## SSAO PSO created with depth enabled — undefined behaviour, visual corruption on WARP

**Symptom**: After migrating `SsaoRenderer` PSOs from raw
`CreateGraphicsPipelineState` to `gfx::createGraphicsPipeline`, the WARP
integration test screenshot showed extreme "spiky" artefacts covering the
lower half of the viewport.  The artifacts were actually the existing
pre-migration baseline (identical pixel data), but the investigation revealed
a latent PSO mismatch:

**Root cause**: `gfx::GraphicsPipelineDesc::depthStencil.depthEnable` defaults
to `true` (mirroring D3D12 defaults for safety — callers opt out). The SSAO
and blur passes are fullscreen-triangle compute-style passes that must have
depth disabled. The original hand-written `D3D12_GRAPHICS_PIPELINE_STATE_DESC`
explicitly set `DepthStencilState.DepthEnable = FALSE`; the gfx-migrated
version forgot to set `pd.depthStencil.depthEnable = false`.

The visual artefacts in the test scene turned out to be a pre-existing SSAO
issue (extreme hemisphere-sampling noise on the infinite grid plane) that was
already in the committed baseline — not a regression from the PSO migration.
But the wrong `depthEnable` value was still a real latent bug.

**Fix**: Add to both SSAO and blur PSO descriptors:
```cpp
pd.depthStencil.depthEnable = false;
pd.depthStencil.depthWrite  = false;
```

**Also fixed in same commit**: `gfx::TextureUsage::operator|` was defined
in `include/gfx_types.h` (global module fragment of `gfx.ixx`) and therefore
not exported.  Added `using ::gfx::operator|;` to the `export namespace gfx`
block in `src/modules/gfx.ixx`.

**Also fixed**: `gfx::ShaderDesc` bytecode size field is `bytecodeSize`, not
`size`. Any migration that uses `ShaderDesc` should use `.bytecodeSize =`.

**Lesson**: when converting PSO creation to `gfx::GraphicsPipelineDesc`,
always explicitly set every non-default rasterizer/depth/blend state that the
original hand-written descriptor set — don't assume the gfx defaults match
D3D12 defaults (they don't for depth: gfx defaults `depthEnable = true`
because it's the common case for scene passes; pure fullscreen passes must
opt out).

---

Running log of non-obvious issues hit during development and the fixes that
made them go away. Format: one section per issue, newest at top. Include
enough context that the same issue is recognisable next time.

---

## Depth-buffer migration TDR on WARP — auto-DENY_SHADER_RESOURCE + auto-SRV on typeless format

**Symptom**: After migrating `Application::depthBuffer` from `ComPtr<ID3D12Resource>`
to `gfx::TextureHandle` via `gfxDevice->createTexture()`, running the
`resources/scenes/test.json` scene on the WARP adapter triggered
`DXGI_ERROR_DEVICE_REMOVED` (HRESULT 0x887a0005) on the *next*
`CreateGraphicsPipelineState` call. Log:

```
info> resizeDepthBuffer(1280, 720)
error> CreateGraphicsPipelineState failed with HRESULT 0x887a0005
       (The GPU device instance has been suspended.)
```

The error is reported at the next PSO call, not at the depth-buffer create
itself, because `CreateCommittedResource` does not surface the failure mode
that ultimately took the device down — it just leaves the runtime in a bad
state. Initial debugging mis-attributed the cause to the optimized clear
value's format; that is *also* an issue (typeless resource formats need a
typed clear-value format) but isn't the actual TDR trigger here.

**Root cause** (the real one): the gfx backend's `createTexture()` had two
silent traps for depth-stencil resources that engine code reads as SRV:

1. `toD3D12ResourceFlags(TextureUsage::DepthStencil)` automatically added
   `D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE` whenever the user did *not*
   explicitly include `TextureUsage::ShaderResource`. The runtime then
   either rejects SRV creation or returns a corrupt descriptor that
   crashes the GPU on the next draw that samples it. SSAO samples the
   main depth buffer, so this triggers.
2. When the user *did* include `ShaderResource`, the auto-SRV creation
   path called `CreateShaderResourceView` with the resource format. For
   typeless resources (`R32G8X24_TYPELESS`) this is not a valid SRV format
   and produces another corrupt descriptor.

So either path corrupted descriptors and TDR'd the GPU on the next frame.
WARP in particular escalates to device-removed rather than clean errors.

**Fix** (this commit): two changes in `src/gfx/`:

1. `toD3D12ResourceFlags` no longer adds `DENY_SHADER_RESOURCE`
   automatically — that flag is an optimization hint that prevents SRV
   reads, but several engine subsystems (SSAO, anything that samples
   depth) need to view a depth target as SRV with a custom format. Future
   work can add an explicit opt-in flag if compression hints become
   measurable.
2. `Device::createTexture` now skips the auto-SRV path when the resource
   format is typeless. The caller is expected to create their own typed
   SRV via `gfxDevice->nativeResource(handle)` +
   `device->CreateShaderResourceView`. Helper `isTypelessFormat()` lives
   in `d3d12_internal.h`.
3. New `TextureDesc::viewFormat` field — when set, the gfx backend uses
   it as the optimized clear value's `Format`, instead of the (typeless)
   resource format. Engine code passes `D32FloatS8X24Uint` here for the
   depth buffer.

`Application::resizeDepthBuffer` is now a clean gfx-flavored call. The
DSV is still created via raw D3D12 (`CreateDepthStencilView` against
`gfxDevice->nativeResource(depthBuffer)`) since the engine still owns
the `dsvHeap`.

**Lesson**: when the gfx backend silently picks defaults that differ from
what the engine code expects, errors surface as TDR rather than clean
runtime errors. Don't auto-DENY descriptor access; don't auto-create
views with formats the user didn't actually ask for.

**How to reproduce next time**: create a typeless depth-stencil resource
through gfx and sample it elsewhere. The TDR is silent until the next
GPU command list runs, at which point all subsequent D3D12 calls fail
with `0x887a0005`.

---

## Stencil-enabled PSOs rejected by gfx::createGraphicsPipeline

**Symptom**: First migration of the scene PSO to `gfx::PipelineHandle`
(stencil-enabled, `stencilPass = Replace`) failed with
`CreateGraphicsPipelineState failed with HRESULT 0x80070057
(The parameter is incorrect.)`.

**Root cause**: `gfxd3d12::Device::createGraphicsPipeline` was setting
`pd.DepthStencilState.StencilEnable / StencilReadMask / StencilWriteMask`
but never populating `pd.DepthStencilState.FrontFace` or `BackFace`. With
`StencilEnable = TRUE`, D3D12 validates the per-face `StencilFunc` /
`StencilFailOp` / `StencilDepthFailOp` / `StencilPassOp` fields — all of
which were left zero-initialised, which decode to invalid enum values, so
the runtime rejects the PSO.

**Fix** (commit `8efcc96`): translate
`gfx::DepthStencilState::stencilFail / stencilDepthFail / stencilPass /
stencilCompare` into a `D3D12_DEPTH_STENCILOP_DESC` and assign it to both
`pd.DepthStencilState.FrontFace` and `pd.DepthStencilState.BackFace`. The
existing single-sided gfx API mirrors the descriptor onto both faces (the
engine never sets different ops per side).

**Lesson**: when adding a new field to a gfx descriptor struct, check
whether the D3D12 backend's PSO desc has *all* the dependent fields
populated — D3D12 silently picks default values for fields you never
write, but those defaults are zero, not the API "ALWAYS / KEEP" defaults
the gfx user might expect.

---

## Stale `.pcm` module files after Clang upgrade

**Symptom**: After `clang-format` or any large refactor, builds occasionally
fail with:

```
error: Microsoft Visual C/C++ Version differs in precompiled file
       'CMakeFiles/main.dir/Debug/foo.pcm' vs. current file
```

or

```
error: module file 'CMakeFiles/main.dir/Debug/scene.pcm' built from a
       different branch (...) than the compiler (...)
```

**Root cause**: the C++23 module precompiled files (`.pcm`) embed the
exact compiler ABI / version. Any Clang upgrade or even a Windows-SDK
revision bump invalidates them. CMake's dyndep doesn't always notice.

**Fix**: clear the module cache, then rebuild. Don't run
`cmake --fresh` (regenerates everything including vcpkg deps — slow):

```bash
find build -name "*.pcm" -delete
cmake --build build --config Debug
```

**Note**: this is unrelated to whatever change you just made — the
errors look terrifying but are purely a stale-cache issue. If only some
.pcm files are stale, the offending compile units are usually the ones
that depend on the latest module interface that got rebuilt; nuking all
.pcm files is fastest.

---

## Application::createScenePSO destruct-then-create ordering

**Symptom (anticipated, defended against)**: hot-shader-reload calls
`createScenePSO()` repeatedly. The first call leaves `pipelineState`
in a default-constructed (id=0) state; subsequent calls leak the
previous `gfx::PipelineHandle` and `gfx::ShaderHandle`s if not
explicitly destroyed.

**Fix** (commit `8efcc96`, `db66254`): every `create*PSO()` method
starts with:

```cpp
if (pipelineState.isValid()) gfxDevice->destroy(pipelineState);
if (scenePsoVS.isValid()) gfxDevice->destroy(scenePsoVS);
if (scenePsoPS.isValid()) gfxDevice->destroy(scenePsoPS);
```

The destroy is fence-tracked inside gfx (it queues the `ComPtr` into
`pendingDestroys` until the queue catches up), so calling destroy mid-frame
is safe.

**Lesson**: gfx-handle fields default to `{ id = 0 }` which is the null
slot. Always guard `destroy()` on `.isValid()` so the first-time create
path doesn't try to free the null handle. The mock backend and the D3D12
backend both treat `id = 0` as a no-op for safety, but explicit guards
keep the call sites honest.

---

## CommandQueue / gfx::IQueue split owning the same ID3D12CommandQueue

**Symptom (when designing P1)**: with two queue wrappers on the same
underlying `ID3D12CommandQueue`, fence values diverge — the gfx queue
signals fence A, CommandQueue signals fence B, the swap chain Present
synchronises with the queue itself, not either fence.

**Fix** (commit `2cfb1ef`): `CommandQueue` gained an "adopt existing
`ID3D12CommandQueue`" constructor. `Application` now constructs its
`CommandQueue` from the gfx queue's native handle (`graphicsQueue()->
nativeHandle()`). They share one queue. Each wrapper still owns its own
fence — that's fine since fences are independent objects, and the engine
only consults `CommandQueue`'s fence (the gfx fence is unused for the
main render path).

**Don't**: create a second `D3D12_COMMAND_QUEUE_DESC` queue alongside the
gfx queue. The swap chain is bound to one specific queue at creation
time, and Present can only synchronise with that queue.

---

## No-arg launch Flecs invalid-entity assert (2026-04-15)

**Symptom**: `main.exe` without arguments aborted with `fatal: flecs_cpp.c: assert(ecs_is_alive(world, entity))`. Test scene worked fine.

**Root cause**: `runtime.singleTeapotMode` path calls `clearScene()`, which deletes gizmo arrows (`GizmoArrow + MeshRef`). `GizmoState` still held stale entity handles and touched them on next update.

**Fix pattern**: Reinitialize gizmo immediately after any clear/reload path:
- deferred reset-to-teapot
- non-append GLTF load
- scene-file load path after apply
- single-teapot branch in `applySceneData()`

**Reusable tips**:
1. When a crash is scene-dependent, compare no-arg startup with `resources/scenes/test.json` first.
2. Audit every path that calls `clearScene()` and list subsystems that cache entity IDs/handles.
3. Treat cached ECS handles as invalid across clears unless explicitly rebuilt.
4. Prefer resetting/reinitializing subsystem state over adding ad hoc `is_alive()` guards everywhere.
5. Verify with both interactive startup and headless/integration flow.

---

## Default scene launch crash (2026-04-14)

**Symptom**: `main.exe` without arguments crashed immediately. Test scene (WARP + hidden window) worked fine.

**Debugging process**:

1. **Initial triage**: Added SEH exception filter to capture crash. First crash: `STATUS_BREAKPOINT (0x80000003)` in D3D12 runtime.

2. **D3D12 debug layer**: Was unconditionally enabled in Debug builds. Without a debugger, debug layer breakpoints crash the process. **Fix**: Gated `enableDebugging()` on `IsDebuggerPresent()` in `window.cpp`.

3. **Second crash**: Access violation during first `render()` call, triggered by `WM_PAINT` handler running before message loop started.

4. **Premature WM_SIZE / WM_PAINT**: `ShowWindow(SW_SHOW)` sends `WM_SIZE` synchronously, triggering `onResize()` → `ResizeBuffers()` during window initialization. **Fix**: Added `Window::inMessageLoop` flag (default `false`). Set to `true` in `main.cpp` just before `ShowWindow`. Removed `UpdateWindow()`.

5. **Hardcoded fullscreen**: Application constructor called `setFullscreen(true)`, generating synchronous `WM_SIZE` events during construction. **Fix**: Replaced with `ConfigData::startFullscreen` (default `false`), applied via deferred fullscreen mechanism.

6. **Environment-specific residual**: Crash persisted on development machine (Parsec Virtual Display Adapter). Confirmed as environment issue, not a code bug.

**Key takeaway**: Multiple independent issues compounded. The SEH filter was essential for capturing crash codes without a debugger. The `inMessageLoop` pattern prevents a class of init-order bugs.
