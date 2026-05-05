# Perf & Cleanup Pass — 2026-05-03

A whole-codebase review surfaced ~20 candidate findings (perf, software design,
GPU pipeline correctness). This note documents the changes that landed, the
findings that were deferred, and the rationale. Extended on the same date
with a second round of small wins (sections 3-5).

---

## What landed in this pass

### 1. Per-frame draw-command vectors → Application members

`sceneDrawCmds`, `visibleSceneDrawCmds`, `gizmoDrawCmds`, and
`nonReflectiveIndices` are now reused across frames as members of
`Application`. They're `clear()`-ed and re-populated each frame; capacity
grows once and stays, eliminating four `std::vector` heap allocations per
frame.

- `src/modules/application.ixx:165-172` — added members.
- `src/application/render.cpp:118-130` (scene/gizmo split, occlusion-cull
  filter) — now uses `clear()` + `reserve()`.
- `src/application/render.cpp:300-320` (cubemap non-reflective set) — moved
  from a per-callback local vector into the same shared buffer.

### 2. Redundant `SetGraphicsRootSignature` calls removed

`gfx::ICommandList::bindPipeline` already binds the matching root signature
(set in the recent debugging-pass fix at `src/gfx/d3d12_command.cpp:317`). All
`cmd->SetGraphicsRootSignature(bindlessRootSigNative())` calls that immediately
follow a `bindPipeline()` were dead. Six removed:

- Cubemap, GBuffer, Scene, Gizmo, ID passes (`render.cpp`).
- Outline (`outline.cpp:128`), SSAO + Blur (`ssao.cpp:335` / `:384`).

The shadow pass keeps its explicit `SetGraphicsRootSignature` because
`bindPerFrameAndPass` runs *before* `shadow.render()` (which is where
`bindPipeline` is called) and CBV bindings need an RS already on the cmd list.
A comment at `render.cpp:275-280` explains why.

Bloom (`bloom.cpp:244`) and billboard (`billboard.cpp:182`) keep their
explicit calls because they use raw `cmd->SetPipelineState`, not
`cmdRef.bindPipeline`. Migrating those PSOs to `gfx::PipelineHandle` is a
follow-up task.

### 3. Bindless SRV index lookups hoisted out of per-draw loops

`gfxDevice->bindlessSrvIndex()` and friends are pure-function lookups, but
calling them inside a draw loop runs the lookup `N_draws` times for values
that are constant across the entire pass (the per-object buffer SRV, the
shadow map, the cubemap, the SSAO blur RT, the shadow/env sampler indices).

All five draw loops in `render.cpp` now build a `BindlessIndices base` once
before entering the loop and only mutate `bi.drawIndex` per draw:

- Cubemap face inner loop (`render.cpp:421`).
- G-Buffer pass (`render.cpp:484`).
- Scene pass (`render.cpp:566`).
- Gizmo pass (`render.cpp:611`).
- ID pass (`render.cpp:748`).

For `~10` entities × `5` per-draw lookups, this is ~50 lookups → 5
lookups per frame.

### 4. `Scene::isGizmoDraw` populated lazily

Was: `populateDrawCommands` did one final pass after building draw cmds —
`isGizmoDraw.resize(N); for (i) isGizmoDraw[i] = drawIndexToEntity[i].has<GizmoArrow>();`
Now: `clear()` once at the start, `push_back(e.has<GizmoArrow>())` inline
next to `drawIndexToEntity.push_back(e)` (and `push_back(false)` in the
instance-group branch since instance groups are never gizmo arrows). One
fewer linear pass and one fewer ECS query per frame.

### 5. `BindlessHeap::free` debug-only double-free guard

Added an `#ifndef NDEBUG` `assert(std::find(...) == freeList.end())` to
both `free()` and `freeBatch()`. Production path is unchanged. Catches the
theoretical double-destroy (`destroy(h)` called twice on a still-valid
handle) at the source rather than as silent descriptor reuse.

### 6. `beginPass` helper extracted

In `render.cpp`, every passes that uses `pipelineState` / `gbufferPSO` /
`picker.pso` repeated this 4-line pattern:

```
cmdRef.bindPipeline(pso);
bindSharedGeometry(cmd);
bindSceneHeapAndObjects(cmd);
bindPerFrameAndPass(cmd, perPassAddr);
```

Consolidated into a single `beginPass(cmdRef, pso, perPassAddr)` lambda
(`render.cpp:204-217`). Used by GBuffer, Scene, Gizmo, and ID passes (4 call
sites, ~20 lines deleted).

The Cubemap pass intentionally still inlines the binds because its per-face
loop sets the per-pass CBV inside the loop, not once up-front — it doesn't
fit the `beginPass` shape.

---

## Verification

- Clean build, 39/39 unit tests pass.
- `test.json` (WARP, debug layer auto-on): exits at frame 10 with 0 D3D12
  errors. Screenshot identical to the committed reference.
- `default.json` and `empty.json` run for ~6s with debug-layer-forced-on:
  0 errors.

---

## Deferred findings (explicitly left for later)

The full review found ~20 issues. The ones below are real but were skipped
this pass — they're a worse effort/value ratio or deserve their own
focused PR.

### Performance — medium impact

- **Cubemap face loop rebuilds the 6 face viewProjs and re-copies object
  data per frame even when the reflective entity hasn't moved.** Cache
  by reflective-position hash. Skipped because cubemap-disabled is the
  common case (test.json) and the runtime impact is negligible there.

### Software design — high impact, large blast radius

- **Application is a god class** (~30 fields, multiple distinct concerns).
  Extracting `EditorState`, `RenderLoopState`, `SceneEditQueue`,
  `PendingActions` is the right move but it touches every UI / scene /
  render file. Earns its own PR.
- **`pending*` flag soup → typed `EditorAction` queue.** Same scope.
- **Subsystems hold raw `gfx::IDevice* devForDestroy`.** Today device
  lifetime > all subsystems by construction; documented invariant is
  enough until the engine ever supports device recreation. No fix needed
  short-term.
- **`Application::render()` is ~900 lines, `update()` ~460.** Extract
  `renderShadowPass()`, `renderCubemapPass()`, etc. This was one of the
  findings — partial mitigation via `beginPass`, but the full split is a
  separate refactor.

### GPU pipeline / render graph — correctness

- **Bloom mip chain + SSAO internal RTs do their own `transitionResource`
  calls outside the render graph.** Currently safe (those mips/RTs aren't
  read elsewhere), but fragile: if a future pass ever samples a bloom mip,
  the graph won't know the actual state. Two ways to fix: (a) declare
  internal RTs as graph resources; (b) add a post-pass
  `RenderGraph::updateResourceState(handle, actualState)` hook. Skipped —
  no current bug.
- **Render graph has no compute-pass support** (no `readBuffer` /
  `writeBuffer` / `UnorderedAccess` state in transitions). Would unblock
  TAA, GPU culling, indirect draws, etc. This is a Tier-B candidate from
  the existing extension plan.
- **No pass culling** in the render graph. Liveness sweep is ~1 day of
  work. Low priority — current pass set is small.
- **`Bloom`/`Billboard` PSOs are raw `ComPtr<ID3D12PipelineState>`** with
  raw `cmd->SetPipelineState` calls. Migrating to `gfx::PipelineHandle`
  would let them benefit from `bindPipeline`'s auto-root-sig binding too.

### Highest-leverage missing features (review list)

Prioritised by learning-value-per-implementation-cost for a DX12 learning
project:

1. **Compute pass support in render graph** (~1 day) — unblocks (2-3).
2. **TAA** (~3 days) — motion vectors already in `MotionRT`; just need
   history buffer + reprojection + jitter.
3. **GPU-driven culling** (~3-5 days) — replaces CPU per-entity frustum
   cull with a compute dispatch over an indirect-draw arg buffer. Teaches
   `ExecuteIndirect`.
4. **Shadow cascades** (~3 days) — replace single-shadow-map with 4-split
   CSM.
5. **HDR display output** (~1 day) — engine renders to `R11G11B10_FLOAT`
   already; add `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020` swap-chain
   detection.

Skipped from the review's findings list as overhyped: theoretical
"double-render race" (`render()` is never called twice without `Present`),
"stale-frame protection in upload buffers" (the existing
`frameFenceValues[curBackBufIdx]` wait at `render.cpp:40-44` is correct).

---

## File-level cheat sheet for next time

| Want to add a new pass? | Use `beginPass(cmdRef, pso, perPassAddr)` then `setRenderTargets`. |
| Want a new draw-cmd buffer? | Add it as an `Application` member, not a per-frame local. |
| Want a new compute pass? | Render graph doesn't support it yet — Tier-B ext required first. |
| Want to expose a subsystem PSO? | Use `gfx::PipelineHandle` so `bindPipeline` auto-binds the root sig. |
