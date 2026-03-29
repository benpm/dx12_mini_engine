#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct CameraData
{
    float fov = 0.959931f;  // 55 degrees in radians
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float yaw = -0.5f;
    float pitch = 0.5f;
    float radius = 30.0f;
};

struct BloomData
{
    float threshold = 1.7f;
    float intensity = 0.1f;
    int tonemapMode = 2;
};

struct DirLightData
{
    std::array<float, 3> dir = { 0.5f, -0.8f, 0.3f };
    float brightness = 3.0f;
    std::array<float, 3> color = { 1.0f, 0.95f, 0.85f };
};

struct FogData
{
    float startY = -4.0f;
    float density = 0.4f;
    std::array<float, 3> color = { 0.1f, 0.35f, 0.45f };
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

struct LightAnimData
{
    std::array<float, 3> center = { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> amp = { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> freq = { 0.0f, 0.0f, 0.0f };
    std::array<float, 4> color = { 1.0f, 1.0f, 1.0f, 1.0f };
};

struct PointLightsData
{
    float brightness = 0.1f;
    float ambient = 0.01f;
    std::array<LightAnimData, 8> lights = {};
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

struct TerrainData
{
    uint32_t gridSize = 256;
    float worldSize = 100.0f;
    float heightScale = 12.0f;
    int octaves = 6;
    float frequency = 0.02f;
    uint32_t seed = 42;
    std::array<float, 4> materialAlbedo = { 0.05f, 0.15f, 0.25f, 1.0f };
    float materialRoughness = 0.3f;
    float positionY = -5.0f;
};

struct MaterialData
{
    std::string name;
    std::array<float, 4> albedo = { 0.8f, 0.8f, 0.8f, 1.0f };
    float roughness = 0.4f;
    float metallic = 0.0f;
    float emissiveStrength = 0.0f;
    bool reflective = false;
    std::array<float, 4> emissive = { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct AnimatedData
{
    float speed = 1.0f;
    float orbitRadius = 5.0f;
    float orbitY = 0.0f;
    float initialScale = 1.0f;
    std::array<float, 3> rotAxis = { 0.0f, 1.0f, 0.0f };
    float rotAngle = 0.0f;
    float pulsePhase = 0.0f;
};

struct EntityData
{
    std::string meshName;
    std::string materialName;
    std::array<float, 3> position = { 0.0f, 0.0f, 0.0f };
    float scale = 1.0f;
    std::array<float, 3> rotAxis = { 0.0f, 1.0f, 0.0f };
    float rotAngle = 0.0f;
    std::array<float, 4> albedoOverride = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool pickable = true;
    std::optional<AnimatedData> animated;
};

struct RuntimeData
{
    bool useWarp = false;
    bool hideWindow = false;
    int screenshotFrame = 0;
    bool exitAfterScreenshot = false;
    int spawnPerFrame = 0;
    bool skipImGui = false;
};

struct SceneFileData
{
    CameraData camera;
    BloomData bloom;
    DirLightData dirLight;
    FogData fog;
    ShadowData shadow;
    CubemapData cubemap;
    PointLightsData pointLights;
    SpawningData spawning;
    DisplayData display;
    TerrainData terrain;
    std::array<float, 3> bgColor = { 0.0f, 0.0f, 0.0f };
    std::vector<MaterialData> materials;
    std::vector<EntityData> entities;
    RuntimeData runtime;
};
