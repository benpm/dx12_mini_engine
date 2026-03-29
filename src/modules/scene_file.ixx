module;

#include <string>
#include "scene_data.h"

export module scene_file;

export using ::CameraData;
export using ::BloomData;
export using ::DirLightData;
export using ::FogData;
export using ::ShadowData;
export using ::CubemapData;
export using ::LightAnimData;
export using ::PointLightsData;
export using ::SpawningData;
export using ::DisplayData;
export using ::TerrainData;
export using ::MaterialData;
export using ::AnimatedData;
export using ::EntityData;
export using ::RuntimeData;
export using ::SceneFileData;

export bool loadSceneFile(const std::string& path, SceneFileData& out);
export bool saveSceneFile(const std::string& path, const SceneFileData& data);
