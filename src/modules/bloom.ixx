module;

#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

export module bloom;

export import common;

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// BloomRenderer — owns HDR RT, bloom mip textures, heaps, PSOs, root sig
// ---------------------------------------------------------------------------
export class BloomRenderer
{
   public:
    static constexpr uint32_t bloomMipCount = 5;

    ComPtr<ID3D12Resource> hdrRenderTarget;
    ComPtr<ID3D12Resource> bloomMips[bloomMipCount];
    ComPtr<ID3D12DescriptorHeap> bloomRtvHeap;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    UINT srvDescSize = 0;
    ComPtr<ID3D12RootSignature> bloomRootSignature;
    ComPtr<ID3D12PipelineState> prefilterPSO;
    ComPtr<ID3D12PipelineState> downsamplePSO;
    ComPtr<ID3D12PipelineState> upsamplePSO;
    ComPtr<ID3D12PipelineState> compositePSO;

    // Creates textures, heaps, root sig, and all four PSOs
    void createResources(ID3D12Device2* device, uint32_t width, uint32_t height);

    // Re-creates textures and heaps at new size (PSOs unchanged)
    void resize(ID3D12Device2* device, uint32_t width, uint32_t height);

    // Runs the full bloom + composite pass.
    // backBuffer must be in PRESENT state on entry; leaves it in RENDER_TARGET state.
    // Resets hdrRenderTarget and bloomMips to RENDER_TARGET before returning.
    void render(
        ComPtr<ID3D12GraphicsCommandList2> cmdList,
        ComPtr<ID3D12Resource> backBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufRtv,
        uint32_t width,
        uint32_t height,
        float threshold,
        float intensity,
        int tonemapMode
    );

    // Recreate PSOs with new shader bytecodes. Null bytecodes fall back to embedded defaults.
    void reloadPipelines(
        ID3D12Device2* device,
        D3D12_SHADER_BYTECODE fullscreenVS,
        D3D12_SHADER_BYTECODE prefilterPS,
        D3D12_SHADER_BYTECODE downsamplePS,
        D3D12_SHADER_BYTECODE upsamplePS,
        D3D12_SHADER_BYTECODE compositePS
    );

   private:
    void createTexturesAndHeaps(ID3D12Device2* device, uint32_t width, uint32_t height);
    void createPipelines(ID3D12Device2* device);
};
