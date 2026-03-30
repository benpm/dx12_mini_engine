# Todo

<!-- Any todo item that has insufficient detail should be ignored. Do these in order! -->

- [ ] Add LuaJIT for dynamic scripting support
- [ ] Add rebindable keybinds for various editor actions (see EditorAction::keyName in @src/gui.cpp). Allow these to be configured through a JSON file, use glaze lib to load. Show keybinds for editor actions in the tooltips.
- [ ] Find icons for all menu items in the GUI by searching this page: https://mui.com/material-ui/material-icons, and downloading PNGs
- [ ] Pressing escape should deselect the currently selected entity. If no entity is selected, show a dialog box that asks for confirmation for closing the window. If the confirmation is dismissed, do not close the window.
- [ ] Capture input events when interacting with the UI, preventing key presses and mouse events from being handled if the UI is occluding. This is a built in feature for imgui, use context7 to find the docs.

---

<!-- Move item here when completed, reference the hash of this commit -->

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