module;

#include <d3d12.h>
#include <flecs.h>
#include <Windows.h>
#include <wrl.h>
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include "material_types.h"

export module scene;

export import common;
export import ecs_components;
export import command_queue;

export using ::Material;
export using ::MaterialPreset;

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// GPU vertex layout (PBR)
// ---------------------------------------------------------------------------
export struct VertexPBR
{
    vec3 position;
    vec3 normal;
    vec2 uv;
};

// ---------------------------------------------------------------------------
// Per-draw data (stored in a StructuredBuffer, indexed by drawIndex)
// ---------------------------------------------------------------------------
export struct SceneConstantBuffer
{
    static constexpr int maxLights = 8;

    mat4 model;
    mat4 viewProj;
    vec4 cameraPos;
    vec4 ambientColor;
    vec4 lightPos[maxLights];    // xyz = world position
    vec4 lightColor[maxLights];  // rgb = pre-multiplied color
    // PBR material
    vec4 albedo;
    float roughness;
    float metallic;
    float emissiveStrength;
    float reflective;
    vec4 emissive;
    // Directional light (shadow-casting)
    vec4 dirLightDir;    // xyz = direction (toward light), w = unused
    vec4 dirLightColor;  // rgb = color * brightness
    mat4 lightViewProj;
    float shadowBias;
    float shadowMapTexelSize;  // 1.0 / shadowMapResolution
    float fogStartY;           // Y level where fog begins
    float fogDensity;          // fog thickness per unit below fogStartY
    vec4 fogColor;             // rgb = fog color, a = unused
};

// ---------------------------------------------------------------------------
// Scene — owns ECS world, mega-buffers, draw-data buffers, materials
// ---------------------------------------------------------------------------
export class Scene
{
   public:
    static constexpr uint8_t nBuffers = 3;
    static constexpr uint32_t maxDrawsPerFrame = 16384;

    flecs::world ecsWorld;
    std::vector<Material> materials;
    int presetIdx[static_cast<int>(MaterialPreset::Count)] = { -1, -1, -1 };
    int selectedMaterialIdx = 0;
    std::vector<MeshRef> spawnableMeshRefs;
    std::vector<std::string> spawnableMeshNames;
    float spawnTimer = 0.0f;
    std::mt19937 rng{ std::random_device{}() };

    // Cached ECS queries
    flecs::query<const Transform, const MeshRef> drawQuery{
        ecsWorld.query<const Transform, const MeshRef>()
    };
    flecs::query<Transform, Animated> animQuery{ ecsWorld.query<Transform, Animated>() };
    flecs::query<const Transform, const InstanceGroup> instanceQuery{
        ecsWorld.query<const Transform, const InstanceGroup>()
    };
    flecs::query<InstanceGroup, InstanceAnimation> instanceAnimQuery{
        ecsWorld.query<InstanceGroup, InstanceAnimation>()
    };
    flecs::query<PointLight> lightQuery{ ecsWorld.query<PointLight>() };

    ComPtr<ID3D12Resource> megaVB;
    ComPtr<ID3D12Resource> megaIB;
    D3D12_VERTEX_BUFFER_VIEW megaVBV{};
    D3D12_INDEX_BUFFER_VIEW megaIBV{};
    uint32_t megaVBCapacity = 0;
    uint32_t megaVBUsed = 0;
    uint32_t megaIBCapacity = 0;
    uint32_t megaIBUsed = 0;

    ComPtr<ID3D12Resource> drawDataBuffer[nBuffers];
    SceneConstantBuffer* drawDataMapped[nBuffers]{};
    ComPtr<ID3D12DescriptorHeap> sceneSrvHeap;
    UINT sceneSrvDescSize = 0;

    void createMegaBuffers(ID3D12Device2* device);
    void createDrawDataBuffers(ID3D12Device2* device);
    MeshRef appendToMegaBuffers(
        ComPtr<ID3D12GraphicsCommandList2> cmdList,
        const std::vector<VertexPBR>& vertices,
        const std::vector<uint32_t>& indices,
        int materialIdx,
        std::vector<ComPtr<ID3D12Resource>>& temps
    );
    void clearScene(CommandQueue& cmdQueue);
    bool loadGltf(
        const std::string& path,
        ID3D12Device2* device,
        CommandQueue& cmdQueue,
        bool append = false
    );
    void loadTeapot(ID3D12Device2* device, CommandQueue& cmdQueue);
};
