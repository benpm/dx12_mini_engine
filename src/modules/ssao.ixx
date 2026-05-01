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
    // Settings
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
        ID3D12Resource* normalBuffer,
        ID3D12Resource* depthBuffer,
        ID3D12DescriptorHeap* sceneSrvHeap,
        UINT sceneSrvDescSize,
        INT ssaoSlot
    );

    void resize(
        gfx::IDevice& dev,
        uint32_t width,
        uint32_t height,
        ID3D12Resource* normalBuffer,
        ID3D12Resource* depthBuffer,
        ID3D12DescriptorHeap* sceneSrvHeap,
        UINT sceneSrvDescSize,
        INT ssaoSlot
    );

    // Run SSAO + blur. normalBuffer must be in PIXEL_SHADER_RESOURCE state on entry.
    void render(
        gfx::ICommandList& cmdRef,
        const mat4& view,
        const mat4& proj,
        uint32_t width,
        uint32_t height
    );

    static void transitionResource(
        gfx::ICommandList& cmdRef,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after
    );

   private:
    Microsoft::WRL::ComPtr<ID3D12Resource> ssaoRT;
    Microsoft::WRL::ComPtr<ID3D12Resource> ssaoBlurRT;
    Microsoft::WRL::ComPtr<ID3D12Resource> noiseTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> noiseUploadBuf;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT noiseFp = {};
    bool noisePendingUpload = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> cbvBuffer;
    void* cbvMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;  // [0]=ssaoRT [1]=blurRT
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>
        srvHeap;  // [0]=normal [1]=depth [2]=noise [3]=ssao
    UINT rtvDescSize = 0;
    UINT srvDescSize = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> ssaoRootSig;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> blurRootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ssaoPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blurPSO;

    DirectX::XMFLOAT4 kernel[32] = {};

    void createRTs(
        ID3D12Device2* device,
        uint32_t width,
        uint32_t height,
        ID3D12Resource* normalBuffer,
        ID3D12Resource* depthBuffer,
        ID3D12DescriptorHeap* sceneSrvHeap,
        UINT sceneSrvDescSize,
        INT ssaoSlot
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