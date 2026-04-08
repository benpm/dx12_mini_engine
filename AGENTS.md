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

# Unit tests (doctest + CTest)
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
| `window.ixx` | Singleton HWND + D3D12Device2 creation, adapter selection, callback-based render loop |
| `application.ixx` | Main Application class — orchestrates subsystems, render loop, input, UI |
| `scene.ixx` | `Scene` class — ECS world, mega-buffers, draw-data, materials, mesh loading |
| `bloom.ixx` | `BloomRenderer` class — HDR RT, bloom mip chain, root sig, PSOs |
| `imgui_layer.ixx` | `ImGuiLayer` class — descriptor heap, init/shutdown, Dracula style |
| `command_queue.ixx` | ID3D12CommandQueue + fence sync + command allocator pooling |
| `camera.ixx` | Re-exports Camera + OrbitCamera from `include/camera_types.h` |
| `input.ixx` | Button/Key enums, gainput integration (uses `module :private;` for global) |
| `ecs_components.ixx` | Re-exports ECS components from `include/ecs_types.h` (Transform, Animated, Pickable, MeshRef, InstanceGroup, InstanceAnimation, TerrainEntity, PointLight) |
| `shader_hotreload.ixx` | `ShaderCompiler` class — watches HLSL files, recompiles via DXC at runtime |
| `billboard.ixx` | `BillboardRenderer` class — point light sprite rendering |
| `object_picking.ixx` | `ObjectPicker` class — ID render pass, readback for entity picking |
| `terrain.ixx` | `TerrainParams` struct + `generateTerrain()` — Perlin noise heightmap mesh |
| `scene_file.ixx` | Scene file serialization — load/save JSON scene files via glaze |
| `ssao.ixx` | `SsaoRenderer` class — normal pre-pass RT, SSAO compute + blur passes |
| `shadow.ixx` | `ShadowRenderer` class — shadow map texture, DSV, PSO, render + reloadPSO |
| `outline.ixx` | `OutlineRenderer` class — stencil-based silhouette PSO + render |
| `render_graph.ixx` | `rg::RenderGraph` class — pass orchestration and automated resource barriers |
| `logging.ixx` | spdlog setup with custom error sink |

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
* **Application public API is minimal** — only `update()`, `render()`, `runtimeConfig`, `cam`, `inputMap`, `keyboardID`, `applySceneData()`, `extractSceneData()` are public

### Subsystem architecture

Application owns subsystem instances: `Scene scene`, `BloomRenderer bloom`, `ImGuiLayer imguiLayer`, `ShadowRenderer shadow`, `OutlineRenderer outline`, `SsaoRenderer ssao`, `ObjectPicker picker`, `BillboardRenderer billboards`, `rg::RenderGraph renderGraph`.

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

* R32_UINT render target (viewport-sized), own depth buffer
* Reuses scene vertex shader + ID pixel shader (`id_ps.hlsl`) that outputs draw index
* PSO created from scene root signature (needs structured buffer + drawIndex root constant)
* Single-pixel readback at mouse position each frame (1-frame latency)
* `drawIndexToEntity` vector (in Application) maps draw index → flecs entity
* Methods: `createResources()`, `resize()`, `readPickResult()`, `copyPickedPixel()`

**SsaoRenderer** (`ssao.ixx` + `ssao.cpp`) — SSAO subsystem:

* Normal pre-pass RT (`R8G8B8A8_UNORM`, world-space normals), SSAO RT (`R8_UNORM`), blur RT (`R8_UNORM`)
* Own RTV heap (3) and SRV heap (4: normal, depth, noise, ssaoRT); SSAO output SRV placed in `sceneSrvHeap[nBuffers+2]` for scene pass
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

**ImGuiLayer** (`imgui_layer.ixx` + `imgui_layer.cpp`) — owns ImGui init/teardown:

* SRV descriptor heap for ImGui
* Methods: `init()`, `shutdown()`, `styleColorsDracula()`
* Note: `renderImGui()` is in `application_ui.cpp` (app-specific UI)

### Application class (split across 5 files)

* `application.cpp` — constructor, destructor, `update()`, helpers
* `application_render.cpp` — `render()` — shadow, cubemap, normal pre-pass, SSAO, scene, outline, ID, billboards, bloom, imgui, present
* `application_ui.cpp` — `renderImGui()` with all ImGui menus, inspector, tooltips
* `application_setup.cpp` — `loadContent()`, `createScenePSO()`, `createNormalPSO()`, `createCubemapResources()`, `onResize()`
* `application_scene.cpp` — `extractSceneData()`, `applySceneData()` — scene file serialization

Thin orchestrator — owns the render loop, swap chain, scene PSO, and input:

* **Swap chain**: triple-buffered, `R8G8B8A8_UNORM`.
* **Root signature**: 6 root params — \[0\] SRV table (t0, structured buffer), \[1\] 1 root constant (`drawIndex`, b0), \[2\] SRV table (t1, shadow map), \[3\] SRV table (t2, cubemap), \[4\] 4 root constants (b1, outline params), \[5\] SRV table (t3, SSAO). Static samplers: s0 (shadow comparison PCF), s1 (cubemap linear).
* **Scene PSO**: standard rasterization to HDR RT, stencil write. **Shadow PSO**: owned by `ShadowRenderer`, depth-only, front-face culling, depth bias. **Normal PSO**: vertex shader + `normal_ps.hlsl` → `R8G8B8A8_UNORM`. **Outline PSO**: owned by `OutlineRenderer`, extrude verts along normal, stencil test NOT_EQUAL.
* **Vertex format**: `VertexPBR` — position (float3), normal (float3), UV (float2).
* Delegates to subsystems: `scene.*`, `bloom.*`, `imguiLayer.*`, `ssao.*`, `shadow.*`, `outline.*`.
* **Shader hot reload**: polls `.hlsl` timestamps every 0.5s in `update()`, recompiles via `dxc.exe`, recreates PSOs. Scene PSO via `createScenePSO()`, shadow PSO via `shadow.reloadPSO()`, outline PSO via `outline.reloadPSO()`, bloom PSOs via `bloom.reloadPipelines()`. Enabled automatically when `DXC_PATH` and `SHADER_SRC_DIR` are set (CMake provides both).
* **Animation system**: `update()` runs `scene.animQuery.each<Transform, Animated>()` — orbits entities around the Y axis at individual speeds, applies sinusoidal scale pulse (±15%). Central teapot has no `Animated` component and stays stationary. `scene.instanceAnimQuery.each<InstanceGroup, InstanceAnimation>()` spins each instance group in place by rebuilding transforms each frame from stored base positions/scales.
* **GPU instancing**: `InstanceGroup` component stores N transforms and per-instance material overrides (albedo, roughness, metallic, emissiveStrength). Rendered via one `DrawIndexedInstanced(..., N, ...)` per group. Vertex/outline shaders use `SV_InstanceID` to index into `drawData[drawIndex + instanceID]`. `totalSlots` = regular entity count + sum of all instance group sizes; shadow/cubemap draw data stored at offsets `totalSlots` and `2*totalSlots`.

### Rendering pipeline

The rendering pipeline is now orchestrated via the **Render Graph**. `Application::render()` builds the graph each frame and executes it.

```
update()  →  render()
              └─ RenderGraph::execute()
                  ├─ Shadow pass      (depth-only to 2048² shadow map)
                  ├─ Cubemap pass     (6-face env map, non-reflective only)
                  ├─ Normal pre-pass  (world normals → R8G8B8A8 normalRT)
                  ├─ SSAO pass        (depth → PSR, hemisphere sampling → R8 ssaoRT)
                  ├─ Scene pass       (HDR RT, samples shadow+cubemap+SSAO)
                  ├─ Grid pass        (infinite Y=0 grid, alpha-blended, depth-tested)
                  ├─ Outline pass     (silhouette for hovered/selected)
                  ├─ ID pass          (R32_UINT RT, entity index per pixel)
                  ├─ Billboards pass  (light sprite rendering)
                  ├─ Bloom pass       (Prefilter → Downsample → Upsample → Composite)
                  ├─ ImGui pass       (UI overlay)
                  └─ Present pass     (Transition backbuffer to PRESENT state)
```

**Resource State Tracking**: The Render Graph automatically generates `ResourceBarrier` calls between passes. For example, it ensures the depth buffer is in `DEPTH_WRITE` for the normal pre-pass, then transitions it to `PIXEL_SHADER_RESOURCE` for SSAO, and back to `DEPTH_WRITE` for the scene pass.

**GPU instancing**: Two draw modes coexist. Regular entities (`MeshRef` component) get one `DrawIndexedInstanced(..., 1, ...)` call each. `InstanceGroup` entities batch N instances of a single mesh into one `DrawIndexedInstanced(..., N, ...)` call. Both write consecutive `SceneConstantBuffer` entries in the structured buffer. Vertex shaders use `drawData[drawIndex + SV_InstanceID]` — backwards-compatible since `SV_InstanceID=0` for non-instanced draws. `drawIndexToEntity` has one entry per instance slot; picking any instance of a group selects the group entity. Instance groups are not `Pickable` and not animated. `DrawCmd` struct (exported from `scene.ixx`) carries `instanceCount` and `baseDrawIndex` for unified draw loop logic across all passes.

**Shadow mapping**: 2048×2048 `R32_TYPELESS`/`D32_FLOAT` depth texture. Orthographic projection from directional light. 3×3 PCF via `SampleCmpLevelZero`. Shadow draw data stored at structured buffer offset `totalSlots` (same model transforms, light viewProj). Shadow PSO uses front-face culling + depth bias to reduce peter-panning/acne.

**Cubemap reflections**: Dynamic environment cubemap (`R11G11B10_FLOAT`, configurable resolution, default 128). Rendered from the first reflective entity's position. Only non-reflective entities are drawn into the cubemap (no recursion). Materials with `reflective=true` sample the cubemap via `reflect(-V, N)` in the pixel shader, weighted by Fresnel and inverse roughness. Cubemap draw data stored at structured buffer offset `2*totalSlots`. Resources: `cubemapTexture` (6 array slices), `cubemapDepth`, `cubemapRtvHeap` (6 RTVs), `cubemapDsvHeap` (6 DSVs), SRV at `sceneSrvHeap[nBuffers+1]`. `createCubemapResources()` recreates when resolution changes. If there are no non-reflective entities, the cubemap pass is skipped and emits a warning.

**SSAO**: Screen-Space Ambient Occlusion computed in two passes. Normal pre-pass renders world-space normals to `R8G8B8A8_UNORM` normalRT; main depth buffer transitions DEPTH_WRITE → PIXEL_SHADER_RESOURCE for SSAO read, then back. SSAO shader reconstructs view-space position from depth + invProj, transforms normals to view space, builds TBN from tiled 4×4 random noise, samples 32-point hemisphere kernel, range-checks occlusion. Blur pass applies 3×3 box filter. Output (`ssaoBlurRT`, `R8_UNORM`) SRV at `sceneSrvHeap[nBuffers+2]` (root param \[5\], t3 in pixel shader). Scene PSO uses `normalPSO` (vertex_shader.hlsl + normal_ps.hlsl → normalRT). `SsaoRenderer::enabled` skips all passes when false.

**Bloom**: 5-mip chain — prefilter (Karis average, soft threshold), 4× downsample, 4× upsample (tent filter, additive blend).

**Tonemappers** (selectable in UI): ACES Filmic, AgX, AgX Punchy, Gran Turismo / Uchimura, PBR Neutral.

**Rayleigh sky + clouds**: Shared implementation in `src/sky.hlsli` (included by `bloom_composite_ps.hlsl` and `pixel_shader.hlsl`). `rayleighSky(viewDir, sunDir, time)` computes Rayleigh scattering + procedural FBM clouds (hash-based value noise, 5 octaves, spherical projection, time-animated drift). HDR RT is cleared to black so the composite pass detects background via `sceneLum < 0.001`. `SkyParams` includes `time` (from `lightTime`); `PerFrameCB` also has `time` for reflection sky. DXC includes are enabled via `-I src/` in both CMake and shader hot reload. Cubemap reflection fallback in `pixel_shader.hlsl` uses the same sky function when envMap sample is near-black.

**Infinite grid**: Rendered via `grid_vs.hlsl` + `grid_ps.hlsl` with its own root signature (`gridRootSig`) and PSO (`gridPSO`). Vertex shader generates a fullscreen triangle, unprojects near/far planes to world space via `InvViewProj`. Pixel shader ray-intersects the Y=0 plane, draws unit lines (1m) and major lines (10m) with `fwidth`-based anti-aliasing. X axis highlighted blue, Z axis red. Alpha-blended with distance fade (80m). Depth-tested against scene geometry (read-only depth). Uses perPass CB slot 10 for `GridCB` (ViewProj, InvViewProj, CameraPos). Toggled via `showGrid` (Display menu, saved in scene files).

**Ocean fog**: Height-based fog in `pixel_shader.hlsl`. Thickens exponentially below `FogStartY` (water surface). Fog color darkens with depth — near-surface uses `FogColor`, deep areas fade toward black. Also blends distance fog for depth cueing. Parameters (`FogStartY`, `FogDensity`, `FogColor`) stored in `SceneConstantBuffer` and editable in UI.

### PBR / BSRDF shader (`src/pixel_shader.hlsl`)

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

* **CLI**: first positional argument is a scene file path (replaces old `--test` flag). If omitted, engine loads `resources/scenes/default.json`.
* **Scene files**: stored in `resources/scenes/` (`SCENES_DIR` CMake define)
  * `test.json` — test automation scene (WARP, hidden window, screenshot at frame 10, exit)
  * `empty.json` — empty scene with defaults, spawning stopped
  * `default.json` — default startup scene with one reflective teapot when no scene argument is provided
* **Runtime block**: optional section in scene files for automation and startup mode: `useWarp`, `hideWindow`, `screenshotFrame`, `exitAfterScreenshot`, `spawnPerFrame`, `skipImGui`, `singleTeapotMode`
* **UI**: Scene menu has save/load path input + buttons
* **Implementation**: `scene_file.ixx` module wraps `glaze_impl.cpp` (isolated TU for glaze templates). `application_scene.cpp` has `applySceneData()`/`extractSceneData()` for converting between `SceneFileData` structs and Application state. Data structs in `include/scene_data.h` reuse engine types directly — `OrbitCamera`, `TerrainParams`, `Material`, `Animated`, `PointLight` — no duplicate "data" structs.

### ImGui UI panels

* **Display**: vsync toggle, grid toggle, fullscreen toggle, tearing status, runtime mode.
* **Camera**: FOV, near/far planes, orbit radius, yaw, pitch.
* **Bloom**: threshold, intensity sliders.
* **Tonemapping**: tonemapper combo.
* **Scene**: background color; directional light direction/color/brightness; point light brightness; ambient brightness; ocean fog (start Y, density, color).
* **Shadows**: enable/disable; shader bias; raster depth/slope/clamp bias (rebuilds shadow PSO on change); shadow light distance, ortho size, near/far.
* **Animation**: entity animation toggle; light animation speed; light time scrub.
* **Spawning**: manual pause/resume, auto-stop toggle, frame-ms threshold, spawn batch size, reset perf gate.
* **Lights**: billboard toggle/size; point-light brightness; per-light center/amplitude/frequency/color controls for all `PointLight` entities (iterated via `lightQuery`).
* **Material**: albedo, roughness, metallic, emissive color + strength, reflective checkbox. Material selector when GLB has multiple.
* **Reflections**: cubemap enable/disable, cubemap resolution slider (32–512, recreates resources on change), cubemap near/far planes.
* **SSAO**: enable/disable, radius, bias, kernel size sliders.
* **Scene** (file menu): scene path input + Load/Save buttons; GLB path input + Load button + Reset-to-Teapot; title/description text inputs (saved in scene JSON, shown as bottom-right overlay).
* **Create**: mesh selector (from loaded mesh refs), material selector, position/scale, animated toggle with speed/radius. Spawns entity on click.
* **Entity Inspector**: shown when entity is selected. Tabbed view of Transform (editable position), MeshRef (material properties, albedo override), Animated (speed, orbit, scale), Pickable (remove toggle). Add Animated/Pickable buttons, Delete button (red). Hover tooltip shows entity ID + material on mouseover.
* **Title/Description overlay**: when `sceneTitle` / `sceneDescription` are set, drawn directly to foreground via `ImGui::GetForegroundDrawList()` in the bottom-right corner. Title at 1.4× font size, description at normal size, both with 1-pixel drop shadow.


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
| doctest v2.4.11 | FetchContent | Unit testing + CTest discovery |
| Tracy v0.13.1 | FetchContent | CPU+GPU profiling (on-demand) |


---

## Code Style

* clang-format: Chromium base, 4-space indent, 100-col limit.
* clang-tidy: bugprone, modernize, performance, readability checks.
* Windows subsystem (no console) — use `spdlog` for all logging.
* DX12 debug layer enabled in Debug builds.


---

## Profiling

Tracy Profiler v0.11.1 is integrated for CPU and GPU instrumentation. `TRACY_ON_DEMAND` is set so there is no overhead when the viewer is not connected.

* **Header**: `include/profiling.h` — include ONLY from `.cpp` files, never from `.ixx` modules
* **GPU context**: `g_tracyD3d12Ctx` (file-static in `application.cpp`, `extern`'d in `application_render.cpp` and `application_setup.cpp`); created in `loadContent()`, destroyed in `~Application()` after `flush()`
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

`renderImGui` is called mid-frame with an open command list. Never call `clearScene()`, `flush()`, or `loadGltf()` directly inside it — use the deferred flags `pendingGltfPath` / `pendingResetToTeapot`, processed at the start of `update()`.

### Tracy D3D12 callstack depth gotcha

With Tracy v0.13.1, `TRACY_CALLSTACK` is defined as `0` by default in `Tracy.hpp`, but `TracyD3D12NamedZone` still routes to the callstack overload when the macro exists. This can assert in `TracyCallstack.hpp` (`depth >= 1 && depth < 63`). Keep GPU profiling wrapper on `TracyD3D12NamedZoneS(..., depth=1, ...)` (see `include/profiling.h`) unless callstack configuration changes.

### Fullscreen toggles are deferred

Fullscreen transitions can generate synchronous `WM_SIZE` events that trigger `onResize()` and swap chain `ResizeBuffers()`. Queue UI fullscreen requests via `pendingFullscreenChange` / `pendingFullscreenValue` and apply them at the start of `update()`. Resources recreated in `onResize()` (HDR RT, depth buffer, bloom mips) must be created in their render-graph-expected initial states (e.g. HDR RT in `PIXEL_SHADER_RESOURCE`, not `RENDER_TARGET`) to avoid state mismatch on the first frame after resize.

### ImGui input capture

Camera rotation, zoom, and entity picking are gated by `ImGui::GetIO().WantCaptureMouse` in `update()` to prevent input passthrough when interacting with UI panels.

### PointLight entities survive clearScene()

`clearScene()` removes entities via `delete_with<MeshRef>()` and `delete_with<InstanceGroup>()` — only entities with those components are deleted. `PointLight` entities have no `MeshRef`, so they persist across scene resets. Light data is restored from JSON via `lightQuery.each(...)` in `applySceneData()`.

### TerrainEntity tag and positionY

The terrain entity is tagged with `TerrainEntity` at creation (in `loadContent()`). `applySceneData()` queries for `TerrainEntity` entities to update their `Transform.world = translate(0, positionY, 0)` from the scene file. This must be guarded by `contentLoaded` since terrain is created in the Application constructor (before the caller can invoke `applySceneData`).

### tinygltf in a separate TU

`TINYGLTF_IMPLEMENTATION` and `STB_IMAGE_IMPLEMENTATION` must be in `src/gltf_impl.cpp` (not in `application.cpp`) to avoid `stb_image` / `Windows.h` macro conflicts in the module implementation file.

### glTF matrix convention

glTF matrices are column-major. When loading into `XMMATRIX` (row-major), transpose: put glTF column *i* as XMMATRIX row *i*. For TRS nodes use `S * R * T` order in DirectXMath.