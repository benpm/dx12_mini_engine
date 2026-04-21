# Repository Map

This document explains what each major directory is responsible for.

## Root

- `AGENTS.md`: detailed engineering, architecture, and agent workflow guidance
- `README.md`: public-facing project summary and quick usage
- `CMakeLists.txt`: project build graph, dependencies, and targets
- `CMakePresets.json`: configure/build presets
- `vcpkg.json`, `vcpkg-configuration.json`: package manager configuration
- `TODO.md`: task tracker and completed work log
- `config.json`: runtime defaults/config state
- `resources.rc`: Win32 resource script
- `LICENSE`: project license

Auxiliary root files:

- `CLAUDE.md`, `GEMINI.md`: agent instruction forwarding files
- `gui.cpp`: standalone/auxiliary source file in root
- `err.txt`, `log.txt`: runtime logs
- `banner.png`, `screenshot.png`: visual assets/screenshots
- `package.json`, `package-lock.json`: Node package metadata (currently empty package descriptor)

## Source Code

### `src/`

Primary implementation units.

- Core renderer and subsystems:
  - `scene.cpp`, `render_graph.cpp`, `shadow.cpp`, `ssao.cpp`, `bloom.cpp`, `gbuffer.cpp`, `outline.cpp`, `object_picking.cpp`, `billboard.cpp`, `gizmo.cpp`, `terrain.cpp`, `restir.cpp`
- Engine systems:
  - `window.cpp`, `command_queue.cpp`, `shader_hotreload.cpp`, `input.cpp`, `logging.cpp`, `config.cpp`, `scene_file.cpp`
- Entry point:
  - `main.cpp`
- Serialization/script bridge implementation units:
  - `glaze_impl.cpp`, `gltf_impl.cpp`, `lua_scripting.cpp`, `lua_scripting_impl.cpp`, `imgui_layer.cpp`, `camera.cpp`

### `src/application/`

Application orchestration split by concern.

- `application.cpp`: lifecycle and update-side logic
- `render.cpp`: frame render graph/passes
- `setup.cpp`: content setup and resize resource rebuild
- `scene.cpp`: scene state import/export
- `ui.cpp`: editor menus and panels

### `src/modules/`

C++23 module interface units defining public APIs.

- `application.ixx`, `scene.ixx`, `render_graph.ixx`, `window.ixx`, `command_queue.ixx`
- Rendering modules: `shadow.ixx`, `ssao.ixx`, `gbuffer.ixx`, `bloom.ixx`, `outline.ixx`, `billboard.ixx`, `object_picking.ixx`, `gizmo.ixx`, `terrain.ixx`, `restir.ixx`
- Support modules: `common.ixx`, `math.ixx`, `camera.ixx`, `input.ixx`, `ecs_components.ixx`, `config.ixx`, `logging.ixx`, `shader_hotreload.ixx`, `lua_scripting.ixx`, `scene_file.ixx`, `imgui_layer.ixx`

### `src/shaders/`

HLSL shader sources.

- Scene/deferred: `vertex_shader.hlsl`, `pixel_shader.hlsl`, `gbuffer_ps.hlsl`, `normal_ps.hlsl`
- Post/bloom: `fullscreen_vs.hlsl`, `bloom_prefilter_ps.hlsl`, `bloom_downsample_ps.hlsl`, `bloom_upsample_ps.hlsl`, `bloom_composite_ps.hlsl`
- Effects/utilities: `ssao_ps.hlsl`, `ssao_blur_ps.hlsl`, `outline_vs.hlsl`, `outline_ps.hlsl`, `id_ps.hlsl`, `billboard_vs.hlsl`, `billboard_ps.hlsl`, `grid_vs.hlsl`, `grid_ps.hlsl`, `sky.hlsli`

## Headers

### `include/`

Shared type definitions and interop headers.

- Core data schemas:
  - `scene_data.h`, `config_data.h`, `ecs_types.h`, `material_types.h`, `camera_types.h`, `terrain_types.h`, `lua_script_types.h`, `math_types.h`
- Rendering/platform support:
  - `d3dx12_clean.h`, `profiling.h`, `resource.h`, `icons.h`

## Resources

### `resources/models/`

Embedded/default and sample meshes:

- `teapot.obj`, `teapot.mtl`
- primitives in GLB format (cube, sphere, cone, cylinder, torus, capsule, plane, pyramid, icosphere)

### `resources/scenes/`

Scene JSONs:

- `default.json`: default startup content
- `empty.json`: near-empty baseline
- `test.json`: automation/integration test scene

### `resources/scripts/`

Lua scripts and action mapping:

- `actions.json`
- behavior/action scripts such as `orbit.lua`, `bounce.lua`, `pulse_emissive.lua`, `spawn_grid.lua`, `spawn_ring.lua`, `randomize_colors.lua`, `delete_all.lua`, `color_cycle.lua`

### `resources/fonts/`

- `MaterialIcons-Regular.ttf`

### `resources/icons/`

PNG icon assets used by UI icon mapping config.

## Tests

### `tests/`

- `math_types_tests.cpp`: math utility/unit tests
- `lua_scripting_tests.cpp`: scripting API and ECS interaction tests

## Notes

### `notes/`

Project and workflow notes, including this generated documentation set plus existing notes files.

## Build Output

### `build/`

Generated build artifacts and intermediate files.

Includes:

- generated shader bytecode headers (e.g. `*_cso.h`)
- CMake/Ninja build files
- dependency sub-build trees (`_deps/`)
- compiled outputs for Debug/Release variants
- CTest metadata

Treat this directory as generated output, not canonical source.

## Misc

### `screenshots/`

Storage folder for screenshots and visual captures.

### `notes/` and root markdown files

Documentation and planning content used during development.
