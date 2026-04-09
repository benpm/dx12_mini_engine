#pragma once

#include <cstdint>
#include <string>

struct ConfigData
{
    // Window
    uint32_t windowWidth = 1280;
    uint32_t windowHeight = 720;

    // Graphics defaults
    bool vsync = true;
    int tonemapMode = 2;
    float bloomThreshold = 1.7f;
    float bloomIntensity = 0.1f;
    bool ssaoEnabled = true;
    float ssaoRadius = 0.5f;
    float ssaoBias = 0.025f;
    int ssaoKernelSize = 32;
    bool shadowsEnabled = true;
    bool cubemapEnabled = true;
    uint32_t cubemapResolution = 128;

    // Display
    bool showGrid = true;
    float gridMajorSize = 10.0f;
    int gridSubdivisions = 10;
    bool showMetrics = true;
    bool showLightBillboards = true;
    bool animateEntities = true;
    float lightAnimationSpeed = 1.0f;

    // Spawning
    bool autoStopSpawning = true;
    float spawnStopFrameMs = 10.0f;
    int spawnBatchSize = 50;

    // Paths
    std::string defaultScenePath = "resources/scenes/default.json";
};
