module;

#include <vector>
#include "terrain_types.h"

export module terrain;

import scene;

export using ::TerrainParams;

// Generate terrain mesh data from noise
export void generateTerrain(
    const TerrainParams& params,
    std::vector<VertexPBR>& outVertices,
    std::vector<uint32_t>& outIndices
);
