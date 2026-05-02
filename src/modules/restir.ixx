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
    Microsoft::WRL::ComPtr<ID3D12Resource> reservoirs[2];
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> uavHeap;
    UINT uavDescSize = 0;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> initialPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> temporalPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spatialPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> resolvePSO;

    gfx::IDevice* devForDestroy = nullptr;

    void createShaders(gfx::IDevice& dev);
    void createTextures(gfx::IDevice& dev, uint32_t width, uint32_t height);
};
