# Code Style

* clang-format: Chromium base, 4-space indent, 100-col limit.
* clang-tidy: bugprone, modernize, performance, readability checks.
* Windows subsystem (no console) — use `spdlog` for all logging.
* DX12 debug layer enabled in Debug builds (only when debugger is attached — see Key Patterns).

# Profiling

Tracy Profiler v0.13.1 integrated for CPU and GPU instrumentation. `TRACY_ON_DEMAND` is set so there is no overhead when the viewer is not connected.

* **Header**: `include/profiling.h` — include ONLY from `.cpp` files, never from `.ixx` modules
* **GPU context**: `g_tracyD3d12Ctx` (file-static in `src/application/application.cpp`, `extern`'d in `render.cpp` and `setup.cpp`); created in `loadContent()`, destroyed in `~Application()` after `flush()`
* **CPU zones**: `PROFILE_ZONE()` / `PROFILE_ZONE_NAMED(name)` — in `update()` and each render pass
* **GPU zones**: `PROFILE_GPU_ZONE(ctx, cmdList.Get(), "name")` — Shadow, Normal Pre-pass+SSAO, Scene, Bloom
* **Frame boundary**: `PROFILE_FRAME_MARK` after `Present` at end of `render()`
* **Viewer**: download Tracy v0.13.1 from GitHub releases; connect to the running engine on localhost
