# Subsystem Architecture

Application owns subsystem instances: `Scene scene`, `BloomRenderer bloom`, `ImGuiLayer imguiLayer`, `ShadowRenderer shadow`, `OutlineRenderer outline`, `SsaoRenderer ssao`, `ObjectPicker picker`, `BillboardRenderer billboards`, `GizmoState gizmo`, `rg::RenderGraph renderGraph`.

## RenderGraph (`render_graph.ixx` + `render_graph.cpp`)

* Handles automated resource state transitions (barriers) based on pass inputs/outputs.
* Methods: `importTexture()`, `addPass()`, `execute()`.
* Use `RenderGraphBuilder` in pass setup to declare `readTexture()`, `writeRenderTarget()`, and `writeDepthStencil()`.

## Scene (`scene.ixx` + `scene.cpp`)

* ECS world (`flecs::world`), cached queries (`drawQuery`, `instanceQuery`, `instanceAnimQuery`, `animQuery`, `lightQuery`), materials, spawn system
* Mega vertex/index buffers (1M verts, 4M indices, default heap)
* Triple-buffered structured draw-data buffers (`SceneConstantBuffer`)
* Methods: `createMegaBuffers()`, `createDrawDataBuffers()`, `appendToMegaBuffers()`, `clearScene()`, `loadTeapot()`, `loadGltf()`
* Also exports: `VertexPBR`, `SceneConstantBuffer`, `Material`, `MaterialPreset` structs/enums
* **Preset materials**: `MaterialPreset` enum (`Diffuse`, `Metal`, `Mirror`). Spawned entities get a random preset.

## BloomRenderer (`bloom.ixx` + `bloom.cpp`)

* HDR render target, 5-mip bloom chain textures and descriptor heaps
* Bloom root signature + 4 PSOs (prefilter, downsample, upsample, composite)
* Methods: `createResources()`, `resize()`, `render()`

## ObjectPicker (`object_picking.ixx` + `object_picking.cpp`)

* R32_UINT render target (viewport-sized), own depth buffer, cleared to 0
* Reuses scene vertex shader + ID pixel shader (`src/shaders/id_ps.hlsl`) that outputs `drawIndex + 1` (0 = no entity)
* Single-pixel readback at mouse position each frame (1-frame latency)
* `drawIndexToEntity` vector (in Application) maps draw index → flecs entity
* Methods: `createResources()`, `resize()`, `readPickResult()`, `copyPickedPixel()`

## GBuffer (`gbuffer.ixx` + `gbuffer.cpp`)

* 4 render targets: Normal (`R8G8B8A8_UNORM`), Albedo (`R8G8B8A8_UNORM`), Material (`R8G8_UNORM`, roughness+metallic), Motion (`R16G16_FLOAT`)
* Own RTV heap (4) and SRV heap (4)
* G-Buffer pass uses `vertex_shader.hlsl` + `gbuffer_ps.hlsl` with 4 MRT outputs
* Motion vectors computed from `PrevViewProj` × `PrevModel` (new `PrevTransform` / `PrevInstanceGroup` ECS components)

## SsaoRenderer (`ssao.ixx` + `ssao.cpp`)

* SSAO RT (`R8_UNORM`), blur RT (`R8_UNORM`); reads normal from GBuffer Normal target
* Hemisphere kernel (32 samples, seed 42), 4×4 `R32G32_FLOAT` random-rotation noise texture
* Persistently-mapped CBV upload buffer with view/proj/invProj matrices + kernel + params
* Methods: `createResources()`, `resize()`, `render()`, `transitionResource()` (public static)
* UI params: `enabled`, `radius`, `bias`, `kernelSize`

## ShadowRenderer (`shadow.ixx` + `shadow.cpp`)

* `shadowMap` (`R32_TYPELESS`/`D32_FLOAT`, 2048²), `dsvHeap`, `pso`
* Public config: `enabled`, `bias`, `rasterDepthBias`, `rasterSlopeBias`, `rasterBiasClamp`, `lightDistance`, `orthoSize`, `nearPlane`, `farPlane`; `static constexpr mapSize = 2048`
* Methods: `createResources()`, `reloadPSO()`, `computeLightViewProj(vec3 dirLightDir)`, `render()`

## OutlineRenderer (`outline.ixx` + `outline.cpp`)

* Owns `pso` (stencil NOT_EQUAL, no depth write, cull none)
* Methods: `createResources()`, `reloadPSO()`, `render()` (draws outline for hovered/selected entities)

## GizmoState (`gizmo.ixx` + `gizmo.cpp`)

* 3 arrow ECS entities (`Transform + MeshRef + GizmoArrow`) — cylinder+cone mesh along +Y, rotated per axis
* Filtered from shadow/cubemap/normal/scene passes via `Scene::isGizmoDraw`; rendered in dedicated Gizmo Pass
* Materials: emissive R/G/B (emissiveStrength=5) so arrows glow regardless of lighting
* Constant screen-space size: `gizmoScale = distance(entity, camera) * 0.1`
* Hidden via `scale(0)` transform when no entity is selected
* Methods: `init()`, `update()`, `isGizmoEntity()`, `isDragging()`

## ImGuiLayer (`imgui_layer.ixx` + `imgui_layer.cpp`)

* SRV descriptor heap for ImGui
* Fonts: Roboto-Medium.ttf (text) + MaterialIcons-Regular.ttf (icons, merged via `MergeMode`). Icon glyphs in PUA range (U+E000–U+F8FF).
* Methods: `init()`, `shutdown()`, `styleColorsDracula()`
* Note: `renderImGui()` is in `src/application/ui.cpp` (app-specific UI)

## Application class (split across 10 files in `src/application/`)

* `application.cpp` — constructor, destructor, `update()`, helpers
* `render.cpp` — `render()` — shadow, cubemap, G-buffer, SSAO, scene, gizmo, grid, outline, ID, billboards, bloom, imgui, present
* `ui.cpp` — `renderImGui()` orchestrator (calls modular UI components)
* `ui_menu.cpp` — `uiMenuBar()` with native file dialogs for scenes/models/scripts
* `ui_inspector.cpp` — `uiInspector()` entity property editing
* `ui_metrics.cpp` — `uiMetrics()` performance and ECS statistics
* `ui_overlay.cpp` — `uiOverlay()` tooltips and scene title/desc
* `ui_util.cpp` — `openNativeFileDialog()` Win32 Shell API wrapper
* `setup.cpp` — `loadContent()`, `createScenePSO()`, `createGBufferPSO()`, `createCubemapResources()`, `onResize()`
* `scene.cpp` — `extractSceneData()`, `applySceneData()` — scene file serialization

Thin orchestrator — owns the render loop, swap chain, scene PSO, and input:

* **Swap chain**: triple-buffered, `R8G8B8A8_UNORM`.
* **Root signature**: 8 root params — \[0\] CBV (b0, PerFrameCB), \[1\] CBV (b1, PerPassCB), \[2\] 1 root constant (b2, drawIndex), \[3\] 4 root constants (b3, outline params), \[4\] SRV table (t0, PerObjectData), \[5\] SRV table (t1, shadow map), \[6\] SRV table (t2, cubemap), \[7\] SRV table (t3, SSAO). Static samplers: s0 (shadow comparison PCF), s1 (cubemap linear).
* **Per-frame data split**: `PerFrameCB` (lights, shadows, fog), `PerPassCB` (viewProj, prevViewProj, cameraPos — 256-byte slots, array of 16 per frame), `PerObjectData` (model, prevModel, material — structured buffer at t0). `PerPassCB` slots: 0=main, 1=shadow, 2-7=cubemap faces, 8=G-buffer, 9=ID, 10=grid.
* **Shader hot reload**: polls `.hlsl` timestamps every 0.5s, launches DXC async. Non-blocking — `poll()` checks completion each frame. If DXC fails, old bytecode is preserved.
* **GPU instancing**: `InstanceGroup` component stores N transforms and per-instance material overrides. `totalSlots` = regular entity count + sum of all instance group sizes; shadow/cubemap draw data stored at offsets `totalSlots` and `2*totalSlots`.

## Rendering Pipeline

```
update()  →  render()
              └─ RenderGraph::execute()
                  ├─ Shadow pass      (depth-only to 2048² shadow map)
                  ├─ Cubemap pass     (6-face env map, non-reflective only)
                  ├─ G-Buffer pass    (Normal/Albedo/Material/Motion → 4 MRTs, motion vectors)
                  ├─ Occlusion query   (binary query around camera-visible G-Buffer draws)
                  ├─ SSAO pass        (reads GBuffer Normal+depth → R8 ssaoRT)
                  ├─ Scene pass       (HDR RT, samples shadow+cubemap+SSAO)
                  ├─ Grid pass        (infinite Y=0 grid, alpha-blended, depth-tested)
                  ├─ Outline pass     (silhouette for hovered/selected)
                  ├─ ID pass          (R32_UINT RT, entity index per pixel)
                  ├─ Billboards pass  (light sprite rendering)
                  ├─ Gizmo pass       (translation arrows, renders on top)
                  ├─ Bloom pass       (Prefilter → Downsample → Upsample → Composite)
                  ├─ ImGui pass       (UI overlay)
                  └─ Present pass     (Transition backbuffer to PRESENT state)
```

**Occlusion culling**: D3D12 binary occlusion query heap + readback buffer. G-Buffer pass wraps each draw in a binary query. Scene/G-Buffer/ID passes use filtered `visibleSceneDrawCmds`; shadow/cubemap passes use the full frustum-visible list. `occlusionRefreshInterval` is 8 frames.

**Shadow mapping**: 2048×2048 `R32_TYPELESS`/`D32_FLOAT` depth texture. Orthographic projection from directional light. 3×3 PCF via `SampleCmpLevelZero`. Shadow PSO uses front-face culling + depth bias.

**Cubemap reflections**: Dynamic environment cubemap (`R11G11B10_FLOAT`, configurable resolution, default 128). Rendered from the first reflective entity's position. Materials with `reflective=true` sample via `reflect(-V, N)`, weighted by Fresnel and inverse roughness.

**SSAO**: Normal pre-pass → world-space normals RT; depth transitions DEPTH_WRITE → PIXEL_SHADER_RESOURCE for SSAO read, then back. SSAO shader reconstructs view-space position from depth + invProj, samples 32-point hemisphere kernel. Blur pass applies 3×3 box filter.

**Bloom**: 5-mip chain — prefilter (Karis average, soft threshold), 4× downsample, 4× upsample (tent filter, additive blend).

**Tonemappers** (selectable in UI): ACES Filmic, AgX, AgX Punchy, Gran Turismo / Uchimura, PBR Neutral.

**Rayleigh sky + clouds**: Shared implementation in `src/sky.hlsli`. `rayleighSky(viewDir, sunDir, time)` computes Rayleigh scattering + procedural FBM clouds (5 octaves, spherical projection, time-animated drift). HDR RT cleared to black; composite pass detects background via `sceneLum < 0.001`.

**Infinite grid**: `grid_vs.hlsl` + `grid_ps.hlsl` with own root signature (`gridRootSig`). Fullscreen triangle, unprojects near/far via `InvViewProj`. Ray-intersects Y=0 plane with `fwidth`-based anti-aliasing. X axis blue, Z axis red. Alpha-blended, distance fade (80m), depth-tested.

**Ocean fog**: Height-based fog in `pixel_shader.hlsl`. Thickens exponentially below `FogStartY`. Parameters stored in `SceneConstantBuffer`.

## PBR / BSRDF shader (`src/shaders/pixel_shader.hlsl`)

Cook-Torrance BRDF:

* **NDF**: GGX / Trowbridge-Reitz
* **Geometry**: Smith + Schlick-GGX
* **Fresnel**: Schlick approximation
* Up to 8 animated point lights with scaled inverse-square attenuation.
* 1 directional light (shadow-casting).
