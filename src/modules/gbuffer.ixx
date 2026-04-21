module;

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <vector>
#include "d3dx12_clean.h"

export module gbuffer;

export import common;

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

    Microsoft::WRL::ComPtr<ID3D12Resource> resources[Count];

    void createResources(ID3D12Device2* device, uint32_t width, uint32_t height);
    void resize(ID3D12Device2* device, uint32_t width, uint32_t height);

    D3D12_CPU_DESCRIPTOR_HANDLE getRtv(TextureType type) const;
    D3D12_CPU_DESCRIPTOR_HANDLE getSrvCpu(TextureType type) const;
    D3D12_GPU_DESCRIPTOR_HANDLE getSrv(TextureType type) const;

    void transition(
        ID3D12GraphicsCommandList2* cmd,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after
    );

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap;
    UINT rtvDescSize = 0;
    UINT srvDescSize = 0;

   private:
    void createHeaps(ID3D12Device2* device);
    void createTextures(ID3D12Device2* device, uint32_t width, uint32_t height);
};