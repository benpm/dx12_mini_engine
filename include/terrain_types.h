#pragma once

#include "math_types.h"

#include <cstdint>

struct TerrainParams
{
    uint32_t gridSize = 256;
    float worldSize = 100.0f;
    float heightScale = 12.0f;
    int octaves = 6;
    float frequency = 0.02f;
    uint32_t seed = 42;
    vec4 materialAlbedo = { 0.05f, 0.15f, 0.25f, 1.0f };
    float materialRoughness = 0.3f;
    float positionY = -5.0f;
};
