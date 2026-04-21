# DX12 Mini Engine Documentation

This folder contains repository-wide documentation for development, architecture, and maintenance.

## Documentation Index

- [project-overview.md](project-overview.md): What this repository is, key features, and technology stack.
- [build-run-test.md](build-run-test.md): Toolchain assumptions, configure/build/test commands, and runtime options.
- [architecture-reference.md](architecture-reference.md): Engine architecture, module map, render pipeline, and subsystem ownership.
- [repository-map.md](repository-map.md): Directory-by-directory reference for source, assets, scripts, tests, and generated output.
- [repository-file-index.md](repository-file-index.md): Full file list snapshot of the workspace.

## Existing Notes

- [optimization.prompt.md](optimization.prompt.md)
- [Useful VS Code Extensions.md](Useful%20VS%20Code%20Extensions.md)

## Quick Start

1. Configure:

```bash
cmake --preset windows-clang
```

2. Build Debug:

```bash
cmake --build build --config Debug
```

3. Run integration scene (WARP + screenshot):

```bash
./build/Debug/main.exe resources/scenes/test.json
```

4. Run tests:

```bash
ctest --test-dir build -C Debug --output-on-failure
```
