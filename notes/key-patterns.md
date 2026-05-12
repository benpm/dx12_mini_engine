# Key Patterns / Pitfalls

## Physics — body spawn budgets and Jolt limits

The Jolt backend (`src/physics_jolt.cpp`) has four critical compile-time caps that need to be scaled with body count:

- **`maxBodies`** (16384) — total bodies, dynamic + static. Going over makes `BodyInterface::CreateAndAddBody` return invalid IDs silently.
- **`maxBodyPairs`** (65536) — broadphase pair-overlap queue. When tight clusters of bodies have overlapping AABBs, this fills up fast; overflow doesn't crash but degrades performance as Jolt falls back to inline narrowphase.
- **`maxContactConstraints`** (16384) — solver work. A stack of N bodies in contact needs O(N) constraints; under-sizing causes simulation instability.
- **`TempAllocator` size** (64 MB) — per-step scratch. **Default 10 MB OOMs around ~500 dynamic bodies under the physics_stress workload.** When this fails it logs `[jolt] TempAllocator: Out of memory` and the step silently produces garbage / no work. **First thing to check when bodies disappear at scale.**

## Physics — spawn-overlap explosion

Dynamic bodies whose collision shapes overlap at spawn frame trigger Jolt's penetration resolver, which applies extreme impulses to separate them — bodies fly off in apparently random directions and end up off-screen. Symptoms: entities visible without physics, invisible the moment `RigidBody` is attached. **Always confirm spawn-grid spacing > 2 × (mesh half-extent × scale)** for all participating meshes. For glTF primitives, `resources/models/*.glb` extents are usually ±1 m, but `torus.glb` is ±1.35 m and `teapot` (built-in) is ±3 m — see `physics_stress.lua` for the filter pattern that excludes them.

## Physics — degenerate convex hulls

`JPH::ConvexHullShape` accepts ≥ 4 points but produces undefined behaviour if all points are coplanar (zero-volume hull → zero inertia tensor → bodies teleport). `plane.glb`'s flat geometry hits this. Filter such meshes before passing positions to `engine.add_convex_hull_body` / `add_mesh_collider`.

## Lua entity-destroy and GizmoState

`engine.destroy_entity` on an entity that has a `GizmoArrow` component leaves `GizmoState::arrows[]` holding dead flecs handles. The next `gizmo.update()` access segfaults. `delete_all.lua` and `physics_stress.lua` both document this — if a script needs to wipe scene content, filter out gizmo arrows or rebuild the gizmo afterwards via `gizmo.init()`.

## GPU upload buffer lifetime

Intermediate upload heaps from `UpdateSubresources` **must** stay alive until after `cmdQueue.waitForFenceVal()`. Uploads are tracked in `Scene::pendingUploads` as fence-keyed batches (`trackUploadBatch` / `retireCompletedUploads`) and retired in `Application::update()` once `CommandQueue::completedFenceValue()` has advanced.

## Frame pacing and fence waits

`Application::render()` waits at frame start only when the current back buffer's fence is still in flight (`frameFenceValues[curBackBufIdx]`). This removes the unconditional end-of-frame CPU stall while still protecting swap-chain resource reuse.

## Picker readback ring buffering

`ObjectPicker` uses a 3-slot readback ring (`readbackSlots`) with per-slot fence values. `copyPickedPixel()` writes into the next slot, `setPendingReadbackFence()` tags the submitted slot, and `readPickResult()` consumes the newest completed slot.

## RenderGraph external resource lifetime

`RenderGraph::reset()` clears `resources` and `externalResources` each frame. Imported textures (especially the current swap-chain back buffer) must be re-imported every frame.

## ResizeBuffers requires render-graph reset first

`IDXGISwapChain::ResizeBuffers()` can fail with `DXGI_ERROR_INVALID_CALL` if old back buffers are still referenced. Before resetting `backBuffers[]` in `Application::onResize()`, call `renderGraph.reset()` (after GPU flush).

## Headless runtime picking

When `runtime.hideWindow` is true (automation scenes), the ID picking pass is skipped in `Application::render()`.

## SceneConstantBuffer layout

Must match `SceneCB` in both HLSL shaders exactly. Current fields: `model`, `viewProj`, `cameraPos`, `ambientColor`, `lightPos[8]`, `lightColor[8]`, `albedo`, roughness/metallic/emissiveStrength/reflective, `emissive`, `dirLightDir`, `dirLightColor`, `lightViewProj`, shadowBias/shadowMapTexelSize/fogStartY/fogDensity, `fogColor`.

## XMMATRIX alignment

`SceneConstantBuffer` contains `XMMATRIX` members — declare on the stack with `alignas(16)`:

```cpp
alignas(16) SceneConstantBuffer scb = {};
```

## Error reporting helpers

Use `chkDX(...)` for HRESULT-returning calls and `throwLastWin32Error(...)` for Win32 APIs. `chkDX` throws `std::runtime_error` with HRESULT hex + decoded message + source location.

## No GPU ops inside renderImGui

`renderImGui` is called mid-frame with an open command list. Never call `clearScene()`, `flush()`, or `loadGltf()` directly inside it — use deferred flags `pendingGltfPath` / `pendingResetToTeapot`, processed at the start of `update()`. Similarly, never mutate ECS entities inside `renderImGui`.

## Tracy D3D12 callstack depth gotcha

With Tracy v0.13.1, keep GPU profiling wrapper on `TracyD3D12NamedZoneS(..., depth=1, ...)` (see `include/profiling.h`).

## Fullscreen toggles are deferred

Queue UI fullscreen requests via `pendingFullscreenChange` / `pendingFullscreenValue` and apply at the start of `update()`. Resources recreated in `onResize()` must be created in their render-graph-expected initial states (e.g. HDR RT in `PIXEL_SHADER_RESOURCE`).

## ImGui input capture

Camera rotation, zoom, and entity picking are gated by `ImGui::GetIO().WantCaptureMouse` in `update()`.

## PointLight entities survive clearScene()

`clearScene()` removes entities via `delete_with<MeshRef>()` and `delete_with<InstanceGroup>()`. `PointLight` entities have no `MeshRef`, so they persist across scene resets.

## Gizmo entities must be recreated after scene clears

`clearScene()` deletes gizmo arrows. Any flow that clears/reloads scene content must call `gizmo.init(scene, device.Get(), cmdQueue)` afterward.

## TerrainEntity tag and positionY

The terrain entity is tagged with `TerrainEntity` at creation. `applySceneData()` queries for `TerrainEntity` entities to update their `Transform.world = translate(0, positionY, 0)`. Must be guarded by `contentLoaded`.

## tinygltf in a separate TU

`TINYGLTF_IMPLEMENTATION` and `STB_IMAGE_IMPLEMENTATION` must be in `src/gltf_impl.cpp` (not in `application.cpp`) to avoid `stb_image` / `Windows.h` macro conflicts.

## glTF matrix convention

glTF matrices are column-major. When loading into `XMMATRIX` (row-major), transpose: put glTF column *i* as XMMATRIX row *i*. For TRS nodes use `S * R * T` order in DirectXMath.

## D3D12 debug layer gated on debugger

`enableDebugging()` in `window.cpp` checks `IsDebuggerPresent()` before enabling the D3D12 debug layer. Without a debugger, debug layer breakpoints crash the process.

## Window callback gating (inMessageLoop)

`Window::inMessageLoop` (default `false`) gates `WM_SIZE` and `WM_PAINT` callbacks in `WndProc`. Set to `true` in `main.cpp` just before `ShowWindow`. Never call `UpdateWindow()`.

## SEH exception filter

`main.cpp` installs `SetUnhandledExceptionFilter(sehFilter)` to log unhandled SEH exceptions (code + address) via spdlog before termination.

## startFullscreen config

`ConfigData::startFullscreen` (default `false`) applied in `applyConfig()` via `pendingFullscreenChange`/`pendingFullscreenValue`.

## Depth buffer must be created in loadContent

For hidden-window test scenes (`runtime.hideWindow = true`), `ShowWindow` is skipped so `WM_SIZE` never fires. Call `resizeDepthBuffer(clientWidth, clientHeight)` at the end of `loadContent()`.

## LOD selection in populateDrawCommands

`LodMesh` component checked inline inside the `drawQuery.each()` lambda using `e.try_get<LodMesh>()`. LOD levels sorted ascending by `distanceThreshold`; first level whose threshold exceeds camera distance is selected. Frustum culling uses `BoundingVolume::sphere` when present.
