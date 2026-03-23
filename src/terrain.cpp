module;

#include <cstdint>
#include <cmath>
#include <vector>
#include <PerlinNoise.hpp>

module terrain;

void generateTerrain(
    const TerrainParams& params,
    std::vector<VertexPBR>& outVertices,
    std::vector<uint32_t>& outIndices
)
{
    const uint32_t N = params.gridSize;
    const float size = params.worldSize;
    const float half = size * 0.5f;
    const float step = size / static_cast<float>(N - 1);

    siv::PerlinNoise perlin{ params.seed };

    // Generate vertices with noise-displaced Y
    outVertices.resize(N * N);
    std::vector<float> heights(N * N);

    for (uint32_t z = 0; z < N; ++z) {
        for (uint32_t x = 0; x < N; ++x) {
            float wx = -half + static_cast<float>(x) * step;
            float wz = -half + static_cast<float>(z) * step;

            float h = static_cast<float>(
                perlin.octave2D_01(wx * params.frequency, wz * params.frequency, params.octaves)
            );
            h = h * params.heightScale;

            uint32_t idx = z * N + x;
            heights[idx] = h;
            outVertices[idx].position = vec3(wx, h, wz);
            outVertices[idx].uv = vec2(
                static_cast<float>(x) / static_cast<float>(N - 1),
                static_cast<float>(z) / static_cast<float>(N - 1)
            );
        }
    }

    // Compute normals from finite differences
    for (uint32_t z = 0; z < N; ++z) {
        for (uint32_t x = 0; x < N; ++x) {
            float hL = (x > 0) ? heights[z * N + (x - 1)] : heights[z * N + x];
            float hR = (x < N - 1) ? heights[z * N + (x + 1)] : heights[z * N + x];
            float hD = (z > 0) ? heights[(z - 1) * N + x] : heights[z * N + x];
            float hU = (z < N - 1) ? heights[(z + 1) * N + x] : heights[z * N + x];

            vec3 normal = normalize(vec3(hL - hR, 2.0f * step, hD - hU));
            outVertices[z * N + x].normal = normal;
        }
    }

    // Generate triangle indices (two triangles per quad)
    outIndices.reserve((N - 1) * (N - 1) * 6);
    for (uint32_t z = 0; z < N - 1; ++z) {
        for (uint32_t x = 0; x < N - 1; ++x) {
            uint32_t tl = z * N + x;
            uint32_t tr = z * N + (x + 1);
            uint32_t bl = (z + 1) * N + x;
            uint32_t br = (z + 1) * N + (x + 1);
            // CW winding (DX12 default front-face)
            outIndices.push_back(tl);
            outIndices.push_back(tr);
            outIndices.push_back(bl);
            outIndices.push_back(tr);
            outIndices.push_back(br);
            outIndices.push_back(bl);
        }
    }
}
