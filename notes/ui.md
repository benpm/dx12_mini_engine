# ImGui UI Panels

All menu bar menus, action buttons, and window titlebars display Material Icons via the merged icon font. Icons are config-driven. Organized into a standard editor structure:

* **File**: scene path (Load/Save), GLB path (Load/Reset), scene title/description.
* **Edit**:
  * **Create**: mesh selector, material selector, position/scale, animated toggle. Spawns entity on click.
  * **Material**: global material editor (albedo, roughness, metallic, emissive color + strength, reflective checkbox).
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

* **Metrics** (floating window): build mode (Debug/Release), FPS + frame ms, FPS graph (last 5s, collapsible), draw calls, objects, vertices, ECS entity/component counts, subsystem status (shadow/cubemap/SSAO).
* **Entity Inspector**: shown when entity is selected. Tabbed view of Transform (editable position), MeshRef (material properties, albedo override), Animated (speed, orbit, scale), Pickable (remove toggle), Scripted (script path, detach). Attach Script input, Add Animated/Pickable buttons, Delete button (red). Hover tooltip shows entity ID + material on mouseover.
* **Title/Description overlay**: when `sceneTitle` / `sceneDescription` are set, drawn directly to foreground via `ImGui::GetForegroundDrawList()` in the bottom-right corner. Title at 1.4× font size, description at normal size, both with 1-pixel drop shadow.

## Testing

* **Unit tests** (`tests/unit_tests.cpp`): doctest-based, covers math utilities, terrain generation, etc.
* **Lua scripting tests** (`tests/lua_scripting_tests.cpp`): 24 test cases (147 assertions) covering all ~40 `engine.*` Lua API functions. Uses a `LuaTestFixture` with a standalone flecs world, materials, mesh refs, and Lua state — no GPU or window needed.
* **CTest**: `doctest_discover_tests()` auto-registers all test cases. Run via `ctest --test-dir build -C Debug --output-on-failure`.
* **Integration test**: `./build/Debug/main.exe resources/scenes/test.json` loads a WARP-adapter scene, renders 10 frames, writes `screenshot.png`, and exits.
