# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Requires VCPKG_ROOT env var pointing to vcpkg installation
export VCPKG_ROOT="C:/Users/ben/Documents/vcpkg"

# Configure (Ninja Multi-Config + Clang)
cmake --preset windows-clang

# Build Debug / Release
cmake --build build --config Debug
cmake --build build --config Release

# Run
./build/Debug/main.exe
```

Presets: `windows-clang` (primary), `windows-msvc` (fallback). Shaders compiled via DXC to .cso at build time.

## Architecture

This is a from-scratch DirectX 12 rendering engine (C++23, Clang) currently rendering a colored cube with orbit camera controls.

**Core classes:**

- **Window** (singleton) — HWND creation, DXGI adapter selection, ID3D12Device2 creation, tearing support detection. Must be initialized before Application.
- **Application** — Owns the render loop: swap chain (triple-buffered), PSO, root signature, vertex/index buffers, depth buffer. `update()` handles input/camera, `render()` draws and presents.
- **CommandQueue** — Wraps ID3D12CommandQueue with fence-based CPU/GPU synchronization and command allocator pooling. Allocators are recycled when their fence value completes.
- **Camera / OrbitCamera** — View/projection matrix generation. OrbitCamera uses spherical coordinates (yaw, pitch, radius) driven by mouse input.
- **UploadBuffer** — Linear page-based allocator for GPU upload heaps. Used for one-time CPU-to-GPU data transfers.
- **DescriptorAllocator** (WIP) — Free-list descriptor allocation from CPU-visible heaps with RAII handles.

**Math/utility types** (`common.hpp`): `vec2/3/4` (XMVECTOR wrappers), `mat4` (XMMATRIX wrapper) with operator overloads. Custom literals: `_deg` (degrees->radians), `_KB`, `_MB`. `align()` for power-of-2 alignment. `chkDX()` macro throws on HRESULT failure.

**Input** uses gainput library (submodule in `lib/gainput/`) for keyboard, mouse, and raw input.

**Rendering pipeline:** Root signature uses inline 32-bit constants (MVP matrix at b0). Single draw call of 36 indices (cube). Depth testing with D32_FLOAT.

## Code Style

- Chromium-based clang-format with 4-space indent, custom brace wrapping, 100 col limit
- clang-tidy enabled (bugprone, modernize, performance, readability checks)
- Windows subsystem app (no console) — use spdlog for logging (stderr + file)
- DX12 debug layer enabled in Debug builds
