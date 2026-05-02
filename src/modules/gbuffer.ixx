module;

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <vector>
#include "d3dx12_clean.h"

export module gbuffer;

export import common;
export import gfx;

export class GBuffer
{
   public:
    enum TextureType
    {
        Normal = 0,
        Albedo,
        Material,
        Motion,
        Count
    };

    gfx::TextureHandle resources[Count];

    void createResources(gfx::IDevice& dev, uint32_t width, uint32_t height);
    void resize(gfx::IDevice& dev, uint32_t width, uint32_t height);
    ~GBuffer();

    D3D12_CPU_DESCRIPTOR_HANDLE getRtv(TextureType type) const;
    D3D12_CPU_DESCRIPTOR_HANDLE getSrvCpu(TextureType type) const;
    D3D12_GPU_DESCRIPTOR_HANDLE getSrv(TextureType type) const;

    // before/after stay as D3D12_RESOURCE_STATES until the imported textures
    // are themselves gfx::TextureHandles (P12+).
    void transition(
        gfx::ICommandList& cmdRef,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after
    );

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap;
    UINT rtvDescSize = 0;
    UINT srvDescSize = 0;

   private:
    void createHeaps(ID3D12Device2* device);
    void createTextures(gfx::IDevice& dev, uint32_t width, uint32_t height);

    static ID3D12Device2* nativeDev(gfx::IDevice& dev)
    {
        return static_cast<ID3D12Device2*>(dev.nativeHandle());
    }

    gfx::IDevice* devForDestroy = nullptr;
};