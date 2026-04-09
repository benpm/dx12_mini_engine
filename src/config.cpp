module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <spdlog/spdlog.h>
#include <string>
#include "config_data.h"

module config;

extern bool readConfigJson(const std::string& path, ConfigData& out, std::string& err);
extern bool writeConfigJson(const std::string& path, const ConfigData& data, std::string& err);

bool loadConfig(const std::string& path, ConfigData& out)
{
    std::string err;
    if (!readConfigJson(path, out, err)) {
        spdlog::warn("Failed to load config '{}': {}", path, err);
        return false;
    }
    spdlog::info("Loaded config from '{}'", path);
    return true;
}

bool saveConfig(const std::string& path, const ConfigData& data)
{
    std::string err;
    if (!writeConfigJson(path, data, err)) {
        spdlog::error("Failed to save config '{}': {}", path, err);
        return false;
    }
    spdlog::info("Saved config to '{}'", path);
    return true;
}

bool mergeConfig(const std::string& path, ConfigData& out)
{
    out = ConfigData{};
    std::string err;
    if (!readConfigJson(path, out, err)) {
        spdlog::info("No existing config at '{}', writing defaults", path);
    }
    if (!writeConfigJson(path, out, err)) {
        spdlog::error("Failed to write config '{}': {}", path, err);
        return false;
    }
    spdlog::info("Config merged at '{}'", path);
    return true;
}
