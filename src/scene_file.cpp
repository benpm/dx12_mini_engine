module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <spdlog/spdlog.h>
#include <string>
#include "scene_data.h"

module scene_file;

// Defined in glaze_impl.cpp (separate TU to isolate glaze templates)
extern bool readSceneJson(const std::string& path, SceneFileData& out, std::string& err);
extern bool writeSceneJson(const std::string& path, const SceneFileData& data, std::string& err);

bool loadSceneFile(const std::string& path, SceneFileData& out)
{
    std::string err;
    if (!readSceneJson(path, out, err)) {
        spdlog::error("Failed to load scene '{}': {}", path, err);
        return false;
    }
    spdlog::info("Loaded scene from '{}'", path);
    return true;
}

bool saveSceneFile(const std::string& path, const SceneFileData& data)
{
    std::string err;
    if (!writeSceneJson(path, data, err)) {
        spdlog::error("Failed to save scene '{}': {}", path, err);
        return false;
    }
    spdlog::info("Saved scene to '{}'", path);
    return true;
}
