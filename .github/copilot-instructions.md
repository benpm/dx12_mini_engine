# Copilot instructions for `dx12_mini_engine`

## Build and test commands (Windows)

Use the documented CMake/Ninja flow:

```bash
cmake --preset windows-clang
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Focused test runs:

```bash
ctest --test-dir build -C Debug --output-on-failure -R gfx
ctest --test-dir build -C Debug --output-on-failure -R "gfx: empty command list submit + fence"
```

Formatting (when needed):

```bash
clang-format -i <changed-files>
```

Build blocker rule:
- If build/link fails with `permission denied` or `user-mapped section open` on `build\Debug\main.exe`, stop and report the blocker instead of retrying loops.

## High-level architecture

- Engine is split across C++23 modules in `src/modules/*.ixx`; `Application` orchestrates frame update/render and owns subsystem instances.
- Rendering is coordinated through `rg::RenderGraph` passes (shadow, cubemap, G-buffer, SSAO, scene, outline, ID/picking, billboards, bloom, ImGui, present).
- Graphics backend is a `gfx::` abstraction (`include/gfx.h`, `include/gfx_types.h`) with D3D12 implementation in `src/gfx/`.
- Bindless is the default model: global SRV/UAV heap + sampler heap, with bindless root signature/table usage across render passes.

## Repository-specific conventions

- Use `app_slots::*` constants from `src/modules/gfx.ixx` for root-parameter slots; avoid hardcoded slot indices in rendering code.
- For bindless passes, after `SetDescriptorHeaps(...)`, bind both descriptor heaps (resource + sampler) and set both descriptor tables:
  - `app_slots::bindlessSrvTable`
  - `app_slots::bindlessSamplerTable`
- When changing `include/gfx.h` virtual API, update all dependent surfaces in the same change:
  - D3D12 backend declarations/definitions (`src/gfx/d3d12_internal.h`, `src/gfx/d3d12_*.cpp`)
  - test doubles (notably `tests/gfx_tests.cpp` `MockDevice`)
- Keep GPU/UI mutation boundaries intact: no scene/ECS destructive operations directly inside ImGui rendering paths; defer through update-time flags.

## Reference docs to consult first

- `AGENTS.md`
- `notes/build.md`
- `notes/architecture.md`
- `notes/subsystems.md`
- `notes/key-patterns.md`

## MCP guidance

- Prefer GitHub MCP for repo/PR/issue/workflow investigation instead of manual API calls when the task involves GitHub metadata or CI history.
- For code changes, inspect local files first; use GitHub MCP only when the requested context is remote (PR discussions, workflow logs, issue threads, branch/commit metadata).
