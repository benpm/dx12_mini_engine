## Plan: DX12 and C++23 Optimization Audit

The recommended approach is to prioritize GPU/CPU synchronization fixes and per-frame overhead reductions first, then consolidate duplicated rendering infrastructure, then apply targeted C++23 modernization for safer APIs and lower maintenance cost. This is based on direct code inspection plus Direct3D 12 guidance (barrier batching, heap-switch minimization, avoiding blocking waits) and C++ Core Guidelines (RAII, ownership clarity, strong typing).

**Steps**

1. Phase 1 - Measure and baseline performance signals before refactoring. Capture CPU frame time, fence-wait time, and pass-level timings via Tracy zones already present in render loop. Add scoped timing around current end-of-frame wait and picker readback to quantify stall cost. This phase blocks Phase 2 because it defines success criteria.
2. Phase 2 - Remove avoidable synchronization stalls. Replace unconditional end-of-frame wait with completion check + conditional wait in render path; redesign object picking readback to avoid synchronous Map dependency (deferred readback with fence-checked ring buffering and read-on-ready behavior). This can run in parallel with Step 3 after shared conventions are set.
3. Phase 2 - Eliminate startup/runtime blocking waits in content paths by introducing deferred upload-resource lifetime management keyed by fence values, so GLTF/terrain uploads do not force immediate CPU waits during content updates. Depends on command-queue fence utility updates from Step 2.
4. Phase 3 - Reduce render-loop CPU overhead and duplication. Extract common pass setup helper (descriptor heaps, VB/IB, per-frame/per-pass CB binding), replace magic descriptor and pass slot constants with named centralized definitions, and collapse repeated per-pass binding boilerplate across shadow/normal/scene/id paths. This can run in parallel with Step 5.
5. Phase 3 - Improve render graph efficiency and correctness boundaries. Keep barrier batching, but introduce transition suppression and optional split-barrier strategy for long-latency transitions; add optional state-cache hints for stable resources; reserve per-pass vectors to avoid repeated allocations; evaluate whether static pass graph construction can replace per-frame dynamic std::function/lambda pass rebuilds.
6. Phase 4 - Consolidate descriptor and resource creation infrastructure. Introduce centralized descriptor-slot registry and descriptor helper/factory utilities for RTV/DSV/SRV creation to remove duplicated heap/view boilerplate across bloom, SSAO, shadow, cubemap, and picker. Depends on Step 4 naming conventions.
7. Phase 4 - Simplify subsystem APIs with a shared render context struct to replace long parameter lists and reduce call-site fragility. Start with Outline renderer signature reduction, then propagate to shadow/billboard/bloom as feasible. Parallel with Step 6 after slot conventions are fixed.
8. Phase 5 - Apply C++23 safety and maintainability refactors. Introduce constexpr alignment helpers for CB offsets, strong types for resource/pass handles and descriptor slots, and std::span for non-owning array-like parameters; normalize error-handling strategy around chkDX/explicit recoverable-return paths. Depends on Phase 3+4 structure stabilization.
9. Phase 6 - Evaluate advanced DX12 opportunities only after above wins are validated: async compute queue for SSAO/bloom overlap, transient resource aliasing for bloom/SSAO intermediates, and optional allocator integration strategy (for example D3D12MA) if memory management complexity grows.

**Relevant files**

- `src/application_render.cpp` — end-of-frame wait behavior, per-frame pass setup duplication, cubemap loop descriptor-size queries, per-pass constant-buffer indexing.
- `src/object_picking.cpp` — synchronous readback Map path and picker copy/read scheduling.
- `src/scene.cpp` — content upload waits in load paths, per-frame buffer alignment math, upload temporary lifetime handling.
- `src/application_setup.cpp` — resize path flush sequencing and repeated resource recreation orchestration.
- `src/command_queue.cpp` — fence APIs, queue wait behavior, allocator lifecycle and potential utility extension points.
- `src/render_graph.cpp` — transition emission strategy, per-pass dynamic allocation behavior, execution-time overhead.
- `src/modules/render_graph`.ixx — handle typing, public render-graph API shape, pass/build contracts.
- `src/bloom.cpp` — descriptor heap creation duplication, repeated descriptor increment-size retrieval, transition helper patterns.
- `src/ssao.cpp` — RTV/SRV heap setup duplication, output slot placement conventions, pass resource layout.
- `src/shadow.cpp` — DSV/SRV creation pattern duplication and pass binding behavior.
- `src/outline.cpp` — oversized render function signature and repetitive binding logic.
- `src/modules/application`.ixx — shared state ownership and new context/slot type definitions.

**Verification**

1. Build and run baseline scene before and after each phase using existing project commands and collect Tracy captures with frame-time distributions.
2. Validate no behavioral regressions: shadow maps, SSAO, bloom, picking, cubemap reflections, outline rendering, and scene load/save.
3. Track stall reduction specifically: measure time spent in end-of-frame wait and picker readback path before/after.
4. Confirm descriptor-slot refactors by adding assertions/static checks that slot ranges do not overlap and all root-parameter bindings map correctly.
5. Execute repository-required validation flow after modifications: build Debug, run test scene, inspect screenshot output for visual correctness.

**Decisions**

- Included scope: actionable optimization and simplification roadmap with dependency-aware phases and risk ordering.
- Excluded scope: immediate deep architectural rewrite (full pass-class hierarchy or complete render-graph replacement) before low-risk synchronization and duplication wins are landed.
- Ordering decision: prioritize synchronization and hot-path overhead first because these provide measurable benefit with minimal shader/feature risk.

**Further Considerations**

1. Decide whether async compute is a near-term target or deferred: Option A keep single direct queue until CPU-side overhead is reduced; Option B prototype SSAO async path after Phase 3; Option C implement full graphics+compute scheduling now (higher risk).
2. Decide whether descriptor management should be lightweight constants+helpers or a full allocator module: Option A constants + helper functions first; Option B full DescriptorHeapManager immediately.
3. Decide whether render graph should stay dynamic-per-frame or become mostly static: Option A static pass registration with runtime enable flags; Option B keep dynamic API and only optimize internal allocations/transitions.
