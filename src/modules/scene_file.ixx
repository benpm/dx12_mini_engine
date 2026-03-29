module;

#include <string>
#include "scene_data.h"

export module scene_file;

export using ::OrbitCamera;
export using ::BloomData;
export using ::DirLightData;
export using ::FogData;
export using ::ShadowData;
export using ::CubemapData;
export using ::PointLightsData;
export using ::SpawningData;
export using ::DisplayData;
export using ::TerrainParams;
export using ::EntityData;
export using ::RuntimeData;
export using ::SceneFileData;

export bool loadSceneFile(const std::string& path, SceneFileData& out);
export bool saveSceneFile(const std::string& path, const SceneFileData& data);
