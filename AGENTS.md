# AGENTS.md — DX12 Mini Engine

Guidance for AI agents (Claude Code, Codex, etc.) working in this repository. Use you're own section for agent-specific stuff.

## Tools, Plugins, Skills, etc.

Use these tools PLEASE (only if they don't fail):

* Use `context7` if:
  * **KEYWORDS:** library, dependency, `lldb`, `eigen`, and anythin formatting like this: `author/dependency` (github repo path)
  * **NOTE:** *required! exit on fail*
* Use `github` if:
  * **KEYWORDS:** github, gh, repo

See @README.md


---

## Build

* `VCPKG_ROOT` should be assumed to be set correctly
* If there is some external issue with the build environment that is not part of the task itself, wait, then retry the build. If it still fails, **STOP** and wait for user input, stating there is an issue with the build environment.

```bash
# Configure (Ninja Multi-Config, Clang 22 from LLVM)
cmake --preset windows-clang

# Build (Debug, Release builds)
cmake --build build --config Debug
cmake --build build --config Release

# Test (loads test scene: WARP adapter, 10 frames, screenshot, exit)
./build/Debug/main.exe resources/scenes/test.json

# Unit tests (doctest + CTest, includes lua_scripting_tests)
ctest --test-dir build -C Debug --output-on-failure
```

**NOTE:** Sometimes the build will fail with a file lock issue ("user-mapped section open"). When this happens, stop execution immediately. The issue is likely due to the language server in the open editor. The user must shut it down before you can continue.

### Build System Principles

* Use Modern CMake (use )

### Toolchain notes

* **Compiler**: `clang++` (v22 or newer). Do NOT use Git's clang (v18 — too old for VS 18 STL).
* **vcpkg**: `$VCPKG_ROOT` (x64-windows-static triplet).
* **Presets**: `windows-clang` (primary), `windows-msvc` (do not use!).
* Shaders compiled via DXC to `.cso` headers at build time.

### Rules (IMPORTANT)

**Before working on a task / during planning phase:**

* Determine the best place to added new functionality. If it's a new feature, try making a new module file for it
* If working on a new feature, use the web search tool to perform initial research on the topic.
* If asked to create a new feature/mode/option for objects, properties, or relationships, consider how to integrate it with the existing ECS system.

**After finishing every task:**

* build then run test scene and inspect `screenshot.png`
  * `./build/Debug/main.exe resources/scenes/test.json`
  * read `screenshot.png` with the Read tool, and visually verify the result looks correct before reporting done
* `git pull`
* Update `AGENTS.md` to reflect any new or modified architecture, modules, rendering pipeline steps, UI panels, key patterns, or dependencies. Keep it accurate and current.
* Run `clang-format` on all source and header files
* If any of the changed files in the working tree are longer than 1100 lines, split the file
  * Create new modules (.ixx) / source files (.cpp) as needed


---

## Architecture

From-scratch DirectX 12 renderer. C++23 modules, Clang, Windows-only.

### Module files (`src/modules/*.ixx`)

| Module | Purpose |
|----|----|
| `math.ixx` | Re-exports math types from `include/math_types.h` |
| `common.ixx` | `chkDX()` + Win32/HRESULT formatting helpers, `_deg` literals, pi constants. Re-exports `math` |
| `window.ixx` | Singleton HWND + tearing detection. Device creation has moved to the `gfx` abstraction (see below). |
| `gfx.ixx` | Backend-agnostic graphics abstraction (`gfx::IDevice`, `IQueue`, `ISwapChain`, `ICommandList`). Thin re-export of `include/gfx.h` for `import gfx;` clients. D3D12 backend is in `src/gfx/*.cpp`. |
| `application.ixx` | Main Application class — orchestrates subsystems, render loop, input, UI |
| `scene.ixx` | `Scene` class — ECS world, mega-buffers, draw-data, materials, mesh loading |
| `bloom.ixx` | `BloomRenderer` class — HDR RT, bloom mip chain, root sig, PSOs |
| `imgui_layer.ixx` | `ImGuiLayer` class — descriptor heap, init/shutdown, Dracula style |
| `command_queue.ixx` | ID3D12CommandQueue + fence sync + command allocator pooling |
| `camera.ixx` | Re-exports Camera + OrbitCamera from `include/camera_types.h` |
| `input.ixx` | Button/Key enums, gainput integration, `EditorAction` enum, `HotkeyBindings` struct, key name lookup |
| `ecs_components.ixx` | Re-exports ECS components from `include/ecs_types.h` (Transform, PrevTransform, Animated, Pickable, LodMesh, BoundingVolume, MeshRef, InstanceGroup, PrevInstanceGroup, InstanceAnimation, TerrainEntity, PointLight, GizmoArrow, GizmoAxis) |
| `shader_hotreload.ixx` | `ShaderCompiler` class — watches HLSL files, recompiles via DXC at runtime |
| `gizmo.ixx` | `GizmoState` struct — translation gizmo (3 arrow entities, drag logic) |
| `billboard.ixx` | `BillboardRenderer` class — point light sprite rendering |
| `object_picking.ixx` | `ObjectPicker` class — ID render pass, readback for entity picking |
| `terrain.ixx` | `TerrainParams` struct + `generateTerrain()` — Perlin noise heightmap mesh |
| `config.ixx` | `ConfigData` struct + load/save/merge config.json via glaze |
| `lua_scripting.ixx` | `LuaScripting` class + `Scripted` component — Lua 5.4 scripting engine |
| `scene_file.ixx` | Scene file serialization — load/save JSON scene files via glaze |
| `ssao.ixx` | `SsaoRenderer` class — SSAO compute + blur passes (now reads from GBuffer Normal) |
| `shadow.ixx` | `ShadowRenderer` class — shadow map texture, DSV, PSO, render + reloadPSO |
| `outline.ixx` | `OutlineRenderer` class — stencil-based silhouette PSO + render |
| `render_graph.ixx` | `rg::RenderGraph` class — pass orchestration and automated resource barriers |
| `logging.ixx` | spdlog setup with custom error sink |
| `gbuffer.ixx` | `GBuffer` class — 4-target G-Buffer (Normal, Albedo, Material, Motion), RTV+SRV heaps |
| `restir.ixx` | `ReStirRenderer` class — skeleton for ReSTIR DI (reservoir buffers, root sig; shaders TBD) |

### Module conventions

* **No** `export using namespace` — each file declares its own `using` locally (e.g., `using Microsoft::WRL::ComPtr;`)
* `export import` only for types in public API — use plain `import` for internal dependencies
* `module :private;` for tiny implementations — avoids separate `.cpp` for <20 lines
* `include/math_types.h` defines vec2/vec3/vec4/mat4 and math functions — included by both modules and plain TUs (e.g. `glaze_impl.cpp`)
* `include/ecs_types.h` defines Transform, Animated, Pickable, MeshRef, InstanceGroup, InstanceAnimation, TerrainEntity, PointLight — included by `ecs_components.ixx`, `scene_data.h`
* `include/material_types.h` defines Material, MaterialPreset — included by `scene.ixx` and `scene_data.h`
* `include/camera_types.h` defines Camera (abstract base) + OrbitCamera — included by `camera.ixx` and `scene_data.h`; `OrbitCamera` used directly in `SceneFileData` (glz::meta excludes `aspectRatio`)
* `include/terrain_types.h` defines TerrainParams (geometry + material/position fields) — included by `terrain.ixx` and `scene_data.h`; replaces former `TerrainData` duplicate
* `include/d3dx12_clean.h` wraps `<directx/d3dx12.h>` (from DirectX-Headers vcpkg) with Clang warning suppression
* `include/icons.h` defines Material Icons codepoints (`IconCP::*`), `iconUtf8()`, `iconCodepointFromName()`, `iconStr()` — included by `application.ixx`, `imgui_layer.cpp`, `application.cpp`
* **Application public API is minimal** — only `update()`, `render()`, `runtimeConfig`, `cam`, `inputMap`, `keyboardID`, `applySceneData()`, `extractSceneData()` are public

### Graphics abstraction (`gfx::`)

The engine is being migrated off raw D3D12 onto a backend-agnostic `gfx::` API in `include/gfx.h` + `include/gfx_types.h` (re-exported via `src/modules/gfx.ixx`). The D3D12 backend lives in `src/gfx/`:

| File | Purpose |
|----|----|
| `include/gfx_types.h` | POD types: handles (`TextureHandle`, `BufferHandle`, ...), enums (`Format`, `ResourceState`, ...), descriptor structs (`TextureDesc`, `GraphicsPipelineDesc`, ...), `Capabilities`. |
| `include/gfx.h` | `IDevice`, `IQueue`, `ISwapChain`, `ICommandList` interfaces + `gfx::createDevice` factory. Plain header — usable from both module and non-module TUs. |
| `src/modules/gfx.ixx` | Thin re-export wrapper for `import gfx;` clients. |
| `src/gfx/d3d12_internal.h` | Private internal header for the D3D12 backend split: `Device`/`Queue`/`CommandList`/`SwapChain` class declarations, `BindlessHeap`, format/state conversion helpers. |
| `src/gfx/d3d12_backend.cpp` | `Device` class + `gfx::createDevice` factory. |
| `src/gfx/d3d12_command.cpp` | `Queue` + `CommandList` + `BindlessHeap` impl. |
| `src/gfx/d3d12_swapchain.cpp` | `SwapChain` impl. |
| `tests/gfx_tests.cpp` | Doctest smoke tests (WARP device, buffer/texture creation, fence sync, mock backend). |

**Migration status (2026-05-02):**
- P0 ✅ — gfx skeleton + D3D12 backend stubs landed.
- P1 ✅ — `Application` owns `gfx::IDevice` + `gfx::ISwapChain`; `Window` no longer creates the device.
- P2 ✅ (lighter variant) — All scene SRVs (per-object structured buffer, shadow map typed view, cubemap, SSAO blur RT) moved from the engine's old `scene.sceneSrvHeap` into the gfx device's single bindless heap. Descriptor table ranges in the 8-param root sig are now `DESCRIPTORS_VOLATILE` so each pass points into arbitrary offsets within the bindless heap. New `IDevice` API: `srvGpuDescriptorHandle(index)`, `createTypedSrv(handle, format)`, `srvHeapNative()`. Shadow `createResources` no longer takes a heap/slot; cubemap creation uses `TextureUsage::ShaderResource` for auto-SRV; SSAO adds public `blurRT()` getter. Shader register layout unchanged (no HLSL rewrite); full bindless indexing in shaders is deferred.
- P3 ✅ — `rg::RenderGraph` callbacks take `gfx::ICommandList&`. `importTexture` now takes `gfx::TextureHandle` + `gfx::ResourceState`. `RenderGraph` ctor takes `gfx::IDevice&`.
- P4 ✅ — `GBuffer::createResources/resize/transition` take `gfx::IDevice&` / `gfx::ICommandList&`.
- P5 ✅ — `ShadowRenderer::createResources/reloadPSO/render` take gfx types.
- P6 ✅ — `SsaoRenderer::createResources/resize/render/transitionResource` take gfx types.
- P7 ✅ — `BloomRenderer::createResources/resize/render/reloadPipelines` take gfx types. `hdrRT` + `bloomMips[]` are `gfx::TextureHandle`; PSO/root sig/heaps stay ComPtr.
- P8 ✅ — `OutlineRenderer::createResources/reloadPSO/render` take gfx types.
- P9 ✅ — `ObjectPicker::createResources/resize/copyPickedPixel` take gfx types.
- P10 ✅ — `BillboardRenderer::init/render` take gfx types. `quadVertexBuffer`+`instanceBuffer` are `gfx::BufferHandle`. `ImGuiLayer::init` takes `gfx::IDevice&`.
- P11 ✅ — `GizmoState::init` takes `gfx::IDevice&`.
- P12 ✅ — `Scene::createMegaBuffers/createDrawDataBuffers/appendToMegaBuffers/loadTeapot/loadGltf` all take `gfx::IDevice&`. Mega VB/IB and all per-frame/per-pass/per-object buffers are `gfx::BufferHandle`. Upload-path uses `dev.uploadBuffer()`.
- P13 ✅ — Application's scene/gbuffer/grid PSOs are `gfx::PipelineHandle`. VS/PS bytecodes go through `gfx::ShaderHandle`. `depthBuffer`, cubemap color + depth, back buffers are `gfx::TextureHandle`. Legacy `device`/`swapChain` ComPtr aliases removed; Application now obtains raw `ID3D12Device2*` via `static_cast<ID3D12Device2*>(gfxDevice->nativeHandle())` wherever D3D12-only APIs are needed (root sig creation, subsystem heap management). **Application-owned descriptor heaps (`rtvHeap`, `dsvHeap`, `cubemapRtvHeap`, `cubemapDsvHeap`) fully removed** — gfx backend auto-creates RTVs/DSVs in private CPU-only heaps at texture creation time; Application uses `gfxDevice->rtvHandle(h, slice)` / `gfxDevice->dsvHandle(h, slice)`. `render_graph.ixx` public API purged of all D3D12 types (`rg::TextureDesc` uses `gfx::Format`/`gfx::TextureUsage`; `writeRenderTarget`/`writeDepthStencil` no longer take a `D3D12_CPU_DESCRIPTOR_HANDLE`). `render.cpp` no longer includes `d3dx12_clean.h`.
- P13 (deferred) — `rootSignature`/`gridRootSig` ComPtrs (deferred to full bindless rewrite). BLAS/TLAS resources capability-gated. `spriteTexture` in BillboardRenderer stays ComPtr (no `adoptTexture` in gfx yet). CPU-only subsystem RTV/DSV heaps (GBuffer `rtvHeap`, SSAO `rtvHeap`, shadow `dsvHeap`, picker, bloom `bloomRtvHeap`) still ComPtr — deferred to P14.
- P14 ✅ (blocked remainder deferred to bindless rewrite) — Shader-visible SRV heaps removed from Bloom, SSAO, and Billboard subsystems. All three now use the global gfx bindless heap via `srvHeapNative()` + `srvGpuDescriptorHandle()`. SSAO root sig refactored to 3 separate VOLATILE 1-SRV descriptor tables. Billboard sprite registered via `createExternalSrv()`. New `gfx::Format::R32FloatX8X24Typeless` added for SSAO depth SRV. New `gfx::VertexBufferView` / `gfx::IndexBufferView` POD types replace `D3D12_VERTEX_BUFFER_VIEW` / `D3D12_INDEX_BUFFER_VIEW` in all exported module interfaces (`scene.ixx`, `shadow.ixx`, `outline.ixx`, `billboard.ixx`); D3D12 conversion now only in `.cpp` implementation files. `ID3D12DescriptorHeap*` removed from `shadow.render()` and `OutlineRenderContext` (both now get the global heap from `devForDestroy->srvHeapNative()` internally). **D3D12 types purged from all exported `.ixx` interfaces (2026-05-02):** `D3D12_VIEWPORT`/`D3D12_RECT` → `gfx::Viewport`/`gfx::ScissorRect` (`gfx.ixx`, `application.ixx`, `gizmo.ixx`); `D3D12_SHADER_BYTECODE` → `gfx::ShaderBytecode` (`shadow.ixx`, `outline.ixx`, `bloom.ixx`); `D3D12_GPU_VIRTUAL_ADDRESS`/`D3D12_CPU_DESCRIPTOR_HANDLE`/`D3D12_GPU_DESCRIPTOR_HANDLE` → `uint64_t` (`bloom.ixx` `backBufRtv`, `object_picking.ixx` `getRTV`/`getDSV`, `application.ixx` `clearRTV`/`clearDepth`); `ComPtr<ID3D12RootSignature>` in `object_picking.ixx` `createResources` → `ID3D12RootSignature*`. `gizmo.ixx` and `gizmo.cpp` now have zero `#include <d3d12.h>`. Inline D3D12 type construction (e.g. `D3D12_CPU_DESCRIPTOR_HANDLE{ val }`) kept in `.cpp` files at D3D12 API call sites. New `gfx::ShaderBytecode` struct added to `gfx_types.h` and re-exported via `gfx.ixx`. **Additional interface purges:** `application.ixx` — `transitionResource`/`clearRTV`/`clearDepth` helpers (dead or trivial) removed; `renderImGui(ComPtr<ID3D12GraphicsCommandList2>)` → `renderImGui(gfx::ICommandList&)`; `#include <dxgi1_6.h>` removed. `bloom.ixx` — `createPipelines(ID3D12Device2*)` → `createPipelines(gfx::IDevice&)`; `nativeDev()`/`nativeCmd()` inline helpers removed from class (inlined at call sites in bloom.cpp). `imgui_layer.ixx` — `DXGI_FORMAT rtvFormat` → `gfx::Format rtvFormat` in `init()`; `#include <dxgi1_6.h>` removed. `restir.ixx` — full public API migrated to `gfx::IDevice&`/`gfx::ICommandList&`/`gfx::TextureHandle`/`gfx::BufferHandle`/`uint64_t`; `restir.cpp` and `restir.ixx` added to CMakeLists.txt (were previously dead code, not compiled). **Third interface purge batch:** `ssao.ixx` — `transitionResource(gfx::ICommandList&, ID3D12Resource*, D3D12_RESOURCE_STATES, D3D12_RESOURCE_STATES)` public static helper removed; `nativeDev()`/`nativeCmd()` inline private helpers removed — all converted to file-static free functions in `ssao.cpp`. `scene.ixx` — `nativeDev()` private static helper removed; cast inlined at call sites in `scene.cpp`. `restir.ixx` — private `createShaders`/`createTextures` params changed from `ID3D12Device2*` to `gfx::IDevice&`; `restir.cpp` updated accordingly. **Fourth interface purge batch:** `bloom.ixx` — `bloomRootSignature`/4 PSO ComPtrs moved from public to private section (not accessed externally). `scene.ixx` — BLAS/TLAS ComPtr fields and `updateTLAS`/`buildBlasForMesh` methods moved to private section (all internal to `scene.cpp`). **Remaining leaks (all blocked on bindless root sig rewrite or architecture):** `ID3D12RootSignature*` params in `shadow.ixx` `createResources`/`reloadPSO`, `outline.ixx` `createResources`/`reloadPSO`/`OutlineRenderContext`, `object_picking.ixx` `createResources`; `ComPtr<ID3D12RootSignature>`/PSO ComPtrs in all subsystem private fields; `D3D12_PLACED_SUBRESOURCE_FOOTPRINT`/`ComPtr<ID3D12Resource>` in `SsaoRenderer` (noise upload); `D3D12_COMMAND_LIST_TYPE` in `CommandQueue` (D3D12-specific class); `ID3D12CommandQueue*` in `ImGuiLayer::init` (required by imgui_impl_dx12); `ImGuiLayer::srvHeap`; `spriteTexture` WIC resource.

**`gfx::Format`** now covers RGB32Float, D16Unorm, D24UnormS8Uint, D32Float, D32FloatS8X24Uint, and the typeless variants R32Typeless / R32G8X24Typeless / R32FloatX8X24Typeless (SRV-only view for depth plane of D32_FLOAT_S8X24_UINT). `IDevice::createExternalSrv(void* nativeResource, Format, mipLevels, isCubemap)` registers an externally-owned `ID3D12Resource*` in the bindless heap (used by Billboard sprite texture). **`gfx::TextureDesc::viewFormat`** specifies a typed format used for the optimized clear value when the resource format is typeless. The bug where `createGraphicsPipeline` left `pd.DepthStencilState.FrontFace/BackFace` zero-initialised when stencil was enabled is fixed. The gfx backend no longer auto-adds `DENY_SHADER_RESOURCE` for depth-stencil resources, and skips auto-SRV creation when the resource format is typeless (caller creates their own typed SRV via `nativeResource()`).

**RTV/DSV auto-management**: The D3D12 backend owns private CPU-only `rtvHeap_` (512 slots) and `dsvHeap_` (256 slots). `createTexture` automatically allocates RTV/DSV slots (via `allocateBatch(arraySlices)`) when `TextureUsage::RenderTarget` or `TextureUsage::DepthStencil` is set. `adoptBackBuffer` also allocates an RTV slot. Callers retrieve handles via `IDevice::rtvHandle(handle, arraySlice)` / `IDevice::dsvHandle(handle, arraySlice)` which return `uint64_t` (cast to `D3D12_CPU_DESCRIPTOR_HANDLE::ptr`). Slots are freed in `destroy(TextureHandle)` and `releaseBackBuffer`. `BindlessHeap::allocateBatch(count)` atomically allocates consecutive slots; `freeBatch(base, count)` returns them to the free list.

**What still leaks D3D12 in subsystems (blocked on bindless root sig rewrite):**
- `ComPtr<ID3D12Resource>` for BLAS/TLAS (RT-only, capability-gated) and `spriteTexture` (WIC-loaded; no `adoptTexture` in gfx yet).
- `ComPtr<ID3D12DescriptorHeap>` in `ImGuiLayer::srvHeap` and `ReStirRenderer::uavHeap`. All other shader-visible SRV heaps (Bloom, SSAO, Billboard) have been migrated to the global bindless heap.
- `ComPtr<ID3D12RootSignature>` and `ComPtr<ID3D12PipelineState>` in Bloom, SSAO, Billboard, Shadow, Outline, ObjectPicker, ReStir, and Application (root sig + grid root sig).
- `ID3D12CommandQueue*` in `ImGuiLayer::init` — required by `imgui_impl_dx12`, cannot be easily replaced.
- `D3D12_RESOURCE_STATES` in render pass lambdas for direct D3D12 binding/clear calls (render graph state tracking itself uses `gfx::ResourceState`).

**Bindless model**: a single global SRV/UAV heap (default 65k descriptors) and a single sampler heap; `IDevice::bindlessSrvIndex(handle)` returns the slot. `IDevice::srvGpuDescriptorHandle(index)` returns the GPU handle (`uint64_t`) for use in `SetGraphicsRootDescriptorTable`. `IDevice::srvHeapNative()` returns the `ID3D12DescriptorHeap*` for `SetDescriptorHeaps`. `IDevice::createTypedSrv(TextureHandle, Format)` allocates a typed SRV in the bindless heap for typeless resources (e.g. R32Typeless shadow map → R32Float view). Bindless root signature (future): `[16 root constants b0][CBV b1][CBV b2][SRV unbounded table][sampler unbounded table]`.

**Capability gating**: `caps.raytracing`/`caps.meshShaders` queried at device init. RT calls (`createAccelStruct`, `traceRays`) throw on unsupported devices.

**Escape hatch**: `IDevice::nativeHandle()` returns `ID3D12Device2*` (etc.). To be removed at P14 once subsystem migration completes.

### Subsystem architecture

Application owns subsystem instances: `Scene scene`, `BloomRenderer bloom`, `ImGuiLayer imguiLayer`, `ShadowRenderer shadow`, `OutlineRenderer outline`, `SsaoRenderer ssao`, `ObjectPicker picker`, `BillboardRenderer billboards`, `GizmoState gizmo`, `rg::RenderGraph renderGraph`.

**RenderGraph** (`render_graph.ixx` + `render_graph.cpp`) — orchestrates rendering passes:

* Handles automated resource state transitions (barriers) based on pass inputs/outputs.
* Methods: `importTexture()`, `addPass()`, `execute()`.
* Use `RenderGraphBuilder` in pass setup to declare `readTexture()`, `writeRenderTarget()`, and `writeDepthStencil()`.

**Scene** (`scene.ixx` + `scene.cpp`) — owns all scene state:

* ECS world (`flecs::world`), cached queries (`drawQuery`, `instanceQuery`, `instanceAnimQuery`, `animQuery`, `lightQuery`), materials, spawn system
* Mega vertex/index buffers (1M verts, 4M indices, default heap)
* Triple-buffered structured draw-data buffers (`SceneConstantBuffer`)
* Methods: `createMegaBuffers()`, `createDrawDataBuffers()`, `appendToMegaBuffers()`, `clearScene()`, `loadTeapot()`, `loadGltf()`
* Also exports: `VertexPBR`, `SceneConstantBuffer`, `Material`, `MaterialPreset` structs/enums
* **Preset materials**: `MaterialPreset` enum (`Diffuse`, `Metal`, `Mirror`). Created in both `loadTeapot()` and `loadGltf()`. Indices stored in `Scene::presetIdx[]`. Spawned entities get a random preset — diffuse uses random HSL color, metal darkens the color to 25%, mirror uses material's black albedo.

**BloomRenderer** (`bloom.ixx` + `bloom.cpp`) — owns all bloom/post-processing state:

* HDR render target, 5-mip bloom chain textures and descriptor heaps
* Bloom root signature + 4 PSOs (prefilter, downsample, upsample, composite)
* Methods: `createResources()`, `resize()`, `render()`

**ObjectPicker** (`object_picking.ixx` + `object_picking.cpp`) — entity picking via ID render pass:

* R32_UINT render target (viewport-sized), own depth buffer, cleared to 0
* Reuses scene vertex shader + ID pixel shader (`src/shaders/id_ps.hlsl`) that outputs `drawIndex + 1` (0 = no entity)
* PSO created from scene root signature (needs structured buffer + drawIndex root constant)
* Single-pixel readback at mouse position each frame (1-frame latency); readback value is decremented by 1 to get actual draw index
* `drawIndexToEntity` vector (in Application) maps draw index → flecs entity
* Methods: `createResources()`, `resize()`, `readPickResult()`, `copyPickedPixel()`

**GBuffer** (`gbuffer.ixx` + `gbuffer.cpp`) — deferred G-Buffer subsystem:

* 4 render targets: Normal (`R8G8B8A8_UNORM`), Albedo (`R8G8B8A8_UNORM`), Material (`R8G8_UNORM`, roughness+metallic), Motion (`R16G16_FLOAT`)
* Own RTV heap (4) and SRV heap (4); created in `loadContent()`, resized in `onResize()`
* G-Buffer pass uses `vertex_shader.hlsl` + `gbuffer_ps.hlsl` with 4 MRT outputs
* `gbufferPSO` owned by Application (created by `createGBufferPSO()` in `setup.cpp`)
* Motion vectors computed from `PrevViewProj` × `PrevModel` (new `PrevTransform` / `PrevInstanceGroup` ECS components)

**SsaoRenderer** (`ssao.ixx` + `ssao.cpp`) — SSAO subsystem:

* SSAO RT (`R8_UNORM`), blur RT (`R8_UNORM`); reads normal from GBuffer Normal target
* Own RTV heap (2) and SRV heap (4: normal, depth, noise, ssaoRT); SSAO output SRV placed in `sceneSrvHeap[nBuffers+2]` for scene pass
* Hemisphere kernel (32 samples, seed 42), 4×4 `R32G32_FLOAT` random-rotation noise texture (deferred upload on first render)
* Persistently-mapped CBV upload buffer with view/proj/invProj matrices + kernel + params
* Methods: `createResources()`, `resize()`, `render()`, `transitionResource()` (public static)
* UI params: `enabled`, `radius`, `bias`, `kernelSize` (SSAO menu)

**ShadowRenderer** (`shadow.ixx` + `shadow.cpp`) — shadow map subsystem:

* `shadowMap` (`R32_TYPELESS`/`D32_FLOAT`, 2048²), `dsvHeap`, `pso`
* Public config: `enabled`, `bias`, `rasterDepthBias`, `rasterSlopeBias`, `rasterBiasClamp`, `lightDistance`, `orthoSize`, `nearPlane`, `farPlane`; `static constexpr mapSize = 2048`
* Methods: `createResources()` (creates texture + DSV + SRV into sceneSrvHeap + PSO), `reloadPSO()`, `computeLightViewProj(vec3 dirLightDir)`, `render()`
* Shadow SRV placed at `sceneSrvHeap[nBuffers]` (slot 3)

**OutlineRenderer** (`outline.ixx` + `outline.cpp`) — stencil silhouette subsystem:

* Owns `pso` (stencil NOT_EQUAL, no depth write, cull none)
* Methods: `createResources()`, `reloadPSO()`, `render()` (draws outline for hovered/selected entities)

**GizmoState** (`gizmo.ixx` + `gizmo.cpp`) — translation gizmo subsystem:

* 3 arrow ECS entities (`Transform + MeshRef + GizmoArrow`) — dynamically generated cylinder+cone mesh along +Y, rotated per axis
* Arrow entities are filtered from shadow/cubemap/normal/scene passes via `Scene::isGizmoDraw`; rendered in dedicated Gizmo Pass (depth-cleared, on top of scene)
* Participates in ID pass for click detection; drag projects mouse delta onto screen-space axis direction
* Materials: emissive R/G/B (emissiveStrength=5) so arrows glow regardless of lighting
* Constant screen-space size: `gizmoScale = distance(entity, camera) * 0.1`
* Hidden via `scale(0)` transform when no entity is selected
* Methods: `init()`, `update()`, `isGizmoEntity()`, `isDragging()`

**ImGuiLayer** (`imgui_layer.ixx` + `imgui_layer.cpp`) — owns ImGui init/teardown:

* SRV descriptor heap for ImGui
* Fonts: Roboto-Medium.ttf (text) + MaterialIcons-Regular.ttf (icons, merged via `MergeMode`). Icon glyphs in PUA range (U+E000–U+F8FF).
* Methods: `init()`, `shutdown()`, `styleColorsDracula()`
* Note: `renderImGui()` is in `src/application/ui.cpp` (app-specific UI)

### Application class (split across 5 files in `src/application/`)

* `application.cpp` — constructor, destructor, `update()`, helpers
* `render.cpp` — `render()` — shadow, cubemap, G-buffer, SSAO, scene, gizmo, grid, outline, ID, billboards, bloom, imgui, present
* `ui.cpp` — `renderImGui()` with all ImGui menus, inspector, tooltips
* `setup.cpp` — `loadContent()`, `createScenePSO()`, `createGBufferPSO()`, `createCubemapResources()`, `onResize()`
* `scene.cpp` — `extractSceneData()`, `applySceneData()` — scene file serialization

Thin orchestrator — owns the render loop, swap chain, scene PSO, and input:

* **Swap chain**: triple-buffered, `R8G8B8A8_UNORM`.
* **Root signature**: 8 root params — \[0\] CBV (b0, PerFrameCB), \[1\] CBV (b1, PerPassCB), \[2\] 1 root constant (b2, drawIndex), \[3\] 4 root constants (b3, outline params), \[4\] SRV table (t0, PerObjectData), \[5\] SRV table (t1, shadow map), \[6\] SRV table (t2, cubemap), \[7\] SRV table (t3, SSAO). Static samplers: s0 (shadow comparison PCF), s1 (cubemap linear).
* **Scene PSO**: standard rasterization to HDR RT (`R11G11B10_FLOAT`), stencil write. **G-Buffer PSO** (`gbufferPSO`): vertex_shader.hlsl + gbuffer_ps.hlsl → 4 MRTs (Normal/Albedo/Material/Motion). **Shadow PSO**: owned by `ShadowRenderer`, depth-only, front-face culling, depth bias. **Outline PSO**: owned by `OutlineRenderer`, extrude verts along normal, stencil test NOT_EQUAL.
* **Per-frame data split**: `PerFrameCB` (lights, shadows, fog — ~512 bytes aligned, triple-buffered root CBV at b0), `PerPassCB` (viewProj, prevViewProj, cameraPos — 256-byte slots, array of 16 per frame at b1), `PerObjectData` (model, prevModel, material — structured buffer at t0). `PerPassCB` slots: 0=main, 1=shadow, 2-7=cubemap faces, 8=G-buffer, 9=ID, 10=grid.
* **Vertex format**: `VertexPBR` — position (float3), normal (float3), UV (float2).
* Delegates to subsystems: `scene.*`, `bloom.*`, `imguiLayer.*`, `ssao.*`, `shadow.*`, `outline.*`.
* **Shader hot reload**: polls `.hlsl` timestamps every 0.5s in `update()`, launches DXC as an async subprocess. Compilation is non-blocking — `poll()` checks for process completion each frame. Once bytecode is ready, PSOs are recreated. Scene PSO via `createScenePSO()`, shadow PSO via `shadow.reloadPSO()`, outline PSO via `outline.reloadPSO()`, bloom PSOs via `bloom.reloadPipelines()`. Enabled automatically when `DXC_PATH` and `SHADER_SRC_DIR` are set (CMake provides both). **Robust**: if DXC compilation fails, old bytecode is preserved. If PSO recreation throws, the exception is caught and the previous PSO stays active — only initial load failures are fatal.
* **Animation system**: `update()` runs `scene.animQuery.each<Transform, Animated>()` — orbits entities around the Y axis at individual speeds, applies sinusoidal scale pulse (±15%). Central teapot has no `Animated` component and stays stationary. `scene.instanceAnimQuery.each<InstanceGroup, InstanceAnimation>()` spins each instance group in place by rebuilding transforms each frame from stored base positions/scales.
* **GPU instancing**: `InstanceGroup` component stores N transforms and per-instance material overrides (albedo, roughness, metallic, emissiveStrength). Rendered via one `DrawIndexedInstanced(..., N, ...)` per group. Vertex/outline shaders use `SV_InstanceID` to index into `drawData[drawIndex + instanceID]`. `totalSlots` = regular entity count + sum of all instance group sizes; shadow/cubemap draw data stored at offsets `totalSlots` and `2*totalSlots`.

### Rendering pipeline

The rendering pipeline is now orchestrated via the **Render Graph**. `Application::render()` builds the graph each frame and executes it.

```
update()  →  render()
              └─ RenderGraph::execute()
                  ├─ Shadow pass      (depth-only to 2048² shadow map)
                  ├─ Cubemap pass     (6-face env map, non-reflective only)
                  ├─ G-Buffer pass    (Normal/Albedo/Material/Motion → 4 MRTs, motion vectors)
                  ├─ SSAO pass        (reads GBuffer Normal+depth → R8 ssaoRT)
                  ├─ Scene pass       (HDR RT, samples shadow+cubemap+SSAO)
                  ├─ Grid pass        (infinite Y=0 grid, alpha-blended, depth-tested)
                  ├─ Outline pass     (silhouette for hovered/selected)
                  ├─ ID pass          (R32_UINT RT, entity index per pixel)
                  ├─ Billboards pass  (light sprite rendering)
                  ├─ Gizmo pass       (translation arrows, renders on top)
                  ├─ Bloom pass       (Prefilter → Downsample → Upsample → Composite)
                  ├─ ImGui pass       (UI overlay)
                  └─ Present pass     (Transition backbuffer to PRESENT state)
```

**Resource State Tracking**: The Render Graph automatically generates `ResourceBarrier` calls between passes. For example, it ensures the depth buffer is in `DEPTH_WRITE` for the normal pre-pass, then transitions it to `PIXEL_SHADER_RESOURCE` for SSAO, and back to `DEPTH_WRITE` for the scene pass.

**GPU instancing**: Two draw modes coexist. Regular entities (`MeshRef` component) get one `DrawIndexedInstanced(..., 1, ...)` call each. `InstanceGroup` entities batch N instances of a single mesh into one `DrawIndexedInstanced(..., N, ...)` call. Both write consecutive `SceneConstantBuffer` entries in the structured buffer. Vertex shaders use `drawData[drawIndex + SV_InstanceID]` — backwards-compatible since `SV_InstanceID=0` for non-instanced draws. `drawIndexToEntity` has one entry per instance slot; picking any instance of a group selects the group entity. Instance groups are not `Pickable` and not animated. `DrawCmd` struct (exported from `scene.ixx`) carries `instanceCount` and `baseDrawIndex` for unified draw loop logic across all passes.

**Shadow mapping**: 2048×2048 `R32_TYPELESS`/`D32_FLOAT` depth texture. Orthographic projection from directional light. 3×3 PCF via `SampleCmpLevelZero`. Shadow draw data stored at structured buffer offset `totalSlots` (same model transforms, light viewProj). Shadow PSO uses front-face culling + depth bias to reduce peter-panning/acne.

**Cubemap reflections**: Dynamic environment cubemap (`R11G11B10_FLOAT`, configurable resolution, default 128). Rendered from the first reflective entity's position. Non-reflective entities are drawn into the cubemap (no recursion); if none exist, the pass falls back to capturing all non-gizmo draws so the cubemap is not empty. Materials with `reflective=true` sample the cubemap via `reflect(-V, N)` in the pixel shader, weighted by Fresnel and inverse roughness. Cubemap draw data stored at structured buffer offset `2*totalSlots`. Resources: `cubemapTexture` (6 array slices), `cubemapDepth`, `cubemapRtvHeap` (6 RTVs), `cubemapDsvHeap` (6 DSVs), SRV at `sceneSrvHeap[nBuffers+1]`. `createCubemapResources()` recreates when resolution changes.

**SSAO**: Screen-Space Ambient Occlusion computed in two passes. Normal pre-pass renders world-space normals to `R8G8B8A8_UNORM` normalRT; main depth buffer transitions DEPTH_WRITE → PIXEL_SHADER_RESOURCE for SSAO read, then back. SSAO shader reconstructs view-space position from depth + invProj, transforms normals to view space, builds TBN from tiled 4×4 random noise, samples 32-point hemisphere kernel, range-checks occlusion. Blur pass applies 3×3 box filter. Output (`ssaoBlurRT`, `R8_UNORM`) SRV at `sceneSrvHeap[nBuffers+2]` (root param \[5\], t3 in pixel shader). Scene PSO uses `normalPSO` (vertex_shader.hlsl + normal_ps.hlsl → normalRT). `SsaoRenderer::enabled` skips all passes when false.

**Bloom**: 5-mip chain — prefilter (Karis average, soft threshold), 4× downsample, 4× upsample (tent filter, additive blend).

**Tonemappers** (selectable in UI): ACES Filmic, AgX, AgX Punchy, Gran Turismo / Uchimura, PBR Neutral.

**Rayleigh sky + clouds**: Shared implementation in `src/sky.hlsli` (included by `bloom_composite_ps.hlsl` and `pixel_shader.hlsl`). `rayleighSky(viewDir, sunDir, time)` computes Rayleigh scattering + procedural FBM clouds (hash-based value noise, 5 octaves, spherical projection, time-animated drift). HDR RT is cleared to black so the composite pass detects background via `sceneLum < 0.001`. `SkyParams` includes `time` (from `lightTime`); `PerFrameCB` also has `time` for reflection sky. DXC includes are enabled via `-I src/` in both CMake and shader hot reload. Cubemap reflection fallback in `pixel_shader.hlsl` uses the same sky function when envMap sample is near-black.

**Infinite grid**: Rendered via `grid_vs.hlsl` + `grid_ps.hlsl` with its own root signature (`gridRootSig`) and PSO (`gridPSO`). Vertex shader generates a fullscreen triangle, unprojects near/far planes to world space via `InvViewProj`. Pixel shader ray-intersects the Y=0 plane, draws minor and major lines with `fwidth`-based anti-aliasing. Major cell size and subdivision count are runtime-configurable from the Display menu and passed via perPass CB slot 10 `GridCB` (`ViewProj`, `InvViewProj`, `CameraPos`, `GridParams`). X axis highlighted blue, Z axis red. Alpha-blended with distance fade (80m). Depth-tested against scene geometry (read-only depth). Toggled via `showGrid` (Display menu, saved in scene files).

**Ocean fog**: Height-based fog in `src/shaders/pixel_shader.hlsl`. Thickens exponentially below `FogStartY` (water surface). Fog color darkens with depth — near-surface uses `FogColor`, deep areas fade toward black. Also blends distance fog for depth cueing. Parameters (`FogStartY`, `FogDensity`, `FogColor`) stored in `SceneConstantBuffer` and editable in UI.

### PBR / BSRDF shader (`src/shaders/pixel_shader.hlsl`)

Cook-Torrance BRDF:

* **NDF**: GGX / Trowbridge-Reitz
* **Geometry**: Smith + Schlick-GGX
* **Fresnel**: Schlick approximation
* **Inputs** (from `SceneConstantBuffer`): albedo RGBA, roughness, metallic, reflective flag, emissive color + strength
* Up to 8 animated point lights (queried from ECS `PointLight` component entities via `lightQuery`) with scaled inverse-square attenuation (`1 / max(d² × 0.01, ε)`).
* 1 directional light (shadow-casting) — direction, color, brightness configurable in UI.

### Scene loading

* **Default**: teapot OBJ embedded as Win32 resource (`IDR_TEAPOT_OBJ`/`IDR_TEAPOT_MTL`). `loadTeapot()` now creates a reflective teapot plus a non-reflective companion teapot so cubemap reflections always have environment content.
* **Startup model loading**: all `.glb` files in `resources/models/` are loaded automatically at startup via `MODELS_DIR` (CMake-defined). Spawned entities pick randomly from all loaded mesh refs.
* **GLB/glTF**: tinygltf v2.9.5 via FetchContent. Load from UI "Load GLB" panel (type path, press Load).
  * Supports binary GLB and ASCII glTF.
  * Extracts POSITION, NORMAL, TEXCOORD_0, indices (any component type).
  * Loads PBR metallic-roughness material factors (base color, roughness, metallic, emissive).
  * Traverses node hierarchy with TRS / matrix transforms.
* **Model files**: stored in `resources/models/` (teapot.obj/mtl + GLB primitives).

### Scene file system

JSON scene files (via glaze) store all configurable scene state: camera, bloom, lighting, fog, shadows, cubemap, terrain params, materials, entities, instance groups, and display settings. Entities and instance groups reference meshes by name (resolved against `spawnableMeshNames` on load). `InstanceGroupData` stores per-instance positions, scales, and albedo overrides.

* **CLI**: first positional argument is a scene file path. If omitted, engine loads the path from `config.json`'s `defaultScenePath` (default: `resources/scenes/default.json`). `--dump-config` writes default `config.json` and exits.
* **Scene files**: stored in `resources/scenes/` (`SCENES_DIR` CMake define)
  * `test.json` — test automation scene (WARP, hidden window, screenshot at frame 10, exit)
  * `empty.json` — empty scene with defaults, spawning stopped
  * `default.json` — default startup scene with one reflective teapot when no scene argument is provided
* **Runtime block**: optional section in scene files for automation and startup mode: `useWarp`, `hideWindow`, `screenshotFrame`, `exitAfterScreenshot`, `spawnPerFrame`, `skipImGui`, `singleTeapotMode`
* **UI**: Scene menu has save/load path input + buttons
* **Implementation**: `scene_file.ixx` module wraps `glaze_impl.cpp` (isolated TU for glaze templates). `src/application/scene.cpp` has `applySceneData()`/`extractSceneData()` for converting between `SceneFileData` structs and Application state. Data structs in `include/scene_data.h` reuse engine types directly — `OrbitCamera`, `TerrainParams`, `Material`, `Animated`, `PointLight` — no duplicate "data" structs.

### Configuration system

`config.json` stores global engine defaults (window size, graphics toggles, display settings, spawning params, default scene path). Struct in `include/config_data.h`, module in `config.ixx` + `config.cpp`.

* **Merge semantics**: on startup, `mergeConfig()` reads existing `config.json` (with `error_on_unknown_keys = false` so obsolete keys are silently skipped), then writes back. Missing keys get defaults, obsolete keys are dropped, existing values are preserved.
* **`--dump-config`**: writes a fresh `config.json` with all defaults and exits (no window/GPU needed).
* **Load order**: config applied first via `app.applyConfig(config)`, then scene file via `app.applySceneData(sceneData)`. Scene values override config for shared settings (bloom, shadows, etc.).
* **Glaze integration**: `readConfigJson`/`writeConfigJson` in `glaze_impl.cpp`. `ConfigData` is a plain aggregate — no `glz::meta` specialization needed.
* **Hotkeys**: `ConfigData::hotkeys` maps action names to lists of key names (e.g. `"toggleFullscreen": ["F11"]`). `HotkeyBindings` struct (in `input.ixx`) manages edge-triggered key detection via `GetAsyncKeyState` and previous-frame state tracking. Default bindings: F11=fullscreen, Delete=delete entity, Escape=deselect. Shortcut labels shown in UI buttons/tooltips. New actions: add to `EditorAction` enum, add default in `HotkeyBindings::setDefaults()`, handle in `Application::update()`.
* **Icons**: `ConfigData::icons` maps UI element keys (e.g. `"menu.Display"`, `"action.Delete"`, `"window.Metrics"`) to Material Icon names (e.g. `"desktop_windows"`, `"delete"`, `"bar_chart"`). Icon names resolve to codepoints via `iconCodepointFromName()` in `include/icons.h`. Font loaded in `ImGuiLayer::init()` as merged Material Icons font. Application caches icon UTF-8 strings in `iconCache` (rebuilt in `applyConfig()`). `iconLabel(key, label)` helper returns icon-prefixed label for ImGui widgets. 34 icons available, 64x64 PNGs in `resources/icons/`. New icons: add codepoint to `IconCP` namespace + lookup map in `icons.h`, add default mapping in `ConfigData::icons`.

### Lua scripting system

Lua 5.4 (FetchContent, compiled as static C lib) provides entity scripting and editor action automation. Module in `lua_scripting.ixx` + `lua_scripting.cpp`, bindings in `lua_scripting_impl.cpp` (isolated TU, includes `lua.h` directly).

* **Scripted component** (`include/lua_script_types.h`): `scriptPath` (relative to `SCRIPTS_DIR`), `luaRef` (Lua registry reference to script table). Scripts return a table with optional `on_create(id)`, `on_update(id, dt)`, `on_destroy(id)` callbacks.
* **Engine API**: ~40 functions under global `engine` table — entity CRUD, transform get/set, material manipulation, component add/remove (Pickable, Animated, PointLight), entity queries, mesh spawning, editor action execution, logging, frame info (dt/time/frameCount).
* **Execution model**: `LuaScripting::updateScriptedEntities()` called each frame in `Application::update()` after deferred ECS mutations. Iterates `scriptQuery` — loads unloaded scripts, calls `on_update`. Entity destroys from scripts are deferred and processed within the same call.
* **Action bindings**: `resources/scripts/actions.json` maps action names to script paths. One-shot scripts executed via `engine.execute_action("name")` or from Scripts menu in UI.
* **Hot reload**: Polls script file timestamps every ~1s. Changed scripts are reloaded — old ref unreffed, `on_destroy`/`on_create` called.
* **Engine context**: `Scene*`, materials, mesh refs stored as lightuserdata in Lua registry. Plain TU (`lua_scripting_impl.cpp`) includes `flecs.h` + engine headers directly.
* **Error handling**: All Lua calls via `lua_pcall`. Errors logged via spdlog, scripts stay loaded for hot-reload fix.
* **UI**: Scripts menu (action bindings + one-off script execution), Entity Inspector Scripted tab (detach), Attach Script input.

### ImGui UI panels

All menu bar menus, action buttons, and window titlebars display Material Icons via the merged icon font. Icons are config-driven — see Configuration system → Icons. Organized into a standard editor structure:

* **File**: scene path (Load/Save), GLB path (Load/Reset), scene title/description.
* **Edit**:
  * **Create**: mesh selector (from loaded mesh refs), material selector, position/scale, animated toggle with speed/radius. Spawns entity on click.
  * **Material**: global material editor (albedo, roughness, metallic, emissive color + strength, reflective checkbox). Material selector when GLB has multiple.
* **View**:
  * **Display**: vsync toggle, grid toggle (size/subdivs), fullscreen toggle, tearing status, runtime mode.
  * **Camera**: FOV, near/far planes, orbit radius, yaw, pitch.
  * **Metrics Panel**: toggle for the floating Metrics window.
* **Render**:
  * **Tonemap**: tonemapper selection (ACES Filmic, AgX, AgX Punchy, Gran Turismo, PBR Neutral).
  * **Shadows**: enable/disable, bias, raster depth/slope/clamp bias, light distance, ortho size, near/far.
  * **Reflections**: cubemap enable/disable, resolution slider (32–512, recreates resources), near/far planes.
  * **Bloom**: threshold, intensity sliders.
  * **SSAO**: enable/disable, radius, bias, kernel size.
* **World**:
  * **Environment**: background color, directional light (direction/color/brightness), ambient brightness, height fog (startY, density, color).
  * **Lights**: billboard toggle/size, point-light brightness, per-light controls for all `PointLight` entities.
  * **Animation**: entity animation toggle, light animation speed/time scrub.
  * **Spawning**: manual pause/resume, auto-stop toggle, frame-ms threshold, spawn batch size, reset perf gate.
* **Tools**:
  * **Scripts**: action bindings from `actions.json` with Execute buttons, one-off script path input + Run button.

* **Metrics** (floating window, toggled via View menu): build mode (Debug/Release), FPS + frame ms, FPS graph (last 5s, collapsible), draw calls, objects, vertices, ECS entity/component counts (MeshRef, Animated, InstanceGroup, PointLight, Pickable), subsystem status (shadow/cubemap/SSAO).
* **Entity Inspector**: shown when entity is selected. Tabbed view of Transform (editable position), MeshRef (material properties, albedo override), Animated (speed, orbit, scale), Pickable (remove toggle), Scripted (script path, detach). Attach Script input, Add Animated/Pickable buttons, Delete button (red). Hover tooltip shows entity ID + material on mouseover.
* **Title/Description overlay**: when `sceneTitle` / `sceneDescription` are set, drawn directly to foreground via `ImGui::GetForegroundDrawList()` in the bottom-right corner. Title at 1.4× font size, description at normal size, both with 1-pixel drop shadow.


### Testing

* **Unit tests** (`tests/unit_tests.cpp`): doctest-based, covers math utilities, terrain generation, etc.
* **Lua scripting tests** (`tests/lua_scripting_tests.cpp`): 24 test cases (147 assertions) covering all ~40 `engine.*` Lua API functions. Uses a `LuaTestFixture` with a standalone flecs world, materials, mesh refs, and Lua state — no GPU or window needed. Links `lua_lib`, `flecs::flecs_static`, `spdlog::spdlog`, `doctest::doctest`.
* **CTest**: `doctest_discover_tests()` auto-registers all test cases. Run via `ctest --test-dir build -C Debug --output-on-failure`.
* **Integration test**: `./build/Debug/main.exe resources/scenes/test.json` loads a WARP-adapter scene, renders 10 frames, writes `screenshot.png`, and exits. Used for visual regression checks.

### Example Lua scripts (`resources/scripts/`)

* **Per-entity** (attach via `Scripted` component): `orbit.lua` (Y-axis orbit), `bounce.lua` (vertical bounce with squash/stretch), `pulse_emissive.lua` (pulsing glow)
* **One-shot actions** (run from Scripts menu or `engine.execute_action()`): `spawn_grid.lua` (5x5 entity grid), `randomize_colors.lua` (random albedo on all MeshRef entities), `delete_all.lua` (destroy all MeshRef entities)
* **Action bindings**: `resources/scripts/actions.json` maps action names to script paths


---

## Dependencies

| Library | Source | Notes |
|----|----|----|
| directxtk12, directxmath, spdlog | vcpkg (x64-windows-static) |    |
| directx-headers | vcpkg (transitive via directxtk12) | d3dx12 helpers |
| gainput | FetchContent (git hash 2be0a50) | Input |
| imgui v1.92.6 | FetchContent | Win32 + DX12 backend |
| tinyobjloader | FetchContent (git hash afdd3fa) | OBJ loading |
| tinygltf v2.9.5 | FetchContent | GLB/glTF loading |
| PerlinNoise v3.0.0 | FetchContent | Terrain heightmap |
| glaze v5.2.1 | FetchContent | JSON serialization |
| Lua 5.4.7 | FetchContent | Scripting engine (compiled as static C lib) |
| doctest v2.4.11 | FetchContent | Unit testing + CTest discovery |
| Material Icons | google/material-design-icons | Icon font for UI menus (`resources/fonts/`) |
| Tracy v0.13.1 | FetchContent | CPU+GPU profiling (on-demand) |


---

## Code Style

* clang-format: Chromium base, 4-space indent, 100-col limit.
* clang-tidy: bugprone, modernize, performance, readability checks.
* Windows subsystem (no console) — use `spdlog` for all logging.
* DX12 debug layer enabled in Debug builds (only when debugger is attached — see Key Patterns).


---

## Profiling

Tracy Profiler v0.11.1 is integrated for CPU and GPU instrumentation. `TRACY_ON_DEMAND` is set so there is no overhead when the viewer is not connected.

* **Header**: `include/profiling.h` — include ONLY from `.cpp` files, never from `.ixx` modules
* **GPU context**: `g_tracyD3d12Ctx` (file-static in `src/application/application.cpp`, `extern`'d in `render.cpp` and `setup.cpp`); created in `loadContent()`, destroyed in `~Application()` after `flush()`
* **CPU zones**: `PROFILE_ZONE()` / `PROFILE_ZONE_NAMED(name)` — in `update()` and each render pass
* **GPU zones**: `PROFILE_GPU_ZONE(ctx, cmdList.Get(), "name")` — Shadow, Normal Pre-pass+SSAO, Scene, Bloom
* **Frame boundary**: `PROFILE_FRAME_MARK` after `Present` at end of `render()`
* **Collect/NewFrame**: called at the top of `render()`; frame waits are now conditional per back buffer (no unconditional end-of-frame wait)
* **Viewer**: download Tracy v0.13.1 from GitHub releases; connect to the running engine on localhost

## Key Patterns / Pitfalls

### GPU upload buffer lifetime

Intermediate upload heaps from `UpdateSubresources` **must** stay alive until after `cmdQueue.waitForFenceVal()`. Pass a `vector<ComPtr<ID3D12Resource>>& temps` to `uploadMesh` and clear it only after the GPU wait.

Current pattern: uploads are tracked in `Scene::pendingUploads` as fence-keyed batches (`trackUploadBatch` / `retireCompletedUploads`) and retired in `Application::update()` once `CommandQueue::completedFenceValue()` has advanced.

### Frame pacing and fence waits

`Application::render()` now waits at frame start only when the current back buffer's fence is still in flight (`frameFenceValues[curBackBufIdx]`). This removes the unconditional end-of-frame CPU stall while still protecting swap-chain resource reuse.

### Picker readback ring buffering

`ObjectPicker` uses a 3-slot readback ring (`readbackSlots`) with per-slot fence values. `copyPickedPixel()` writes into the next slot, `setPendingReadbackFence()` tags the submitted slot, and `readPickResult()` consumes the newest completed slot. This avoids single-buffer contention when frames overlap.

### RenderGraph external resource lifetime

`RenderGraph::reset()` clears `resources` and `externalResources` each frame. Imported textures (especially the current swap-chain back buffer) must be re-imported every frame to avoid stale handles/state tracking.

### ResizeBuffers requires render-graph reset first

`IDXGISwapChain::ResizeBuffers()` can fail with `DXGI_ERROR_INVALID_CALL` if old back buffers are still referenced indirectly. Before resetting `backBuffers[]` in `Application::onResize()`, call `renderGraph.reset()` (after GPU flush) so imported external `ComPtr` refs are released.

### Headless runtime picking

When `runtime.hideWindow` is true (automation scenes), the ID picking pass is skipped in `Application::render()` to avoid unnecessary offscreen picking work.

### SceneConstantBuffer layout

Must match `SceneCB` in both HLSL shaders exactly. Current fields: `model`, `viewProj`, `cameraPos`, `ambientColor`, `lightPos[8]`, `lightColor[8]`, `albedo`, roughness/metallic/emissiveStrength/reflective, `emissive`, `dirLightDir`, `dirLightColor`, `lightViewProj`, shadowBias/shadowMapTexelSize/fogStartY/fogDensity, `fogColor`.

### XMMATRIX alignment

`SceneConstantBuffer` contains `XMMATRIX` members — declare on the stack with `alignas(16)`:

```cpp
alignas(16) SceneConstantBuffer scb = {};
```

### Error reporting helpers

Use `chkDX(...)` for HRESULT-returning calls and `throwLastWin32Error(...)` for Win32 APIs that signal failure via `GetLastError()`. `chkDX` now throws `std::runtime_error` with HRESULT hex + decoded message + source location to make crash logs actionable.

### No GPU ops inside renderImGui

`renderImGui` is called mid-frame with an open command list. Never call `clearScene()`, `flush()`, or `loadGltf()` directly inside it — use the deferred flags `pendingGltfPath` / `pendingResetToTeapot`, processed at the start of `update()`. Similarly, never mutate ECS entities (set/add components, destruct) inside `renderImGui` — use `pendingAddAnimated`, `pendingAddPickable`, `pendingDeleteSelected`, `pendingCreateEntity` flags, processed in `update()` before any ECS queries run.

### Tracy D3D12 callstack depth gotcha

With Tracy v0.13.1, `TRACY_CALLSTACK` is defined as `0` by default in `Tracy.hpp`, but `TracyD3D12NamedZone` still routes to the callstack overload when the macro exists. This can assert in `TracyCallstack.hpp` (`depth >= 1 && depth < 63`). Keep GPU profiling wrapper on `TracyD3D12NamedZoneS(..., depth=1, ...)` (see `include/profiling.h`) unless callstack configuration changes.

### Fullscreen toggles are deferred

Fullscreen transitions can generate synchronous `WM_SIZE` events that trigger `onResize()` and swap chain `ResizeBuffers()`. Queue UI fullscreen requests via `pendingFullscreenChange` / `pendingFullscreenValue` and apply them at the start of `update()`. Resources recreated in `onResize()` (HDR RT, depth buffer, bloom mips) must be created in their render-graph-expected initial states (e.g. HDR RT in `PIXEL_SHADER_RESOURCE`, not `RENDER_TARGET`) to avoid state mismatch on the first frame after resize.

### ImGui input capture

Camera rotation, zoom, and entity picking are gated by `ImGui::GetIO().WantCaptureMouse` in `update()` to prevent input passthrough when interacting with UI panels.

### PointLight entities survive clearScene()

`clearScene()` removes entities via `delete_with<MeshRef>()` and `delete_with<InstanceGroup>()` — only entities with those components are deleted. `PointLight` entities have no `MeshRef`, so they persist across scene resets. Light data is restored from JSON via `lightQuery.each(...)` in `applySceneData()`.

### Gizmo entities must be recreated after scene clears

`clearScene()` deletes gizmo arrows (`GizmoArrow + MeshRef`) along with normal mesh entities. Any flow that clears/reloads scene content (`pendingResetToTeapot`, non-append `loadGltf`, or `applySceneData()` single-teapot reset) must call `gizmo.init(scene, device.Get(), cmdQueue)` afterward, otherwise `GizmoState` retains stale entity handles and Flecs can assert on first update.

### TerrainEntity tag and positionY

The terrain entity is tagged with `TerrainEntity` at creation (in `loadContent()`). `applySceneData()` queries for `TerrainEntity` entities to update their `Transform.world = translate(0, positionY, 0)` from the scene file. This must be guarded by `contentLoaded` since terrain is created in the Application constructor (before the caller can invoke `applySceneData`).

### tinygltf in a separate TU

`TINYGLTF_IMPLEMENTATION` and `STB_IMAGE_IMPLEMENTATION` must be in `src/gltf_impl.cpp` (not in `application.cpp`) to avoid `stb_image` / `Windows.h` macro conflicts in the module implementation file.

### glTF matrix convention

glTF matrices are column-major. When loading into `XMMATRIX` (row-major), transpose: put glTF column *i* as XMMATRIX row *i*. For TRS nodes use `S * R * T` order in DirectXMath.

### D3D12 debug layer gated on debugger

`enableDebugging()` in `window.cpp` checks `IsDebuggerPresent()` before enabling the D3D12 debug layer and DXGI debug factory flag. Without a debugger, debug layer breakpoints (STATUS_BREAKPOINT 0x80000003) crash the process.

### Window callback gating (inMessageLoop)

`Window::inMessageLoop` (default `false`) gates `WM_SIZE` and `WM_PAINT` callbacks in `WndProc`. Set to `true` in `main.cpp` before `ShowWindow` so the resize triggered by `ShowWindow` is handled, but premature callbacks during window creation are suppressed. Never call `UpdateWindow()` — it triggers synchronous `WM_PAINT` → render before the message loop starts.

### SEH exception filter

`main.cpp` installs `SetUnhandledExceptionFilter(sehFilter)` to log unhandled SEH exceptions (code + address) via spdlog before termination. Cannot use `__try/__except` in the same function as C++ `try/catch`.

### startFullscreen config

`ConfigData::startFullscreen` (default `false`) replaces any hardcoded `setFullscreen(true)` in the constructor. Applied in `applyConfig()` via `pendingFullscreenChange`/`pendingFullscreenValue` to go through the deferred fullscreen path.

### Depth buffer must be created in loadContent

`resizeDepthBuffer()` is called from `onResize()`, which only fires when `isInitialized == true` and the window receives `WM_SIZE`. For hidden-window test scenes (`runtime.hideWindow = true`), `ShowWindow` is skipped so `WM_SIZE` never fires — `depthBuffer` stays null and all passes that import it (G-Buffer, Scene, Gizmo) will crash. **Fix**: call `resizeDepthBuffer(clientWidth, clientHeight)` at the end of `loadContent()`, after the DSV heap is created. `onResize()` will call it again when the window is actually shown, which is safe (it just reallocates).

### LOD selection in populateDrawCommands

`LodMesh` component is checked inline inside the `drawQuery.each()` lambda using `e.try_get<LodMesh>()`. The active `MeshRef` is overridden based on squared distance to camera. LOD levels are sorted ascending by `distanceThreshold`; the first level whose threshold exceeds the camera distance is selected, otherwise the last (most distant) level is used. Frustum culling via `DirectX::BoundingFrustum::Contains` uses the entity's `BoundingVolume::sphere` when present; without it, culling is skipped for that entity.


---

## Debugging Notes

### No-arg launch Flecs invalid-entity assert (2026-04-15)

**Symptom**: Running `main.exe` without arguments reached startup logs and then aborted with:

`fatal: flecs_cpp.c: assert(ecs_is_alive(world, entity))`

The integration scene (`resources/scenes/test.json`) still completed successfully, which pointed to a default-scene application path issue rather than a global render failure.

**Root cause**:

- `runtime.singleTeapotMode` scene application clears scene content.
- `Scene::clearScene()` deletes `MeshRef` entities, which includes gizmo arrows (`GizmoArrow + MeshRef`).
- `GizmoState` still held stale arrow entity handles and touched them during the next update, triggering Flecs alive checks.

**Fix pattern**:

- Reinitialize gizmo immediately after any clear/reload path that can destroy gizmo arrows:
  - deferred reset-to-teapot
  - non-append GLTF load
  - scene-file load path after apply
  - single-teapot branch in `applySceneData()`

**Reusable tips for bugs like this**:

1. When a crash is scene-dependent, compare no-arg startup with `resources/scenes/test.json` first.
2. Audit every path that calls `clearScene()` and list subsystems that cache entity IDs/handles.
3. Treat cached ECS handles as invalid across clears unless explicitly rebuilt.
4. Prefer resetting/reinitializing subsystem state over adding ad hoc `is_alive()` guards everywhere.
5. Verify with both interactive startup and headless/integration flow so regressions are caught early.

### Default scene launch crash (2026-04-14)

**Symptom**: Running `main.exe` without arguments (loading `default.json`) crashed immediately. The test scene (`test.json`, WARP + hidden window) worked fine.

**Debugging process**:

1. **Initial triage**: Added SEH exception filter (`SetUnhandledExceptionFilter`) to `main.cpp` to capture crash details before termination. First crash logged: `STATUS_BREAKPOINT (0x80000003)` in the D3D12 runtime.

2. **D3D12 debug layer**: The debug layer was unconditionally enabled in Debug builds. Without a debugger attached, debug layer breakpoints (`int 3`) become unhandled exceptions → crash. **Fix**: Gated `enableDebugging()` on `IsDebuggerPresent()` in `window.cpp`. Also gated `DXGI_CREATE_FACTORY_DEBUG` flag on the same check.

3. **Second crash**: After fixing the debug layer, a new crash appeared — access violation during the first `render()` call. The call stack showed it was triggered by a `WM_PAINT` handler running before the message loop started.

4. **Premature WM_SIZE / WM_PAINT**: `ShowWindow(SW_SHOW)` sends `WM_SIZE` synchronously, which triggered `onResize()` → swap chain `ResizeBuffers()` during window initialization. `UpdateWindow()` then sent synchronous `WM_PAINT` → `render()` before the Application was fully initialized. **Fix**: Added `Window::inMessageLoop` flag (default `false`), checked in `WndProc` before dispatching `WM_SIZE`/`WM_PAINT`. Set to `true` in `main.cpp` just before `ShowWindow`. Removed `UpdateWindow()` entirely.

5. **Hardcoded fullscreen**: The Application constructor called `setFullscreen(true)`, which generated synchronous `WM_SIZE` events during construction. **Fix**: Replaced with `ConfigData::startFullscreen` (default `false`), applied via the deferred fullscreen mechanism in `applyConfig()`.

6. **Environment-specific residual**: After all code fixes, a crash persisted on the development machine (Parsec Virtual Display Adapter). The access violation (0xC0000005) occurred inside DX12 rendering to a visible window with the virtual display driver. The WARP adapter + hidden window path continued to work. This was confirmed as an environment issue, not a code bug.

**Key takeaway**: Multiple independent issues compounded — each fix exposed the next crash. The SEH filter was essential for capturing crash codes without a debugger. The `inMessageLoop` pattern prevents a class of init-order bugs where Win32 message dispatch runs callbacks before the application is ready.