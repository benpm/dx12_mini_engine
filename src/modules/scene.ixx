module;

#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <flecs.h>
#include <vector>
#include <string>
#include <random>
#include <cstdint>

export module scene;

export import common;
export import ecs_components;
export import command_queue;

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
    mat4 model;
    mat4 viewProj;
    vec4 cameraPos;
    vec4 lightPos;
    vec4 lightColor;
    vec4 ambientColor;
    // PBR material
    vec4 albedo;
    float roughness;
    float metallic;
    float emissiveStrength;
    float _pad;
    vec4 emissive;
};

// ---------------------------------------------------------------------------
// CPU-side material
// ---------------------------------------------------------------------------
export struct Material
{
    vec4 albedo{ 0.8f, 0.8f, 0.8f, 1.0f };
    float roughness{ 0.4f };
    float metallic{ 0.0f };
    float emissiveStrength{ 0.0f };
    float _pad{ 0.0f };
    vec4 emissive{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::string name;
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
    int selectedMaterialIdx = 0;
    std::vector<MeshRef> spawnableMeshRefs;
    float spawnTimer = 0.0f;
    std::mt19937 rng{ std::random_device{}() };

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
    bool loadGltf(const std::string& path, ID3D12Device2* device, CommandQueue& cmdQueue);
    void loadTeapot(ID3D12Device2* device, CommandQueue& cmdQueue);
};
