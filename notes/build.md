# Build

* `VCPKG_ROOT` should be assumed to be set correctly
* If there is some external issue with the build environment that is not part of the task itself, wait, then retry the build. If it still fails, **STOP** and wait for user input, stating there is an issue with the build environment.

```bash
# Configure (Ninja Multi-Config, Clang 22 from LLVM)
cmake --preset windows-clang

# Build (Debug, Release builds)
cmake --build build --config Debug
cmake --build build --config Release

# Test (loads test scene: WARP adapter, 10 frames, screenshot, exit)
./build/Debug/main.exe resources/scenes/test.json

# Unit tests (doctest + CTest, includes lua_scripting_tests)
ctest --test-dir build -C Debug --output-on-failure
```

**NOTE:** Sometimes the build will fail with a file lock issue ("user-mapped section open"). When this happens, stop execution immediately. The issue is likely due to the language server in the open editor. The user must shut it down before you can continue.

## Build System Principles

* Use Modern CMake (use targets)

## Toolchain notes

* **Compiler**: `clang++` (v22 or newer). Do NOT use Git's clang (v18 — too old for VS 18 STL).
* **vcpkg**: `$VCPKG_ROOT` (x64-windows-static triplet).
* **Presets**: `windows-clang` (primary), `windows-msvc` (do not use!).
* Shaders compiled via DXC to `.cso` headers at build time.

## Rules (IMPORTANT)

**Before working on a task / during planning phase:**

* Determine the best place to added new functionality. If it's a new feature, try making a new module file for it
* If working on a new feature, use the web search tool to perform initial research on the topic.
* If asked to create a new feature/mode/option for objects, properties, or relationships, consider how to integrate it with the existing ECS system.

**After finishing every task:**

* build then run test scene and inspect `screenshot.png`
  * `./build/Debug/main.exe resources/scenes/test.json`
  * read `screenshot.png` with the Read tool, and visually verify the result looks correct before reporting done
* `git pull`
* Update `AGENTS.md` to reflect any new or modified architecture, modules, rendering pipeline steps, UI panels, key patterns, or dependencies. Keep it accurate and current.
* Run `clang-format` on all source and header files
* If any of the changed files in the working tree are longer than 1100 lines, split the file
  * Create new modules (.ixx) / source files (.cpp) as needed
