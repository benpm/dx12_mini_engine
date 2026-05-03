module;

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <cstdint>
#include "d3dx12_clean.h"

export module ssao;

export import math;
export import common;
export import gfx;

export class SsaoRenderer
{
   public:
    bool enabled = true;
    float radius = 0.5f;
    float bias = 0.025f;
    int kernelSize = 32;

    void createResources(
        gfx::IDevice& dev,
        uint32_t width,
        uint32_t height,
        gfx::TextureHandle normalBuffer,
        gfx::TextureHandle depthBuffer
    );

    void resize(
        gfx::IDevice& dev,
        uint32_t width,
        uint32_t height,
        gfx::TextureHandle normalBuffer,
        gfx::TextureHandle depthBuffer
    );

    gfx::TextureHandle blurRT() const { return ssaoBlurRT; }

    void render(
        gfx::ICommandList& cmdRef,
        const mat4& view,
        const mat4& proj,
        uint32_t width,
        uint32_t height
    );

    ~SsaoRenderer();

   private:
    gfx::TextureHandle ssaoRT{};
    gfx::TextureHandle ssaoBlurRT{};
    gfx::TextureHandle noiseTexture{};
    Microsoft::WRL::ComPtr<ID3D12Resource> noiseUploadBuf;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT noiseFp = {};
    bool noisePendingUpload = false;
    gfx::BufferHandle cbvBuffer{};
    void* cbvMapped = nullptr;

    // Bindless SRV indices in the gfx global heap
    uint32_t normalSrvIdx = 0;
    uint32_t depthSrvIdx = 0;
    uint32_t noiseSrvIdx = 0;
    uint32_t ssaoRtSrvIdx = 0;

    gfx::PipelineHandle ssaoPSO{};
    gfx::PipelineHandle blurPSO{};
    gfx::ShaderHandle vsHandle{};
    gfx::ShaderHandle ssaoPsHandle{};
    gfx::ShaderHandle blurPsHandle{};

    DirectX::XMFLOAT4 kernel[32] = {};

    gfx::IDevice* devForDestroy = nullptr;

    void createRTs(
        gfx::IDevice& dev,
        uint32_t width,
        uint32_t height,
        gfx::TextureHandle normalBuffer,
        gfx::TextureHandle depthBuffer
    );
};
