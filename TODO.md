# Todo

<!-- Any todo item that has insufficient detail should be ignored. Do these in order! -->

- [X] Add an outline fragment shader. Research best way to do outline rendering. When an object is hovered, instead of modulating the draw color, show an outline. If it's selected, show a thicker outline with a brighter color. — fd67af8
- [ ] Add LuaJIT for dynamic scripting support
- [X] Remove explicit clang diagnostic ignore pragmas — de0ee11
- [X] Add SSAO (Screen-Space Ambient Occlusion) — f7c410c
- [ ] Add rebindable keybinds for various editor actions (see EditorAction::keyName in @src/gui.cpp). Allow these to be configured through a JSON file, use glaze lib to load. Show keybinds for editor actions in the tooltips.
- [X] Add an optional title and description field to the scene .json, which are displayed as text in the bottom right of the screen. — d5bb439

---

<!-- Move item here when completed, reference the hash of this commit -->

- [X] Add support for multiple objects using the same buffer for data, so that all geometry can be drawn in a single drawcall
- [X] Add UI scaling
- [X] Add object picking via ID render pass with Pickable component
- [X] Entity hover highlight (emissive tint) + click-to-select + tabbed inspector UI
- [X] Perlin noise terrain generation (256x256 grid, terrain module) — 9bdb739
- [X] Create Entity UI + enhanced inspector (delete, add components) — b3fe8d1