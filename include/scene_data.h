#pragma once

#include "camera_types.h"
#include "ecs_types.h"
#include "material_types.h"
#include "terrain_types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct BloomData
{
    float threshold = 1.7f;
    float intensity = 0.1f;
    int tonemapMode = 2;
};

struct DirLightData
{
    vec3 dir = { 0.5f, -0.8f, 0.3f };
    float brightness = 3.0f;
    vec3 color = { 1.0f, 0.95f, 0.85f };
};

struct FogData
{
    float startY = -4.0f;
    float density = 0.4f;
    vec3 color = { 0.1f, 0.35f, 0.45f };
};

struct ShadowData
{
    bool enabled = true;
    float bias = 0.0002f;
    int rasterDepthBias = 1000;
    float rasterSlopeBias = 1.0f;
    float rasterBiasClamp = 0.0f;
    float lightDistance = 25.0f;
    float orthoSize = 30.0f;
    float nearPlane = 0.1f;
    float farPlane = 60.0f;
};

struct CubemapData
{
    bool enabled = true;
    uint32_t resolution = 128;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

struct PointLightsData
{
    float brightness = 0.1f;
    float ambient = 0.01f;
    std::array<PointLight, 8> lights = {};  // 8 matches SceneConstantBuffer::maxLights
};

struct SpawningData
{
    bool stopped = false;
    bool autoStop = true;
    float stopFrameMs = 10.0f;
    int batchSize = 50;
};

struct DisplayData
{
    bool vsync = true;
    bool animateEntities = true;
    float lightAnimSpeed = 1.0f;
    bool showBillboards = true;
};

struct EntityData
{
    std::string meshName;
    std::string materialName;
    vec3 position;
    float scale = 1.0f;
    vec3 rotAxis = { 0.0f, 1.0f, 0.0f };
    float rotAngle = 0.0f;
    vec4 albedoOverride;
    bool pickable = true;
    std::optional<Animated> animated;
};

struct InstanceGroupData
{
    std::string meshName;
    std::string materialName;
    std::vector<vec3> positions;
    std::vector<float> scales;
    std::vector<vec4> albedoOverrides;
};

struct RuntimeData
{
    bool useWarp = false;
    bool hideWindow = false;
    int screenshotFrame = 0;
    bool exitAfterScreenshot = false;
    int spawnPerFrame = 0;
    bool skipImGui = false;
    bool singleTeapotMode = false;
};

struct SceneFileData
{
    std::string title;
    std::string description;
    OrbitCamera camera;
    BloomData bloom;
    DirLightData dirLight;
    FogData fog;
    ShadowData shadow;
    CubemapData cubemap;
    PointLightsData pointLights;
    SpawningData spawning;
    DisplayData display;
    TerrainParams terrain;
    vec3 bgColor;
    std::vector<Material> materials;
    std::vector<EntityData> entities;
    std::vector<InstanceGroupData> instanceGroups;
    RuntimeData runtime;
};
