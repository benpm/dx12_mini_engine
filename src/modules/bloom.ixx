module;

#include <d3d12.h>
#include <Windows.h>
#include <wrl.h>
#include <cstdint>

export module bloom;

import common;
export import gfx;

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// BloomRenderer — owns HDR RT, bloom mip textures, heaps, PSOs, root sig
// ---------------------------------------------------------------------------
export class BloomRenderer
{
   public:
    static constexpr uint32_t bloomMipCount = 5;

    gfx::TextureHandle hdrRT{};
    gfx::TextureHandle bloomMips[bloomMipCount]{};
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    UINT srvDescSize = 0;
    ComPtr<ID3D12RootSignature> bloomRootSignature;
    ComPtr<ID3D12PipelineState> prefilterPSO;
    ComPtr<ID3D12PipelineState> downsamplePSO;
    ComPtr<ID3D12PipelineState> upsamplePSO;
    ComPtr<ID3D12PipelineState> compositePSO;

    void createResources(gfx::IDevice& dev, uint32_t width, uint32_t height);
    void resize(gfx::IDevice& dev, uint32_t width, uint32_t height);

    struct SkyParams
    {
        vec3 camForward, camRight, camUp;
        vec3 sunDir;
        float aspectRatio, tanHalfFov;
        float time;
    };

    void render(
        gfx::ICommandList& cmdRef,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufRtv,
        uint32_t width,
        uint32_t height,
        float threshold,
        float intensity,
        int tonemapMode,
        const SkyParams& sky
    );

    void reloadPipelines(
        gfx::IDevice& dev,
        D3D12_SHADER_BYTECODE fullscreenVS,
        D3D12_SHADER_BYTECODE prefilterPS,
        D3D12_SHADER_BYTECODE downsamplePS,
        D3D12_SHADER_BYTECODE upsamplePS,
        D3D12_SHADER_BYTECODE compositePS
    );

    ~BloomRenderer();

   private:
    gfx::IDevice* devForDestroy = nullptr;

    void createTexturesAndHeaps(gfx::IDevice& dev, uint32_t width, uint32_t height);
    void createPipelines(ID3D12Device2* device);

    static ID3D12Device2* nativeDev(gfx::IDevice& dev)
    {
        return static_cast<ID3D12Device2*>(dev.nativeHandle());
    }
    static ID3D12GraphicsCommandList2* nativeCmd(gfx::ICommandList& cmdRef)
    {
        return static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
    }
};
