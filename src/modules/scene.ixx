module;

#include <d3d12.h>
#include <DirectXCollision.h>
#include <flecs.h>
#include <Windows.h>
#include <wrl.h>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>
#include "material_types.h"

export module scene;

export import common;
export import ecs_components;
export import command_queue;
export import gfx;

export using ::Material;
export using ::MaterialPreset;

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// Draw command (one per draw call, references mega-buffer regions)
// ---------------------------------------------------------------------------
export struct DrawCmd
{
    uint32_t indexCount;
    uint32_t indexOffset;
    uint32_t vertexOffset;
    uint32_t instanceCount;
    uint32_t baseDrawIndex;
};

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
// Light data for Raytracing / ReSTIR
// ---------------------------------------------------------------------------
export struct LightData
{
    vec3 position;
    float intensity;
    vec3 color;
    float radius;
};

// ---------------------------------------------------------------------------
// Per-draw data (stored in a StructuredBuffer, indexed by drawIndex)
// ---------------------------------------------------------------------------
export struct PerFrameCB
{
    static constexpr int maxLights = 8;
    vec4 ambientColor;
    vec4 lightPos[maxLights];
    vec4 lightColor[maxLights];
    vec4 dirLightDir;
    vec4 dirLightColor;
    mat4 lightViewProj;
    float shadowBias;
    float shadowMapTexelSize;
    float fogStartY;
    float fogDensity;
    vec4 fogColor;
    float time;
};

export struct PerPassCB
{
    mat4 viewProj;
    mat4 prevViewProj;
    vec4 cameraPos;
};

export struct PerObjectData
{
    mat4 model;
    mat4 prevModel;
    vec4 albedo;
    float roughness;
    float metallic;
    float emissiveStrength;
    float reflective;
    vec4 emissive;
};

// ---------------------------------------------------------------------------
// Scene — owns ECS world, mega-buffers, draw-data buffers, materials
// ---------------------------------------------------------------------------
export class Scene
{
   public:
    static constexpr uint8_t nBuffers = 3;
    static constexpr uint32_t maxDrawsPerFrame = 16384;
    static constexpr uint32_t maxPassesPerFrame = 16;

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
    flecs::query<Scripted> scriptQuery{ ecsWorld.query<Scripted>() };

    gfx::BufferHandle megaVB{};
    gfx::BufferHandle megaIB{};
    gfx::VertexBufferView megaVBV{};
    gfx::IndexBufferView megaIBV{};
    uint32_t megaVBCapacity = 1024 * 1024;  // 1M verts
    uint32_t megaVBUsed = 0;
    uint32_t megaIBCapacity = 4 * 1024 * 1024;  // 4M indices
    uint32_t megaIBUsed = 0;

    gfx::BufferHandle perObjectBuffer[nBuffers];
    PerObjectData* perObjectMapped[nBuffers]{};
    gfx::BufferHandle perFrameBuffer[nBuffers];
    PerFrameCB* perFrameMapped[nBuffers]{};
    gfx::BufferHandle perPassBuffer[nBuffers];
    PerPassCB* perPassMapped[nBuffers]{};

    // Light Buffer for Raytracing/ReSTIR
    gfx::BufferHandle lightBuffer{};
    uint32_t activeLightCount = 0;
    static constexpr uint32_t maxLightsRRT = 1024;

    // Raytracing Acceleration Structures
    ComPtr<ID3D12Resource> tlasBuffer;
    ComPtr<ID3D12Resource> tlasScratch;
    ComPtr<ID3D12Resource> tlasInstances;
    struct BlasEntry
    {
        ComPtr<ID3D12Resource> buffer;
    };
    std::map<uint64_t, BlasEntry> blasMap;  // key: (vertexOffset << 32) | indexOffset

    std::vector<DrawCmd> drawCmds;
    std::vector<flecs::entity> drawIndexToEntity;
    std::vector<bool> isGizmoDraw;
    uint32_t totalSlots = 0;
    bool anyReflective = false;
    vec3 reflectivePos{};

    void populateDrawCommands(
        uint32_t curBackBufIdx,
        const mat4& matModel,
        const vec3& cameraPos,
        const DirectX::BoundingFrustum& frustum
    );
    void updateLightBuffer(gfx::IDevice& dev, CommandQueue& cmdQueue);

    void createMegaBuffers(gfx::IDevice& dev);
    void createDrawDataBuffers(gfx::IDevice& dev);
    MeshRef appendToMegaBuffers(
        gfx::IDevice& dev,
        CommandQueue& cmdQueue,
        const std::vector<VertexPBR>& vertices,
        const std::vector<uint32_t>& indices,
        int materialIdx
    );
    void clearScene(CommandQueue& cmdQueue);
    bool loadGltf(
        const std::string& path,
        gfx::IDevice& dev,
        CommandQueue& cmdQueue,
        bool append = false
    );
    void loadTeapot(gfx::IDevice& dev, CommandQueue& cmdQueue, bool includeCompanion = true);

    void updateTLAS(gfx::IDevice& dev, CommandQueue& cmdQueue, uint32_t curBackBufIdx);
    void buildBlasForMesh(gfx::IDevice& dev, CommandQueue& cmdQueue, MeshRef& mesh);

   private:
    static ID3D12Device2* nativeDev(gfx::IDevice& dev)
    {
        return static_cast<ID3D12Device2*>(dev.nativeHandle());
    }

   public:
};