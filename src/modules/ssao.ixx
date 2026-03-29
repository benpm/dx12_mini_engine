module;

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <cstdint>
#include "d3dx12_clean.h"

export module ssao;

export import math;
export import common;

export class SsaoRenderer
{
   public:
    // Settings
    bool enabled = true;
    float radius = 0.5f;
    float bias = 0.025f;
    int kernelSize = 32;

    // Exposed to Application for normal pre-pass setup
    Microsoft::WRL::ComPtr<ID3D12Resource> normalRT;

    D3D12_CPU_DESCRIPTOR_HANDLE normalRtvCpu() const;
    D3D12_CPU_DESCRIPTOR_HANDLE ssaoRtvCpu() const;
    D3D12_CPU_DESCRIPTOR_HANDLE blurRtvCpu() const;

    // depthBuffer: main depth/stencil resource (R32G8X24_TYPELESS)
    // sceneSrvHeap: place SSAO output SRV at sceneSrvHeap[ssaoSlot]
    void createResources(
        ID3D12Device2* device,
        uint32_t width,
        uint32_t height,
        ID3D12Resource* depthBuffer,
        ID3D12DescriptorHeap* sceneSrvHeap,
        UINT sceneSrvDescSize,
        INT ssaoSlot
    );

    void resize(
        ID3D12Device2* device,
        uint32_t width,
        uint32_t height,
        ID3D12Resource* depthBuffer,
        ID3D12DescriptorHeap* sceneSrvHeap,
        UINT sceneSrvDescSize,
        INT ssaoSlot
    );

    // Run SSAO + blur. normalRT must be in PIXEL_SHADER_RESOURCE state on entry.
    void render(
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> cmdList,
        const mat4& view,
        const mat4& proj,
        uint32_t width,
        uint32_t height
    );

    static void transitionResource(
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> cmdList,
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

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;  // [0]=normalRT [1]=ssaoRT [2]=blurRT
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
        ID3D12Resource* depthBuffer,
        ID3D12DescriptorHeap* sceneSrvHeap,
        UINT sceneSrvDescSize,
        INT ssaoSlot
    );
};
