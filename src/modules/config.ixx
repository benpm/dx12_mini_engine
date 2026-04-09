module;

#include <string>
#include "config_data.h"

export module config;

export using ::ConfigData;

export bool loadConfig(const std::string& path, ConfigData& out);
export bool saveConfig(const std::string& path, const ConfigData& data);
export bool mergeConfig(const std::string& path, ConfigData& out);
