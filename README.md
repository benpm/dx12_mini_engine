# DirectX 12 Toy Engine

 ![banner](banner.png)

Engine prototype for DX12 experimentation!

 ![](screenshot.png)

## Credit

* [3dgep.com](https://www.3dgep.com/learning-directx-12-1/) tutorials

## Dependencies

CMake LLVM toolchain with Ninja is preferred, you can also use MSVC. Check out `CMakePresets.json`.

## Building

```bash
cmake --preset windows-clang
cmake --build build --config Debug
```

## Testing

```bash
# Unit tests (doctest + CTest)
ctest --test-dir build -C Debug --output-on-failure

# Integration test (WARP adapter, renders 10 frames, writes screenshot.png)
./build/Debug/main.exe resources/scenes/test.json
```

## Troubleshooting Startup And Flecs Asserts

If the app crashes at startup or reports a Flecs invalid-entity assert, use this quick checklist.

1. Reproduce with no args first:
	- `./build/Debug/main.exe`
2. Compare with the automation scene:
	- `./build/Debug/main.exe resources/scenes/test.json`
3. If no-arg fails but test scene works, inspect scene-load and reset paths for stale ECS handles.
4. Any system that caches entities (gizmo, selection, hover, picker maps) must be rebuilt or cleared after `Scene::clearScene()`.
5. Keep UI-triggered scene changes deferred to `update()`; do not mutate ECS directly from ImGui rendering code.
6. After changes, re-run:
	- no-arg launch
	- test scene launch
	- `ctest --test-dir build -C Debug --output-on-failure`

Common pattern from recent fixes: `clearScene()` can remove entities owned by subsystems that hold persistent handles. Reinitialize those subsystems immediately after clear/reload operations.

## Lua Scripting

Lua 5.4 scripts can be attached to entities (`Scripted` component) or run as one-shot editor actions. ~40 `engine.*` API functions expose entity CRUD, transforms, materials, components, queries, spawning, and editor actions. See `resources/scripts/` for examples. Action bindings are configured in `resources/scripts/actions.json`.

## Agents

For agent contributions, see [AGENTS.md](AGENTS.md). This project is meant to be my learning of DirectX12, so I mostly use agents to write boilerplate, helping me write features while I learn the API by making more interesting features out of the building blocks that AI tools have provided. This project is also meant to be in feature-parity with my [OpenGL experimental engine](https://github.com/benpm/gl_playground).