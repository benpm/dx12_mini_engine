# Architecture Reference

## High-Level Architecture

The engine is organized around a thin Application orchestrator plus subsystem modules.

- `Application`: per-frame update/render orchestration, swapchain integration, UI and input integration.
- `Scene`: ECS world ownership, mesh/material registries, draw command generation, GPU per-object/per-pass data.
- `RenderGraph`: frame pass graph and automated D3D12 resource state transitions.
- Specialized renderer modules: shadow, SSAO, bloom, outline, picking, billboards, gizmo, gbuffer.

## Core Source Layout

- Public module interfaces: `src/modules/*.ixx`
- Implementations: `src/*.cpp` and `src/application/*.cpp`
- Shared C/C++ headers for non-module/interoperability paths: `include/*.h`
- Shaders: `src/shaders/*.hlsl` and `src/shaders/*.hlsli`

## Main Runtime Flow

1. `src/main.cpp`
2. Initialize logging and COM
3. Parse CLI args and config
4. Load scene JSON (optional path or config default)
5. Initialize Window singleton and Application
6. Message loop:
   - Poll/update input
   - `app.update()`
   - `app.render()`

## Render Pipeline (Frame)

`Application::render()` builds and executes render graph passes each frame.

Typical pass order:

1. Shadow pass
2. Cubemap reflection pass
3. G-Buffer pass
4. SSAO pass
5. Main scene pass
6. Gizmo pass
7. Grid pass
8. Outline pass
9. ID picking pass
10. Billboard pass
11. Bloom pass (prefilter/downsample/upsample/composite)
12. ImGui pass
13. Present transition

## Module Map (`src/modules`)

- `application.ixx`: Application class API surface
- `billboard.ixx`: point-light billboard renderer
- `bloom.ixx`: HDR + bloom chain and post pipeline
- `camera.ixx`: camera type exports
- `command_queue.ixx`: queue/fence/allocator lifecycle
- `common.ixx`: shared utilities and error helpers
- `config.ixx`: config loading/merge/save interface
- `ecs_components.ixx`: ECS component exports
- `gbuffer.ixx`: deferred gbuffer resources/pipeline
- `gizmo.ixx`: translation gizmo state and behavior
- `imgui_layer.ixx`: ImGui init/shutdown/styling layer
- `input.ixx`: input enums/hotkey binding support
- `logging.ixx`: spdlog setup
- `lua_scripting.ixx`: script system interface
- `math.ixx`: math type exports
- `object_picking.ixx`: ID render pass + readback
- `outline.ixx`: stencil outline renderer
- `render_graph.ixx`: pass graph API and builder contracts
- `restir.ixx`: ReSTIR scaffolding
- `scene.ixx`: Scene class, draw data structs, mesh/material APIs
- `scene_file.ixx`: scene JSON serialization interface
- `shader_hotreload.ixx`: DXC-driven shader hot reload
- `shadow.ixx`: shadow map renderer/configuration
- `ssao.ixx`: SSAO renderer/configuration
- `terrain.ixx`: terrain generation API
- `window.ixx`: Window singleton (HWND only — device creation is in `gfx::IDevice`)
- `gfx.ixx`: backend-agnostic graphics abstraction (re-exports `include/gfx.h`)

## Application Split (`src/application`)

- `application.cpp`: constructor/destructor, update loop helpers
- `render.cpp`: frame rendering and render-graph pass wiring
- `scene.cpp`: scene data extract/apply
- `setup.cpp`: content loading, PSO creation, resize handling
- `ui.cpp`: ImGui menus, panels, inspector

## Data and Serialization

- Config data schema: `include/config_data.h`
- Scene data schema: `include/scene_data.h`
- Terrain data schema: `include/terrain_types.h`
- ECS component schema: `include/ecs_types.h`
- Material schema: `include/material_types.h`
- Camera schema: `include/camera_types.h`

JSON IO path:

- `src/config.cpp` + `src/glaze_impl.cpp`
- `src/scene_file.cpp` + `src/glaze_impl.cpp`

## Scripting

- Lua engine/module: `src/lua_scripting.cpp`
- Lua bindings implementation TU: `src/lua_scripting_impl.cpp`
- Script component data type: `include/lua_script_types.h`
- Script assets: `resources/scripts/*.lua`, `resources/scripts/actions.json`

## Testing

- Math unit tests: `tests/math_types_tests.cpp`
- Lua integration-style unit tests: `tests/lua_scripting_tests.cpp`
- CTest integration through CMake

## Assets and Runtime Data

- Models: `resources/models/`
- Scenes: `resources/scenes/`
- Scripts: `resources/scripts/`
- Fonts/icons for UI: `resources/fonts/`, `resources/icons/`
- Win32 resources: `resources.rc`, `include/resource.h`
