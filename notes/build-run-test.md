# Build, Run, Test

## Environment Assumptions

- `VCPKG_ROOT` is set and points to a valid vcpkg installation.
- Preferred toolchain is LLVM Clang (Windows preset: `windows-clang`).
- Presets use `x64-windows-static` triplet.

## Configure

```bash
cmake --preset windows-clang
```

Alternative preset exists (`windows-msvc`) but primary development flow is clang.

## Build

Debug:

```bash
cmake --build build --config Debug
```

Release:

```bash
cmake --build build --config Release
```

## Run

Default run (uses config default scene path if no CLI scene path is provided):

```bash
./build/Debug/main.exe
```

Run a specific scene:

```bash
./build/Debug/main.exe resources/scenes/default.json
```

Integration/automation scene:

```bash
./build/Debug/main.exe resources/scenes/test.json
```

## CLI Options

- `--dump-config`: writes default `config.json` and exits.
- Positional scene path: loads that scene before entering the runtime loop.

## Test

Run all CTest-registered tests:

```bash
ctest --test-dir build -C Debug --output-on-failure
```

Current unit test sources:

- `tests/math_types_tests.cpp`
- `tests/lua_scripting_tests.cpp`

## Important Build/Runtime Notes

- Shader bytecode headers are generated at build time into `build/`.
- `compile_commands.json` is exported for clangd.
- Some file-lock build failures on Windows can come from external tooling (language server mapping files).
- Debug-layer behavior and startup sequencing are guarded to avoid early crashes when no debugger is attached.

## Dependency Sources

vcpkg dependencies (declared in `vcpkg.json`):

- directxtk12
- directxmath
- spdlog

FetchContent dependencies (declared in `CMakeLists.txt`):

- gainput
- imgui
- tinyobjloader
- tinygltf
- PerlinNoise
- flecs
- glaze
- Lua 5.4 source
- doctest
- Tracy
