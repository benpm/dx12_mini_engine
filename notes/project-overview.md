# Project Overview

## Purpose

DX12 Mini Engine is a Windows-only DirectX 12 renderer and editor-oriented sandbox focused on modern graphics experimentation with C++23 modules.

## Core Characteristics

- API: Direct3D 12
- Language: C++23 (modules + implementation .cpp files)
- Platform: Windows
- Build: CMake + Ninja Multi-Config + LLVM/Clang (primary)
- ECS: flecs
- UI: ImGui (Win32 + DX12 backend)
- Input: gainput
- Logging: spdlog
- Serialization: glaze JSON
- Scripting: Lua 5.4

## What The Engine Currently Includes

- Forward/PBR scene rendering with Cook-Torrance shading
- Shadow mapping for directional light
- SSAO pass (with blur)
- Deferred G-Buffer pass used by SSAO and motion vectors
- Bloom post-processing and tone mapping options
- Runtime cubemap reflections for reflective materials
- Object picking via ID pass + readback
- Entity outline rendering (hover/selection)
- Translation gizmo for editor manipulation
- Terrain generation (Perlin noise)
- Lua scripting system for entity behaviors and editor actions
- Render graph for pass orchestration and resource state transitions

## Primary Runtime Data Paths

- Scene and renderer state is orchestrated by Application
- ECS world and draw data are owned by Scene
- Rendering is assembled each frame through RenderGraph
- Scene/config JSON are loaded before runtime loop and then applied to Application

## Important Top-Level Files

- `README.md`: high-level intro and quick troubleshooting
- `AGENTS.md`: most complete architecture + workflow reference
- `CMakeLists.txt`: build graph, dependencies, targets
- `CMakePresets.json`: compiler presets and build configurations
- `config.json`: runtime and UI defaults used on startup
- `TODO.md`: active and completed implementation items

## Entry Point

- `src/main.cpp`:
  - Parses CLI args (`--dump-config`, optional scene path)
  - Loads/merges config
  - Loads scene file for runtime flags (WARP, hide window, etc.)
  - Initializes Window + Application
  - Runs Win32 message loop and per-frame update/render
