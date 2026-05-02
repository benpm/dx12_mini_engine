# Todo

<!-- AGENT:
    When asked, start doing tasks from top to bottom.
    During work, stop if there is a major issue, consistent build issues that have nothing to do with the task, etc. etc.

    - Before completing task:
      1. Check that task has sufficient information. If not, ask clarifying questions in this document and move on
      2. git pull

    - After completing task:
      1. compact current context, update markdown files including anything in notes/, README.md and AGENTS.md. if a markdown note is bigger than 500 lines, split it up in a semantically meaningful way.
      2. run clang-tidy, clang-format on ALL source files, not just those edited
      3. mark task as done, copy it to the Completed section below
      4. commit and push, resolve merge conflicts if they are simple
 -->

- [ ] Add native file dialogs for choosing scripts and loading scenes in the gui. Also, split the gui
- [ ] Create a complete abstraction over all D3D12 / DirectX (gfx layer in `include/gfx.h`, `src/gfx/`, plan in `~/.claude/plans/write-an-api-abstraction-synchronous-wand.md`)
  - [x] P0: Land gfx skeleton + D3D12 backend stubs
  - [x] P1: Migrate device + swap chain into `gfx::IDevice` / `gfx::ISwapChain`
  - [x] P3: Render graph callbacks take `gfx::ICommandList&`
  - [x] P4: GBuffer signatures use gfx types
  - [x] P5: ShadowRenderer signatures use gfx types
  - [x] P6: SsaoRenderer signatures use gfx types
  - [x] P7: BloomRenderer signatures use gfx types
  - [x] P8: OutlineRenderer signatures use gfx types
  - [x] P9: ObjectPicker signatures use gfx types
  - [x] P10: BillboardRenderer + ImGuiLayer signatures use gfx types
  - [x] P11: GizmoState init takes gfx types
  - [x] P12 (partial): Scene methods take `gfx::IDevice&`. Still pending: dissolve `CommandQueue` into `gfx::IQueue`, migrate ComPtr fields to gfx handles, BLAS/TLAS gating on `caps.raytracing`.
  - [x] P13 (partial): scene PSO, gbuffer PSO, grid PSO are `gfx::PipelineHandle` (use `nativeRootSignatureOverride` escape hatch). New `IDevice::nativeResource()` accessor. Main `depthBuffer` migrated to `gfx::TextureHandle` via the new `TextureDesc::viewFormat` field.
  - [ ] P13 (remainder): dsvHeap, cubemap heaps, rootSignature ComPtrs.
  - [ ] P2: Bindless descriptor heap + bindless root sig + shader rewrite (high-risk, orthogonal)
  - [ ] P14: Cleanup — verify no `ID3D12*` outside `src/gfx/`, remove `nativeHandle()` escape hatches
- [ ] Use flecs
- [ ] Implement advanced culling techniques from [the culling techniques note](notes/culling-techniques.md)
  - [x] Frustum Culling (CPU-side bounding sphere)
  - [ ] Occlusion Culling (Hi-Z or Hardware Queries)

---

## Completed

- [x] Make grid lines not appear in front of geometry when that geometry is selected (Moved Gizmo pass after Grid/Outline passes)
- [x] Outlines no longer work (Fixed by moving Gizmo pass, which was clearing stencil)
- [x] Get the [sponza scene](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza) from the internet and load it (Downloaded GLB to resources/external_scenes/Sponza)
- [x] Implement Level of Detail (LOD)
  - [x] Added `LodMesh` component for distance-based mesh selection
- [x] Download and use some assets from [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) (DamagedHelmet, SciFiHelmet, Corset downloaded via CMake)
- [x] Fix no-arg startup Flecs invalid-entity assert by reinitializing gizmo after scene clear/reload paths; documented troubleshooting checklist in README.md and AGENTS.md.

- [x] **Editor Action Icons:** Material Icons font (`MaterialIcons-Regular.ttf`) merged into ImGui fonts. 34 icons from [MUI Material Icons](https://mui.com/material-ui/material-icons/) as 64x64 PNGs in `resources/icons/`. Icons on all 16 menu bar menus, 8 action buttons, 2 window titlebars. Icon assignments configurable via `config.json` `icons` map (key → Material Icon name). `include/icons.h` provides codepoint constants and lookup functions.
- [x] **Lua Scripting Support**: Add support for [LuaJIT](https://luajit.org/) for dynamic scripting support. Scripts should be able to interact with entities. Editor actions should be able to be associated with scripts via JSON files. Expose as much as you can, especially interaction with the ECS, thru Lua scripts.
  - Editor actions should be able to be executed via a script
  - Scripts can be attached to entities via a `Scripted` component
- [x] **Hotkeys:** Add rebindable keybinds for various editor actions. Allow these to be configured via the config json. Show keybinds for editor actions in the tooltips. Allow multiple keys to be assigned to an action.
  - *F11* : toggle fullscreen
  - *Delete* : remove entity
  - *Tab* : start navigating imgui GUI with keyboard (already enabled via ImGuiConfigFlags_NavEnableKeyboard)
- [x] **Lua Scripting Support**: Lua 5.4 via FetchContent, `Scripted` component, ~40 engine API functions (entity CRUD, transforms, materials, components, queries, spawning, editor actions), per-entity scripts with on_create/on_update/on_destroy lifecycle, action bindings via actions.json, script hot reload, Scripts menu + inspector UI. 24 unit tests (147 assertions) in `tests/lua_scripting_tests.cpp`. 6 example scripts in `resources/scripts/` (orbit, bounce, pulse_emissive, spawn_grid, randomize_colors, delete_all).
- [x] **Hotkeys:** Rebindable keybinds (F11=fullscreen, Delete=remove entity, Escape=deselect) via config.json, shown in UI tooltips
- [x] **Configuration**: Identify global settings that should be able to be configured via config file. Add a configuration loading and saving feature. If no config is loaded, the defaults should automatically be written out by the program to `config.json`. If that file exists, add and remove keys, but do not modify values otherwise. It should also be possible to pass a flag, `--dump-config`, to force writing to config.json, overwriting the values.
- [x] **Configuration**: config.json loading/saving with merge semantics (add/remove keys, preserve values), `--dump-config` flag
- [X]**Reorganization:** Shaders →`src/shaders/`, Application .cpp files →`src/application/`
- [x] Escape deselects selected entity; if nothing selected, shows Yes/No exit confirmation dialog
- [X]**Info / Metrics:** FPS graph, draw calls, entity/component counts, Debug/Release indicator, View menu toggle
- [X]**Shader Hot Reload Robustness:** Failed hot reloads keep previous working PSO; try/catch around PSO recreation
- [x] Fix "Add Animated" / entity mutation crash — defer all ECS mutations from UI to start of `update()`
- [x] Fix shader hot reload hang — async DXC compilation (non-blocking process launch + poll for completion)
- [x] Fix tooltip showing when hovering nothing — ID shader writes drawIndex+1, clear RT to 0, subtract 1 on readback
- [x] Mouse wheel zoom (already implemented)
- [x] 3D translation gizmo — 3 colored arrows (R/G/B for X/Y/Z) at selected entity position, click+drag to translate along axis
- [x] Capture input events when interacting with the UI, preventing key presses and mouse events from being handled if the UI is occluding. This is a built in feature for imgui, use context7 to find the docs.
- [x] Add an outline fragment shader. Research best way to do outline rendering. When an object is hovered, instead of modulating the draw color, show an outline. If it's selected, show a thicker outline with a brighter color. — fd67af8
- [x] Remove explicit clang diagnostic ignore pragmas — de0ee11
- [x] Add SSAO (Screen-Space Ambient Occlusion) — f7c410c
- [x] Add an optional title and description field to the scene .json, which are displayed as text in the bottom right of the screen. — d5bb439
- [x] Add support for multiple objects using the same buffer for data, so that all geometry can be drawn in a single drawcall
- [x] Add UI scaling
- [x] Add object picking via ID render pass with Pickable component
- [x] Entity hover highlight (emissive tint) + click-to-select + tabbed inspector UI
- [x] Perlin noise terrain generation (256x256 grid, terrain module) — 9bdb739
- [x] Create Entity UI + enhanced inspector (delete, add components) — b3fe8d1
