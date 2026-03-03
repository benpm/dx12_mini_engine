# DX12 Experiments Project Context

## Project Overview
This is a C++23 project focused on DirectX 12 experiments. It is built using modern CMake, utilizing `vcpkg` for dependency management and `CMakePresets.json` for streamlined configuration and building. 

### Key Technologies
*   **Language:** C++23
*   **Graphics API:** DirectX 12
*   **Shader Compiler:** DirectX Shader Compiler (DXC) compiling HLSL to `.cso`
*   **Build System:** CMake (v3.20+) with `Ninja Multi-Config`
*   **Package Manager:** vcpkg (Requires `VCPKG_ROOT` environment variable)
*   **Dependencies:**
    *   `directxtk12` & `directxmath` (via vcpkg)
    *   `spdlog` for fast logging (via vcpkg)
    *   `gainput` for input handling (integrated as a local subdirectory `lib/gainput`)

## Directory Structure
*   `src/`: Contains C++ source files (`.cpp`) and HLSL shaders (`.hlsl`).
*   `include/`: Contains C++ header files (`.hpp`, `.h`).
*   `lib/gainput/`: Local vendor directory containing the `gainput` library.
*   `build/`: Default output directory for CMake configurations and builds.

## Building and Running

The build system is defined around CMake Presets. Ensure you have the required dependencies installed via vcpkg (`vcpkg install directxtk12 directxmath spdlog`) and `VCPKG_ROOT` is set in your environment.

### Configuration
Configure the project by selecting a compiler preset (Clang or MSVC).

```bash
# Configure using Clang
cmake --preset windows-clang

# Or configure using MSVC
cmake --preset windows-msvc
```

### Building
Compile the project by invoking the corresponding build preset for your chosen configuration.

```bash
# Build Debug with Clang
cmake --build --preset windows-clang-debug

# Build Release with Clang
cmake --build --preset windows-clang-release

# Build Debug with MSVC
cmake --build --preset windows-msvc-debug
```

### Running
The build produces an executable named `main.exe` and compiled shader objects (`.cso`) in the configuration-specific output directory.

```bash
# Run the debug executable (example)
.\build\Debug\main.exe
```

## Development Conventions

*   **Code Formatting & Linting:** The repository includes `.clang-format` and `.clang-tidy` files. Ensure your IDE or editor runs Clang-Format on save and surfaces Clang-Tidy diagnostics.
*   **Logging:** The project uses `spdlog`. Logging macros or configurations are handled via CMake definitions, setting `SPDLOG_ACTIVE_LEVEL` to `SPDLOG_LEVEL_TRACE` for Debug builds and `SPDLOG_LEVEL_INFO` for Release builds.
*   **Shaders:** HLSL shaders are stored in the `src/` directory alongside source code. The build system automatically compiles them to `.cso` using `dxc.exe` during the build step. Ensure DXC is installed and discoverable (typically via the Windows SDK).
