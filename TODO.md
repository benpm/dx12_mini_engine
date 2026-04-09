# Todo

<!-- AGENT:
    When asked, start doing tasks from top to bottom.
    During work, stop if there is a major issue, consistent build issues that have nothing to do with the task, etc. etc.

    - Before completing task:
      - Check that task has sufficient information. If not, skip it for now
      - git pull

    - After completing task:
      - run clang-tidy, clang-format on ALL source files, not just those edited
      - mark task as done, copy it to the Completed section below
      - commit and push, resolve merge conflicts if they are simple
 -->

- [ ] The program crashes when the "Add Animated" button is pressed. The error is on `application.cpp:715`, due to an invalid read or write. This is likely because the loop over entities is running while a component is being added? Not sure. Either way, adding components to entities should only happen during a pre-update stage every frame. Clicking the button should simply queue that action.
- [ ] The program hangs when a shader is hot reloaded.
- [ ] Add a interactive gizmo for transforming entities with the `Transform` component, allowing the user to move them via a set of 3 arrows, one for each 3D axis.
  - Generate the arrow mesh dynamically.
  - Clicking and dragging on one of the arrows should move the transform position in the direction of the associated axis.
- [ ] Clicking the "Add Animated" button crashes
- [ ] Currently, hovering over nothing shows a tooltip. Hide the tooltip when hovering nothing, and unhide once something is being hovered over.
- [ ] Scrolling the mouse wheel should zoom in and out.
- [ ] **Configuration**: Identify global settings that should be able to be configured via config file. Add a configuration loading and saving feature. If no config is loaded, the defaults should automatically be written out by the program to `config.json`. If that file exists, add and remove keys, but do not modify values otherwise. It should also be possible to pass a flag, `--dump-config`, to force writing to config.json, overwriting the values. When the config is loaded, 
- [ ] **Hotkeys:** Add rebindable keybinds for various editor actions. Allow these to be configured via the config json. Show keybinds for editor actions in the tooltips. Allow multiple keys to be assigned to an action.
  - F11 : toggle fullscreen
  - Delete
- [ ] **Editor Action Icons:** Find icons for all menu items in the GUI by searching this page: https://mui.com/material-ui/material-icons, and downloading 64x64 PNGs. Add icons to menus, submenus, and editor actions, as well as window titlebars in the program. Icons should be assignable thru the config file.
- [ ] **Lua Scripting Support**: Add support for [LuaJIT](https://luajit.org/) for dynamic scripting support. Scripts should be able to interact with entities. Editor actions should be able to be associated with scripts via JSON files. Expose as much as you can, especially interaction with the ECS, thru Lua scripts.
  - Editor actions should be able to be executed via a script
  - Scripts can be attached to entities via a `Scripted` component

---

## Completed

- [X]**Reorganization:** Shaders →`src/shaders/`, Application .cpp files →`src/application/`
- [X] Escape deselects selected entity; if nothing selected, shows Yes/No exit confirmation dialog
- [X]**Info / Metrics:** FPS graph, draw calls, entity/component counts, Debug/Release indicator, View menu toggle
- [X]**Shader Hot Reload Robustness:** Failed hot reloads keep previous working PSO; try/catch around PSO recreation
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
