# AGENTS.md — DX12 Mini Engine

Guidance for AI agents (Claude Code, Codex, etc.) working in this repository.

---

## Build

- `VCPKG_ROOT` should be assumed to be set correctly
- If there is some external issue with the build environment that is not part of the task itself, wait, then retry the build. If it still fails, **STOP** and wait for user input, stating there is an issue with the build environment.

```bash
# Configure (Ninja Multi-Config, Clang 22 from LLVM)
cmake --preset windows-clang

# Build (Debug, Release builds)
cmake --build build --config Debug
cmake --build build --config Release

# Test (runs 10 frames, saves screenshot.png, exits)
./build/Debug/main.exe --test
```

### Toolchain notes
- **Compiler**: `clang++` (v22 or newer). Do NOT use Git's clang (v18 — too old for VS 18 STL).
- **vcpkg**: `$VCPKG_ROOT` (x64-windows-static triplet).
- **Presets**: `windows-clang` (primary), `windows-msvc` (do not use!).
- Shaders compiled via DXC to `.cso` headers at build time.

### Rules (IMPORTANT)
**Before working on a task / during planning phase:**
- Determine the best place to added new functionality. If it's a new feature, try making a new module file for it
- If working on a new feature, use the web search tool to perform initial research on the topic.
- If asked to create a new feature/mode/option for objects, properties, or relationships, consider how to integrate it with the existing ECS system.

**After finishing every task:**
- build then run `--test` and inspect `screenshot.png`
  - read `screenshot.png` with the Read tool, and visually verify the result looks correct before reporting done
- `git pull`
- Update `AGENTS.md` to reflect any new or modified architecture, modules, rendering pipeline steps, UI panels, key patterns, or dependencies. Keep it accurate and current.
- Run `clang-format` on all source and header files
- If any of the changed files in the working tree are longer than 1100 lines, split the file
  - Create new modules (.ixx) / source files (.cpp) as needed

---

## Architecture

From-scratch DirectX 12 renderer. C++23 modules, Clang, Windows-only.

### Module files (`src/modules/*.ixx`)
| Module                 | Purpose                                                                      |
| ---------------------- | ---------------------------------------------------------------------------- |
| `math.ixx`             | Pure math types (vec2/3/4, mat4), matrix/vector functions                    |
| `common.ixx`           | `chkDX()`, `_deg` literals, pi constants. Re-exports `math`                  |
| `window.ixx`           | Singleton HWND + D3D12Device2 creation, adapter selection, callback-based render loop |
| `application.ixx`      | Main Application class — orchestrates subsystems, render loop, input, UI     |
| `scene.ixx`            | `Scene` class — ECS world, mega-buffers, draw-data, materials, mesh loading  |
| `bloom.ixx`            | `BloomRenderer` class — HDR RT, bloom mip chain, root sig, PSOs              |
| `imgui_layer.ixx`      | `ImGuiLayer` class — descriptor heap, init/shutdown, Dracula style           |
| `command_queue.ixx`    | ID3D12CommandQueue + fence sync + command allocator pooling                  |
| `camera.ixx`           | Base Camera + OrbitCamera (uses `module :private;` for implementation)       |
| `input.ixx`            | Button/Key enums, gainput integration (uses `module :private;` for global)   |
| `ecs_components.ixx`   | ECS components: `Transform`, `MeshRef`, `Animated`, `Pickable`               |
| `shader_hotreload.ixx` | `ShaderCompiler` class — watches HLSL files, recompiles via DXC at runtime   |
| `billboard.ixx`        | `BillboardRenderer` class — point light sprite rendering                     |
| `object_picking.ixx`   | `ObjectPicker` class — ID render pass, readback for entity picking           |
| `terrain.ixx`          | `TerrainParams` struct + `generateTerrain()` — Perlin noise heightmap mesh   |
| `logging.ixx`          | spdlog setup with custom error sink                                          |

### Module conventions
- **No `export using namespace`** — each file declares its own `using` locally (e.g., `using Microsoft::WRL::ComPtr;`)
- **`export import` only for types in public API** — use plain `import` for internal dependencies
- **`module :private;`** for tiny implementations — avoids separate `.cpp` for <20 lines
- **`include/d3dx12_clean.h`** wraps `d3dx12.h` with Clang warning suppression — use instead of raw include
- **Application public API is minimal** — only `update()`, `render()`, `testMode`, `cam`, `inputMap`, `keyboardID` are public

### Subsystem architecture

Application owns three subsystem instances: `Scene scene`, `BloomRenderer bloom`, `ImGuiLayer imguiLayer`.

**Scene** (`scene.ixx` + `scene.cpp`) — owns all scene state:
- ECS world (`flecs::world`), materials, spawn system
- Mega vertex/index buffers (1M verts, 4M indices, default heap)
- Triple-buffered structured draw-data buffers (`SceneConstantBuffer`)
- Methods: `createMegaBuffers()`, `createDrawDataBuffers()`, `appendToMegaBuffers()`, `clearScene()`, `loadTeapot()`, `loadGltf()`
- Also exports: `VertexPBR`, `SceneConstantBuffer`, `Material`, `MaterialPreset` structs/enums
- **Preset materials**: `MaterialPreset` enum (`Diffuse`, `Metal`, `Mirror`). Created in both `loadTeapot()` and `loadGltf()`. Indices stored in `Scene::presetIdx[]`. Spawned entities get a random preset — diffuse uses random HSL color, metal darkens the color to 25%, mirror uses material's black albedo.

**BloomRenderer** (`bloom.ixx` + `bloom.cpp`) — owns all bloom/post-processing state:
- HDR render target, 5-mip bloom chain textures and descriptor heaps
- Bloom root signature + 4 PSOs (prefilter, downsample, upsample, composite)
- Methods: `createResources()`, `resize()`, `render()`

**ObjectPicker** (`object_picking.ixx` + `object_picking.cpp`) — entity picking via ID render pass:
- R32_UINT render target (viewport-sized), own depth buffer
- Reuses scene vertex shader + ID pixel shader (`id_ps.hlsl`) that outputs draw index
- PSO created from scene root signature (needs structured buffer + drawIndex root constant)
- Single-pixel readback at mouse position each frame (1-frame latency)
- `drawIndexToEntity` vector (in Application) maps draw index → flecs entity
- Methods: `createResources()`, `resize()`, `readPickResult()`, `copyPickedPixel()`

**ImGuiLayer** (`imgui_layer.ixx` + `imgui_layer.cpp`) — owns ImGui init/teardown:
- SRV descriptor heap for ImGui
- Methods: `init()`, `shutdown()`, `styleColorsDracula()`
- Note: `renderImGui()` is in `application_ui.cpp` (app-specific UI)

### Application class (split across 3 files)
- `application.cpp` — constructor, destructor, `update()`, `render()`, helpers (~1000 lines)
- `application_ui.cpp` — `renderImGui()` with all ImGui menus, inspector, tooltips (~370 lines)
- `application_setup.cpp` — `loadContent()`, `createScenePSO()`, `createShadowPSO()`, `createCubemapResources()`, `onResize()` (~520 lines)

Thin orchestrator — owns the render loop, swap chain, scene PSO, and input:
- **Swap chain**: triple-buffered, `R8G8B8A8_UNORM`.
- **Root signature**: 4 root params — SRV descriptor table (t0, structured buffer), 1 root constant (`drawIndex`, b0), SRV descriptor table (t1, shadow map), SRV descriptor table (t2, cubemap). Static samplers: s0 (shadow comparison PCF), s1 (cubemap linear).
- **Scene PSO**: standard rasterization to HDR RT. **Shadow PSO**: depth-only, front-face culling, depth bias.
- **Vertex format**: `VertexPBR` — position (float3), normal (float3), UV (float2).
- Delegates to subsystems: `scene.*`, `bloom.*`, `imguiLayer.*`.
- **Shader hot reload**: polls `.hlsl` timestamps every 0.5s in `update()`, recompiles via `dxc.exe`, recreates PSOs. Scene PSO + shadow PSO via `createScenePSO()` / `createShadowPSO()`, bloom PSOs via `bloom.reloadPipelines()`. Enabled automatically when `DXC_PATH` and `SHADER_SRC_DIR` are set (CMake provides both).
- **Animation system**: `update()` runs `ecsWorld.each<Transform, Animated>()` — orbits entities around the Y axis at individual speeds, applies sinusoidal scale pulse (±15%). Central teapot has no `Animated` component and stays stationary.

### Rendering pipeline

```
update()  →  render()
              ├─ Shadow pass      (depth-only to 2048² shadow map, directional light ortho VP)
              ├─ Cubemap pass     (6-face env map from first reflective entity, non-reflective only)
              ├─ Scene pass       (HDR RT, depth, per-mesh draw calls, samples shadow map + cubemap)
              ├─ ID pass          (R32_UINT RT, own depth, same draw calls → entity index per pixel)
              ├─ Readback copy    (single pixel at mouse pos → CPU readback buffer)
              ├─ Bloom prefilter  → downsample chain → upsample chain
              ├─ Composite        (HDR + bloom → swap chain backbuffer)
              └─ ImGui overlay    (directly to backbuffer)
```

**Shadow mapping**: 2048×2048 `R32_TYPELESS`/`D32_FLOAT` depth texture. Orthographic projection from directional light. 3×3 PCF via `SampleCmpLevelZero`. Shadow draw data stored at structured buffer offset `entityCount` (same model transforms, light viewProj). Shadow PSO uses front-face culling + depth bias to reduce peter-panning/acne.

**Cubemap reflections**: Dynamic environment cubemap (`R11G11B10_FLOAT`, configurable resolution, default 128). Rendered from the first reflective entity's position. Only non-reflective entities are drawn into the cubemap (no recursion). Materials with `reflective=true` sample the cubemap via `reflect(-V, N)` in the pixel shader, weighted by Fresnel and inverse roughness. Cubemap draw data stored at structured buffer offset `2*entityCount`. Resources: `cubemapTexture` (6 array slices), `cubemapDepth`, `cubemapRtvHeap` (6 RTVs), `cubemapDsvHeap` (6 DSVs), SRV at `sceneSrvHeap[nBuffers+1]`. `createCubemapResources()` recreates when resolution changes. If there are no non-reflective entities, the cubemap pass is skipped and emits a warning.

**Bloom**: 5-mip chain — prefilter (Karis average, soft threshold), 4× downsample, 4× upsample (tent filter, additive blend).

**Tonemappers** (selectable in UI): ACES Filmic, AgX, AgX Punchy, Gran Turismo / Uchimura, PBR Neutral.

### PBR / BSRDF shader (`src/pixel_shader.hlsl`)
Cook-Torrance BRDF:
- **NDF**: GGX / Trowbridge-Reitz
- **Geometry**: Smith + Schlick-GGX
- **Fresnel**: Schlick approximation
- **Inputs** (from `SceneConstantBuffer`): albedo RGBA, roughness, metallic, reflective flag, emissive color + strength
- 8 animated point lights with scaled inverse-square attenuation (`1 / max(d² × 0.01, ε)`).
- 1 directional light (shadow-casting) — direction, color, brightness configurable in UI.

### Scene loading
- **Default**: teapot OBJ embedded as Win32 resource (`IDR_TEAPOT_OBJ`/`IDR_TEAPOT_MTL`). `loadTeapot()` now creates a reflective teapot plus a non-reflective companion teapot so cubemap reflections always have environment content.
- **Startup model loading**: all `.glb` files in `resources/models/` are loaded automatically at startup via `MODELS_DIR` (CMake-defined). Spawned entities pick randomly from all loaded mesh refs.
- **GLB/glTF**: tinygltf v2.9.5 via FetchContent. Load from UI "Load GLB" panel (type path, press Load).
  - Supports binary GLB and ASCII glTF.
  - Extracts POSITION, NORMAL, TEXCOORD_0, indices (any component type).
  - Loads PBR metallic-roughness material factors (base color, roughness, metallic, emissive).
  - Traverses node hierarchy with TRS / matrix transforms.
- **Model files**: stored in `resources/models/` (teapot.obj/mtl + GLB primitives).

### ImGui UI panels
- **Display**: vsync toggle, fullscreen toggle, tearing/test-mode status.
- **Camera**: FOV, near/far planes, orbit radius, yaw, pitch.
- **Bloom**: threshold, intensity sliders.
- **Tonemapping**: tonemapper combo.
- **Scene**: background color; directional light direction/color/brightness; point light brightness; ambient brightness.
- **Shadows**: enable/disable; shader bias; raster depth/slope/clamp bias (rebuilds shadow PSO on change); shadow light distance, ortho size, near/far.
- **Animation**: entity animation toggle; light animation speed; light time scrub.
- **Spawning**: manual pause/resume, auto-stop toggle, frame-ms threshold, spawn batch size, reset perf gate.
- **Lights**: billboard toggle/size; point-light brightness; per-light center/amplitude/frequency/color controls for all 8 animated lights.
- **Material**: albedo, roughness, metallic, emissive color + strength, reflective checkbox. Material selector when GLB has multiple.
- **Reflections**: cubemap enable/disable, cubemap resolution slider (32–512, recreates resources on change), cubemap near/far planes.
- **Load GLB**: path input + Load button + Reset-to-Teapot button.
- **Create**: mesh selector (from loaded mesh refs), material selector, position/scale, animated toggle with speed/radius. Spawns entity on click.
- **Entity Inspector**: shown when entity is selected. Tabbed view of Transform (editable position), MeshRef (material properties, albedo override), Animated (speed, orbit, scale), Pickable (remove toggle). Add Animated/Pickable buttons, Delete button (red). Hover tooltip shows entity ID + material on mouseover.

---

## Dependencies

| Library                          | Source                          | Notes                |
| -------------------------------- | ------------------------------- | -------------------- |
| directxtk12, directxmath, spdlog | vcpkg (x64-windows-static)      |                      |
| gainput                          | FetchContent (git hash 2be0a50) | Input                |
| imgui v1.92.6                    | FetchContent                    | Win32 + DX12 backend |
| tinyobjloader                    | FetchContent (git hash afdd3fa) | OBJ loading          |
| tinygltf v2.9.5                  | FetchContent                    | GLB/glTF loading     |
| PerlinNoise v3.0.0               | FetchContent                    | Terrain heightmap    |

---

## Code Style
- clang-format: Chromium base, 4-space indent, 100-col limit.
- clang-tidy: bugprone, modernize, performance, readability checks.
- Windows subsystem (no console) — use `spdlog` for all logging.
- DX12 debug layer enabled in Debug builds.

---

## Key Patterns / Pitfalls

### GPU upload buffer lifetime
Intermediate upload heaps from `UpdateSubresources` **must** stay alive until after `cmdQueue.waitForFenceVal()`. Pass a `vector<ComPtr<ID3D12Resource>>& temps` to `uploadMesh` and clear it only after the GPU wait.

### SceneConstantBuffer layout
Must match `SceneCB` in both HLSL shaders exactly. Current fields: `model`, `viewProj`, `cameraPos`, `ambientColor`, `lightPos[8]`, `lightColor[8]`, `albedo`, roughness/metallic/emissiveStrength/reflective, `emissive`, `dirLightDir`, `dirLightColor`, `lightViewProj`, shadowBias/shadowMapTexelSize/_pad2[2].

### XMMATRIX alignment
`SceneConstantBuffer` contains `XMMATRIX` members — declare on the stack with `alignas(16)`:
```cpp
alignas(16) SceneConstantBuffer scb = {};
```

### No GPU ops inside renderImGui
`renderImGui` is called mid-frame with an open command list. Never call `clearScene()`, `flush()`, or `loadGltf()` directly inside it — use the deferred flags `pendingGltfPath` / `pendingResetToTeapot`, processed at the start of `update()`.

### tinygltf in a separate TU
`TINYGLTF_IMPLEMENTATION` and `STB_IMAGE_IMPLEMENTATION` must be in `src/gltf_impl.cpp` (not in `application.cpp`) to avoid `stb_image` / `Windows.h` macro conflicts in the module implementation file.

### glTF matrix convention
glTF matrices are column-major. When loading into `XMMATRIX` (row-major), transpose: put glTF column _i_ as XMMATRIX row _i_. For TRS nodes use `S * R * T` order in DirectXMath.
