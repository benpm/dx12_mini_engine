# Dependencies

| Library | Source | Notes |
|----|----|----|
| directxtk12, directxmath, spdlog | vcpkg (x64-windows-static) |    |
| directx-headers | vcpkg (transitive via directxtk12) | d3dx12 helpers |
| gainput | FetchContent (git hash 2be0a50) | Input |
| imgui v1.92.6 | FetchContent | Win32 + DX12 backend |
| tinyobjloader | FetchContent (git hash afdd3fa) | OBJ loading |
| tinygltf v2.9.5 | FetchContent | GLB/glTF loading |
| PerlinNoise v3.0.0 | FetchContent | Terrain heightmap |
| glaze v5.2.1 | FetchContent | JSON serialization |
| Lua 5.4.7 | FetchContent | Scripting engine (compiled as static C lib) |
| doctest v2.4.11 | FetchContent | Unit testing + CTest discovery |
| Material Icons | google/material-design-icons | Icon font for UI menus (`resources/fonts/`) |
| Tracy v0.13.1 | FetchContent | CPU+GPU profiling (on-demand) |
