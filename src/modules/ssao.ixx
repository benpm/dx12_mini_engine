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

    D3D12_CPU_DESCRIPTOR_HANDLE ssaoRtvCpu() const;
    D3D12_CPU_DESCRIPTOR_HANDLE blurRtvCpu() const;

    void createResources(
        gfx::IDevice& dev,
        uint32_t width,
        uint32_t height,
        gfx::TextureHandle normalBuffer,
        gfx::TextureHandle depthBuffer,
        ID3D12DescriptorHeap* sceneSrvHeap,
        UINT sceneSrvDescSize,
        INT ssaoSlot
    );

    void resize(
        gfx::IDevice& dev,
        uint32_t width,
        uint32_t height,
        gfx::TextureHandle normalBuffer,
        gfx::TextureHandle depthBuffer,
        ID3D12DescriptorHeap* sceneSrvHeap,
        UINT sceneSrvDescSize,
        INT ssaoSlot
    );

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

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap;
    UINT rtvDescSize = 0;
    UINT srvDescSize = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> ssaoRootSig;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> blurRootSig;
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
        gfx::TextureHandle depthBuffer,
        ID3D12DescriptorHeap* sceneSrvHeap,
        UINT sceneSrvDescSize,
        INT ssaoSlot
    );

    void transitionResource(
        gfx::ICommandList& cmdRef,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after
    );

    static ID3D12Device2* nativeDev(gfx::IDevice& dev)
    {
        return static_cast<ID3D12Device2*>(dev.nativeHandle());
    }
    static ID3D12GraphicsCommandList2* nativeCmd(gfx::ICommandList& cmdRef)
    {
        return static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
    }
};
