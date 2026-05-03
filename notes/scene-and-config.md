# Scene Loading, Scene Files, and Configuration

## Scene Loading

* **Default**: teapot OBJ embedded as Win32 resource (`IDR_TEAPOT_OBJ`/`IDR_TEAPOT_MTL`). `loadTeapot()` creates a reflective teapot plus a non-reflective companion so cubemap reflections always have environment content.
* **Startup model loading**: all `.glb` files in `resources/models/` loaded automatically at startup via `MODELS_DIR` (CMake-defined). Spawned entities pick randomly from all loaded mesh refs.
* **GLB/glTF**: tinygltf v2.9.5 via FetchContent. Load from UI "Load GLB" panel.
  * Supports binary GLB and ASCII glTF.
  * Extracts POSITION, NORMAL, TEXCOORD_0, indices (any component type).
  * Loads PBR metallic-roughness material factors (base color, roughness, metallic, emissive).
  * Traverses node hierarchy with TRS / matrix transforms.
* **Model files**: stored in `resources/models/`.

## Scene File System

JSON scene files (via glaze) store all configurable scene state: camera, bloom, lighting, fog, shadows, cubemap, terrain params, materials, entities, instance groups, and display settings.

* **CLI**: first positional argument is a scene file path. If omitted, engine loads from `config.json`'s `defaultScenePath` (default: `resources/scenes/default.json`). `--dump-config` writes default `config.json` and exits.
* **Scene files** (`resources/scenes/`):
  * `test.json` — test automation scene (WARP, hidden window, screenshot at frame 10, exit)
  * `empty.json` — empty scene with defaults, spawning stopped
  * `default.json` — default startup scene with one reflective teapot
* **Runtime block**: `useWarp`, `hideWindow`, `screenshotFrame`, `exitAfterScreenshot`, `spawnPerFrame`, `skipImGui`, `singleTeapotMode`
* **Implementation**: `scene_file.ixx` wraps `glaze_impl.cpp` (isolated TU for glaze templates). `src/application/scene.cpp` has `applySceneData()`/`extractSceneData()`. Data structs in `include/scene_data.h` reuse engine types directly.

## Configuration System

`config.json` stores global engine defaults. Struct in `include/config_data.h`, module in `config.ixx` + `config.cpp`.

* **Merge semantics**: `mergeConfig()` reads existing `config.json` (`error_on_unknown_keys = false`), then writes back. Missing keys get defaults, obsolete keys are dropped.
* **Load order**: config applied first via `app.applyConfig(config)`, then scene file via `app.applySceneData(sceneData)`. Scene values override config for shared settings.
* **Hotkeys**: `ConfigData::hotkeys` maps action names to key name lists. `HotkeyBindings` struct (in `input.ixx`) manages edge-triggered key detection via `GetAsyncKeyState`. Default bindings: F11=fullscreen, Delete=delete entity, Escape=deselect. New actions: add to `EditorAction` enum, add default in `HotkeyBindings::setDefaults()`, handle in `Application::update()`.
* **Icons**: `ConfigData::icons` maps UI element keys to Material Icon names. Icon names resolve to codepoints via `iconCodepointFromName()` in `include/icons.h`. Application caches icon UTF-8 strings in `iconCache` (rebuilt in `applyConfig()`). `iconLabel(key, label)` helper returns icon-prefixed label for ImGui widgets. New icons: add codepoint to `IconCP` namespace + lookup map in `icons.h`, add default mapping in `ConfigData::icons`.

## Lua Scripting System

Lua 5.4 (FetchContent, compiled as static C lib). Module in `lua_scripting.ixx` + `lua_scripting.cpp`, bindings in `lua_scripting_impl.cpp`.

* **Scripted component** (`include/lua_script_types.h`): `scriptPath` (relative to `SCRIPTS_DIR`), `luaRef`. Scripts return a table with optional `on_create(id)`, `on_update(id, dt)`, `on_destroy(id)` callbacks.
* **Engine API**: ~40 functions under global `engine` table — entity CRUD, transform get/set, material manipulation, component add/remove, entity queries, mesh spawning, editor action execution, logging, frame info.
* **Execution model**: `LuaScripting::updateScriptedEntities()` called each frame after deferred ECS mutations.
* **Action bindings**: `resources/scripts/actions.json` maps action names to script paths. One-shot scripts executed via `engine.execute_action("name")` or from Scripts menu.
* **Hot reload**: Polls script file timestamps every ~1s. Changed scripts are reloaded — old ref unreffed, `on_destroy`/`on_create` called.
* **Error handling**: All Lua calls via `lua_pcall`. Errors logged via spdlog.

## Example Lua Scripts (`resources/scripts/`)

* **Per-entity**: `orbit.lua` (Y-axis orbit), `bounce.lua` (vertical bounce with squash/stretch), `pulse_emissive.lua` (pulsing glow)
* **One-shot actions**: `spawn_grid.lua` (5x5 entity grid), `randomize_colors.lua` (random albedo on all MeshRef entities), `delete_all.lua` (destroy all MeshRef entities)
* **Action bindings**: `resources/scripts/actions.json` maps action names to script paths
