# Architecture

From-scratch DirectX 12 renderer. C++23 modules, Clang, Windows-only.

## Module files (`src/modules/*.ixx`)

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

## Module conventions

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

## Graphics abstraction (`gfx::`)

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
- P2 ✅ (lighter variant) — All scene SRVs moved from `scene.sceneSrvHeap` into the gfx device's single bindless heap. Descriptor table ranges now `DESCRIPTORS_VOLATILE`. New `IDevice` API: `srvGpuDescriptorHandle(index)`, `createTypedSrv(handle, format)`, `srvHeapNative()`.
- P3 ✅ — `rg::RenderGraph` callbacks take `gfx::ICommandList&`. `importTexture` takes `gfx::TextureHandle` + `gfx::ResourceState`. `RenderGraph` ctor takes `gfx::IDevice&`.
- P4 ✅ — `GBuffer::createResources/resize/transition` take `gfx::IDevice&` / `gfx::ICommandList&`.
- P5 ✅ — `ShadowRenderer::createResources/reloadPSO/render` take gfx types.
- P6 ✅ — `SsaoRenderer::createResources/resize/render/transitionResource` take gfx types.
- P7 ✅ — `BloomRenderer::createResources/resize/render/reloadPipelines` take gfx types. `hdrRT` + `bloomMips[]` are `gfx::TextureHandle`.
- P8 ✅ — `OutlineRenderer::createResources/reloadPSO/render` take gfx types.
- P9 ✅ — `ObjectPicker::createResources/resize/copyPickedPixel` take gfx types.
- P10 ✅ — `BillboardRenderer::init/render` take gfx types. `quadVertexBuffer`+`instanceBuffer` are `gfx::BufferHandle`. `ImGuiLayer::init` takes `gfx::IDevice&`.
- P11 ✅ — `GizmoState::init` takes `gfx::IDevice&`.
- P12 ✅ — `Scene::createMegaBuffers/createDrawDataBuffers/appendToMegaBuffers/loadTeapot/loadGltf` all take `gfx::IDevice&`. Mega VB/IB and all buffers are `gfx::BufferHandle`.
- P13 ✅ — Application's scene/gbuffer/grid PSOs are `gfx::PipelineHandle`. VS/PS bytecodes go through `gfx::ShaderHandle`. `depthBuffer`, cubemap color + depth, back buffers are `gfx::TextureHandle`. **Application-owned descriptor heaps (`rtvHeap`, `dsvHeap`, `cubemapRtvHeap`, `cubemapDsvHeap`) fully removed** — gfx backend auto-creates RTVs/DSVs in private CPU-only heaps at texture creation time.
- P13 (deferred) — `gridRootSig` ComPtr (grid-specific layout). BLAS/TLAS resources capability-gated. `spriteTexture` stays ComPtr (no `adoptTexture` in gfx yet).
- P14 ✅ — Shader-visible SRV heaps removed from Bloom, SSAO, and Billboard. All three now use the global gfx bindless heap. New `gfx::VertexBufferView` / `gfx::IndexBufferView` POD types replace D3D12 equivalents in all exported module interfaces. **D3D12 types purged from all exported `.ixx` interfaces:** `D3D12_VIEWPORT`/`D3D12_RECT` → `gfx::Viewport`/`gfx::ScissorRect`; `D3D12_SHADER_BYTECODE` → `gfx::ShaderBytecode`; descriptor handles → `uint64_t`. `ID3D12RootSignature*` params removed from `shadow.ixx`, `outline.ixx`, `object_picking.ixx` — subsystems call `dev.bindlessRootSigNative()` internally. Application's legacy `rootSignature` ComPtr removed; all render passes use bindless root sig. All `#ifdef USE_BINDLESS`/`#else` dead branches removed from `render.cpp`, `shadow.cpp`, `outline.cpp`, `object_picking.cpp`.

**`gfx::Format`** covers RGB32Float, D16Unorm, D24UnormS8Uint, D32Float, D32FloatS8X24Uint, and typeless variants R32Typeless / R32G8X24Typeless / R32FloatX8X24Typeless. `IDevice::createExternalSrv(void* nativeResource, Format, mipLevels, isCubemap)` registers an externally-owned resource in the bindless heap. **`gfx::TextureDesc::viewFormat`** specifies a typed format for the optimized clear value when the resource format is typeless.

**RTV/DSV auto-management**: D3D12 backend owns private CPU-only `rtvHeap_` (512 slots) and `dsvHeap_` (256 slots). `createTexture` automatically allocates slots when `TextureUsage::RenderTarget` or `TextureUsage::DepthStencil` is set. Callers retrieve via `IDevice::rtvHandle(handle, arraySlice)` / `IDevice::dsvHandle(handle, arraySlice)` returning `uint64_t`.

**`gfx::` Bindless model**: single global SRV/UAV heap (default 65k descriptors) and sampler heap. Bindless root signature: `[32 root constants b0][CBV b1][CBV b2][SRV/UAV descriptor table][sampler descriptor table]`. Enabled by default via `USE_BINDLESS=ON`.

**What still leaks D3D12 in subsystems (private fields, blocked on deeper rewrites):**
- `ComPtr<ID3D12Resource>` for BLAS/TLAS (RT-only) and `spriteTexture` (WIC-loaded).
- `ComPtr<ID3D12DescriptorHeap>` in `ImGuiLayer::srvHeap` and `ReStirRenderer::uavHeap`.
- `ComPtr<ID3D12RootSignature>` and `ComPtr<ID3D12PipelineState>` in Bloom, SSAO, Billboard, Shadow, Outline, ObjectPicker, ReStir (all private). Application keeps `gridRootSig` (grid-specific layout).
- `ID3D12CommandQueue*` in `ImGuiLayer::init` — required by `imgui_impl_dx12`.
- `#ifdef USE_BINDLESS` guards remain in `billboard.cpp`, `bloom.cpp`, `ssao.cpp` (own root sigs) and HLSL shaders (compile-time define).

**ECS Update systems**: Native flecs systems in `Scene::setupSystems()` driven by `scene.progress(dt)`:
- `StorePrevTransforms` / `StorePrevInstanceTransforms`: motion vector data
- `EnsurePrevTransform` / `EnsurePrevInstanceTransform`: component initialization
- `AnimateOrbitingEntities`: rotation + scale pulse
- `AnimateInstancedGroups`: group member spin

**Capability gating**: `caps.raytracing`/`caps.meshShaders` queried at device init. RT calls throw on unsupported devices.

**Escape hatch**: `IDevice::nativeHandle()` returns `ID3D12Device2*`. To be removed once subsystem migration completes.
