module;

#include <cstdint>
#include <vector>

export module terrain;

import common;
import scene;

export struct TerrainParams
{
    uint32_t gridSize = 256;    // vertices per side
    float worldSize = 100.0f;   // world units per side
    float heightScale = 12.0f;  // max height
    int octaves = 6;
    float frequency = 0.02f;  // base noise frequency
    uint32_t seed = 42;
};

// Generate terrain mesh data from noise
export void generateTerrain(
    const TerrainParams& params,
    std::vector<VertexPBR>& outVertices,
    std::vector<uint32_t>& outIndices
);
