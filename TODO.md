# Todo

<!-- Any todo item that has insufficient detail should be ignored. COMPLETE IN-ORDER! -->
<!-- After completing an item, check that the program is working, run all tests with ctest, check the program output image for correctness. If all is well, commit and push. -->

- [ ] **Hotkeys & Config:** Add rebindable keybinds for various editor actions (see `EditorAction::keyName` in @src/gui.cpp). Allow these to be configured through a JSON file, `config/default/hotkeys.json`, use glaze lib to load. Show keybinds for editor actions in the tooltips.
- [ ] **Hotkeys & Config:** Add rebindable keybinds for various editor actions (see `EditorAction::keyName` in @src/gui.cpp). Allow these to be configured through a JSON file, `config/default/hotkeys.json`, use glaze lib to load. Show keybinds for editor actions in the tooltips.
- [ ] **Editor Action Icons:** Find icons for all menu items in the GUI by searching this page: https://mui.com/material-ui/material-icons, and downloading 64x64 PNGs. Add icons to menus, submenus, and editor actions, as well as window titlebars in the program. Icons should be assignable thru a config file.
- [ ] Add [LuaJIT](https://luajit.org/) for dynamic scripting support. Scripts should be able to interact with entities. Editor actions should be able to be associated with scripts via JSON files. Expose as much as you can, especially interaction with the ECS, thru Lua scripts.
- [ ] Make configurable all colors via the JSON config

---

## Completed

<!-- Move item here when completed, reference the hash of this commit -->

- [X] **Reorganization:** Shaders → `src/shaders/`, Application .cpp files → `src/application/`
- [X] Escape deselects selected entity; if nothing selected, shows Yes/No exit confirmation dialog
- [X] **Info / Metrics:** FPS graph, draw calls, entity/component counts, Debug/Release indicator, View menu toggle
- [X] **Shader Hot Reload Robustness:** Failed hot reloads keep previous working PSO; try/catch around PSO recreation
- [X] Capture input events when interacting with the UI, preventing key presses and mouse events from being handled if the UI is occluding. This is a built in feature for imgui, use context7 to find the docs.
- [X] Add an outline fragment shader. Research best way to do outline rendering. When an object is hovered, instead of modulating the draw color, show an outline. If it's selected, show a thicker outline with a brighter color. — fd67af8
- [X] Remove explicit clang diagnostic ignore pragmas — de0ee11
- [X] Add SSAO (Screen-Space Ambient Occlusion) — f7c410c
- [X] Add an optional title and description field to the scene .json, which are displayed as text in the bottom right of the screen. — d5bb439
- [X] Add support for multiple objects using the same buffer for data, so that all geometry can be drawn in a single drawcall
- [X] Add UI scaling
- [X] Add object picking via ID render pass with Pickable component
- [X] Entity hover highlight (emissive tint) + click-to-select + tabbed inspector UI
- [X] Perlin noise terrain generation (256x256 grid, terrain module) — 9bdb739
- [X] Create Entity UI + enhanced inspector (delete, add components) — b3fe8d1