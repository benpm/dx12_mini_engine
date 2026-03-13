# AGENTS.md — DX12 Mini Engine

Guidance for AI agents (Claude Code, Codex, etc.) working in this repository.

---

## Build

- `VCPKG_ROOT` must be set to the `vcpkg` root dir!

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
- **Compiler**: `clang++` (v22). Do NOT use Git's clang (v18 — too old for VS 18 STL).
- **vcpkg**: `$VCPKG_ROOT` (x64-windows-static triplet).
- **Presets**: `windows-clang` (primary), `windows-msvc` (do not use!).
- Shaders compiled via DXC to `.cso` headers at build time.

### Rules
- After any change: build then run `--test` and inspect `screenshot.png`
- Before commiting:
  - `git pull`
  - Run clang-tidy and clang-format on all source and header files
  - Make updates to `AGENTS.md`

---

## Architecture

From-scratch DirectX 12 renderer. C++23 modules, Clang, Windows-only.

### Module files (`src/modules/*.ixx`)
| Module              | Purpose                                                                      |
| ------------------- | ---------------------------------------------------------------------------- |
| `window.ixx`        | Singleton HWND + D3D12Device2 creation, adapter selection, tearing detection |
| `application.ixx`   | Main Application class — orchestrates subsystems, render loop, input, UI     |
| `scene.ixx`         | `Scene` class — ECS world, mega-buffers, draw-data, materials, mesh loading  |
| `bloom.ixx`         | `BloomRenderer` class — HDR RT, bloom mip chain, root sig, PSOs             |
| `imgui_layer.ixx`   | `ImGuiLayer` class — descriptor heap, init/shutdown, Dracula style           |
| `command_queue.ixx` | ID3D12CommandQueue + fence sync + command allocator pooling                  |
| `camera.ixx`        | Base Camera + OrbitCamera (spherical yaw/pitch/radius)                       |
| `input.ixx`         | Button/Key enums, gainput integration                                        |
| `common.ixx`        | Math types (vec2/3/4, mat4), `chkDX()`, `_deg`/`_KB`/`_MB` literals          |
| `ecs_components.ixx`| ECS components: `Transform` (mat4 world), `MeshRef` (mega-buffer offsets)    |
| `shader_hotreload.ixx` | `ShaderCompiler` class — watches HLSL files, recompiles via DXC at runtime |
| `logging.ixx`       | spdlog setup with custom error sink                                          |

### Subsystem architecture

Application owns three subsystem instances: `Scene scene`, `BloomRenderer bloom`, `ImGuiLayer imguiLayer`.

**Scene** (`scene.ixx` + `scene.cpp`) — owns all scene state:
- ECS world (`flecs::world`), materials, spawn system
- Mega vertex/index buffers (1M verts, 4M indices, default heap)
- Triple-buffered structured draw-data buffers (`SceneConstantBuffer`)
- Methods: `createMegaBuffers()`, `createDrawDataBuffers()`, `appendToMegaBuffers()`, `clearScene()`, `loadTeapot()`, `loadGltf()`
- Also exports: `VertexPBR`, `SceneConstantBuffer`, `Material` structs

**BloomRenderer** (`bloom.ixx` + `bloom.cpp`) — owns all bloom/post-processing state:
- HDR render target, 5-mip bloom chain textures and descriptor heaps
- Bloom root signature + 4 PSOs (prefilter, downsample, upsample, composite)
- Methods: `createResources()`, `resize()`, `render()`

**ImGuiLayer** (`imgui_layer.ixx` + `imgui_layer.cpp`) — owns ImGui init/teardown:
- SRV descriptor heap for ImGui
- Methods: `init()`, `shutdown()`, `styleColorsDracula()`
- Note: `renderImGui()` stays in `application.cpp` (app-specific UI)

### Application class (`src/application.cpp`)
Thin orchestrator — owns the render loop, swap chain, scene PSO, and input:
- **Swap chain**: triple-buffered, `R8G8B8A8_UNORM`.
- **Scene PSO + root signature**: SRV descriptor table + 1 root constant (`drawIndex`).
- **Vertex format**: `VertexPBR` — position (float3), normal (float3), UV (float2).
- Delegates to subsystems: `scene.*`, `bloom.*`, `imguiLayer.*`.
- **Shader hot reload**: polls `.hlsl` timestamps every 0.5s in `update()`, recompiles via `dxc.exe`, recreates PSOs. Scene PSO via `createScenePSO()`, bloom PSOs via `bloom.reloadPipelines()`. Enabled automatically when `DXC_PATH` and `SHADER_SRC_DIR` are set (CMake provides both).

### Rendering pipeline

```
update()  →  render()
              ├─ Scene pass       (HDR RT, depth, per-mesh draw calls)
              ├─ Bloom prefilter  → downsample chain → upsample chain
              ├─ Composite        (HDR + bloom → swap chain backbuffer)
              └─ ImGui overlay    (directly to backbuffer)
```

**Bloom**: 5-mip chain — prefilter (Karis average, soft threshold), 4× downsample, 4× upsample (tent filter, additive blend).

**Tonemappers** (selectable in UI): ACES Filmic, AgX, AgX Punchy, Gran Turismo / Uchimura, PBR Neutral.

### PBR / BSRDF shader (`src/pixel_shader.hlsl`)
Cook-Torrance BRDF:
- **NDF**: GGX / Trowbridge-Reitz
- **Geometry**: Smith + Schlick-GGX
- **Fresnel**: Schlick approximation
- **Inputs** (from `SceneConstantBuffer`): albedo RGBA, roughness, metallic, emissive color + strength
- Single punctual light with scaled inverse-square attenuation (`1 / max(d² × 0.01, ε)`).

### Scene loading
- **Default**: teapot OBJ embedded as Win32 resource (`IDR_TEAPOT_OBJ`/`IDR_TEAPOT_MTL`).
- **GLB/glTF**: tinygltf v2.9.5 via FetchContent. Load from UI "Load GLB" panel (type path, press Load).
  - Supports binary GLB and ASCII glTF.
  - Extracts POSITION, NORMAL, TEXCOORD_0, indices (any component type).
  - Loads PBR metallic-roughness material factors (base color, roughness, metallic, emissive).
  - Traverses node hierarchy with TRS / matrix transforms.

### ImGui UI panels
- **Bloom**: threshold, intensity sliders.
- **Tonemapping**: tonemapper combo.
- **Scene**: background color, light brightness, ambient brightness.
- **Material**: albedo, roughness, metallic, emissive color + strength. Material selector when GLB has multiple.
- **Load GLB**: path input + Load button + Reset-to-Teapot button.

---

## Dependencies

| Library                          | Source                          | Notes                |
| -------------------------------- | ------------------------------- | -------------------- |
| directxtk12, directxmath, spdlog | vcpkg (x64-windows-static)      |                      |
| gainput                          | FetchContent (git hash 2be0a50) | Input                |
| imgui v1.92.6                    | FetchContent                    | Win32 + DX12 backend |
| tinyobjloader                    | FetchContent (git hash afdd3fa) | OBJ loading          |
| tinygltf v2.9.5                  | FetchContent                    | GLB/glTF loading     |

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
