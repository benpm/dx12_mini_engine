# AGENTS.md — DX12 Mini Engine

Guidance for AI agents (Claude Code, Codex, etc.) working in this repository. Use your own section for agent-specific stuff.

## Tools, Plugins, Skills, etc.

Use these tools PLEASE (only if they don't fail):

* Use `context7` if:
  * **KEYWORDS:** library, dependency, `lldb`, `eigen`, and anything formatting like this: `author/dependency` (github repo path)
  * **NOTE:** *required! exit on fail*
* Use `github` if:
  * **KEYWORDS:** github, gh, repo

See @README.md

---

## Reference Notes

Detailed documentation is split into sub-notes in the `notes/` directory:

| Note | Contents |
|----|----|
| @notes/build.md | Build commands, toolchain, CMake rules, before/after task checklist |
| @notes/architecture.md | Module list, module conventions, `gfx::` abstraction, migration status, ECS systems |
| @notes/subsystems.md | Subsystem descriptions, Application class layout, rendering pipeline, per-feature detail |
| @notes/scene-and-config.md | Scene loading, scene file system, configuration, Lua scripting |
| @notes/ui.md | ImGui UI panels, entity inspector, metrics window, testing |
| @notes/dependencies.md | All third-party dependencies with source and notes |
| @notes/code-style-and-profiling.md | clang-format/tidy rules, Tracy profiling setup |
| @notes/key-patterns.md | GPU patterns, pitfalls, and known gotchas |
| @notes/debugging.md | Running log of non-obvious bugs and fixes |
