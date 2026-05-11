module;

#include <string>

export module save_game;

// SaveGame — thin path helper around the existing scene-file machinery.
// A "save slot" is just a scene file at a well-known location under
// %LOCALAPPDATA%\dx12_mini_engine\saves\<name>.json. The actual save/load
// reuses Application::queueSceneLoad / queueSceneSave so the same code path
// handles editor scene files and runtime saves.
//
// Persisting only what scene_file.ixx already round-trips (camera, materials,
// entities, instance groups, runtime block) is good enough for a first cut.
// A proper save system needs flecs-level snapshotting with an opt-in Persistent
// tag and per-component versioning — future work.
export namespace SaveGame
{
    // Returns the full filesystem path for a save slot. Creates the parent
    // directory if it doesn't exist. Returns empty string on failure.
    std::string slotPath(const std::string& slotName);

    // Returns the saves directory (created on demand).
    std::string savesDir();
}  // namespace SaveGame
