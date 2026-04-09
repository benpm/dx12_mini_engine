# Todo

<!-- AGENT:
    When asked, start doing tasks from top to bottom.
    During work, stop if there is a major issue, consistent build issues that have nothing to do with the task, etc. etc.

    - Before completing task:
      1. Check that task has sufficient information. If not, skip it for now
      2. git pull

    - After completing task:
      1. compact current context, update markdown files including anything in notes/, README.md and AGENTS.md. if a markdown note is bigger than 500 lines, split it up in a semantically meaningful way.
      2. run clang-tidy, clang-format on ALL source files, not just those edited
      3. mark task as done, copy it to the Completed section below
      4. commit and push, resolve merge conflicts if they are simple
 -->

- [X] **Hotkeys:** Add rebindable keybinds for various editor actions. Allow these to be configured via the config json. Show keybinds for editor actions in the tooltips. Allow multiple keys to be assigned to an action.
  - *F11* : toggle fullscreen
  - *Delete* : remove entity
  - *Tab* : start navigating imgui GUI with keyboard (already enabled via ImGuiConfigFlags_NavEnableKeyboard)
- [ ] **Editor Action Icons:** Find icons for all menu items in the GUI by searching this page: https://mui.com/material-ui/material-icons, and downloading 64x64 PNGs. Add icons to menus, submenus, and editor actions, as well as window titlebars in the program. Icons should be assignable thru the config file.
- [ ] **Lua Scripting Support**: Add support for [LuaJIT](https://luajit.org/) for dynamic scripting support. Scripts should be able to interact with entities. Editor actions should be able to be associated with scripts via JSON files. Expose as much as you can, especially interaction with the ECS, thru Lua scripts.
  - Editor actions should be able to be executed via a script
  - Scripts can be attached to entities via a `Scripted` component

---

## Completed

- [X] **Hotkeys:** Rebindable keybinds (F11=fullscreen, Delete=remove entity, Escape=deselect) via config.json, shown in UI tooltips
- [X] **Configuration**: Identify global settings that should be able to be configured via config file. Add a configuration loading and saving feature. If no config is loaded, the defaults should automatically be written out by the program to `config.json`. If that file exists, add and remove keys, but do not modify values otherwise. It should also be possible to pass a flag, `--dump-config`, to force writing to config.json, overwriting the values.
- [X] **Configuration**: config.json loading/saving with merge semantics (add/remove keys, preserve values), `--dump-config` flag
- [X]**Reorganization:** Shaders →`src/shaders/`, Application .cpp files →`src/application/`
- [X] Escape deselects selected entity; if nothing selected, shows Yes/No exit confirmation dialog
- [X]**Info / Metrics:** FPS graph, draw calls, entity/component counts, Debug/Release indicator, View menu toggle
- [X]**Shader Hot Reload Robustness:** Failed hot reloads keep previous working PSO; try/catch around PSO recreation
- [X] Fix "Add Animated" / entity mutation crash — defer all ECS mutations from UI to start of `update()`
- [X] Fix shader hot reload hang — async DXC compilation (non-blocking process launch + poll for completion)
- [X] Fix tooltip showing when hovering nothing — ID shader writes drawIndex+1, clear RT to 0, subtract 1 on readback
- [X] Mouse wheel zoom (already implemented)
- [X] 3D translation gizmo — 3 colored arrows (R/G/B for X/Y/Z) at selected entity position, click+drag to translate along axis
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
