module;

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <vector>

export module restir;

export import common;
export import gfx;
export import math;

export class ReStirRenderer
{
   public:
    struct Settings
    {
        bool enabled = true;
        int initialSamples = 32;
        int temporalMClamp = 20;
        float spatialRadius = 16.0f;
        int spatialSamples = 4;
    } settings;

    void createResources(gfx::IDevice& dev, uint32_t width, uint32_t height);
    void resize(gfx::IDevice& dev, uint32_t width, uint32_t height);

    void render(
        gfx::ICommandList& cmd,
        gfx::TextureHandle tlas,  // acceleration structure (RT-only)
        gfx::BufferHandle lightBuffer,
        uint32_t lightCount,
        gfx::TextureHandle normalRT,
        gfx::TextureHandle albedoRT,
        gfx::TextureHandle materialRT,
        gfx::TextureHandle motionRT,
        gfx::TextureHandle depthBuffer,
        gfx::TextureHandle outputHdrRT,
        uint64_t perFrameCBAddr,
        uint32_t width,
        uint32_t height,
        uint32_t frameIndex
    );

   private:
    // Reservoir UAV textures (RGBA32_UINT, double-buffered for temporal reuse).
    // Migrated to gfx so the resource + bindless UAV slot are managed centrally.
    gfx::TextureHandle reservoirs[2]{};

    // Compute-shader root signature. ReStir defines its own layout (non-bindless)
    // because the shaders predate the bindless rewrite. Migrating to the engine's
    // bindless root sig is part of the eventual ReStir shader port.
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig;

    // 4 PSOs (initial / temporal / spatial / resolve) were stubs and never
    // allocated; their ComPtrs are removed in favour of gfx::PipelineHandle
    // slots that the eventual shader implementation will create on demand.
    gfx::PipelineHandle initialPSO{};
    gfx::PipelineHandle temporalPSO{};
    gfx::PipelineHandle spatialPSO{};
    gfx::PipelineHandle resolvePSO{};

    gfx::IDevice* devForDestroy = nullptr;

    void createShaders(gfx::IDevice& dev);
    void createTextures(gfx::IDevice& dev, uint32_t width, uint32_t height);
};
