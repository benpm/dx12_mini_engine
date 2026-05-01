module;

#include <d3d12.h>
#include <wrl.h>
#include "d3dx12_clean.h"

module gbuffer;

using Microsoft::WRL::ComPtr;

void GBuffer::createResources(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    auto* device = nativeDev(dev);
    createHeaps(device);
    createTextures(device, width, height);
}

void GBuffer::resize(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    auto* device = nativeDev(dev);
    for (int i = 0; i < Count; ++i) {
        resources[i].Reset();
    }
    createTextures(device, width, height);
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::getRtv(TextureType type) const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        rtvHeap->GetCPUDescriptorHandleForHeapStart(), type, rtvDescSize
    );
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::getSrvCpu(TextureType type) const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        srvHeap->GetCPUDescriptorHandleForHeapStart(), type, srvDescSize
    );
}

D3D12_GPU_DESCRIPTOR_HANDLE GBuffer::getSrv(TextureType type) const
{
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        srvHeap->GetGPUDescriptorHandleForHeapStart(), type, srvDescSize
    );
}

void GBuffer::transition(
    gfx::ICommandList& cmdRef,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after
)
{
    auto* cmd = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
    D3D12_RESOURCE_BARRIER barriers[Count];
    for (int i = 0; i < Count; ++i) {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(resources[i].Get(), before, after);
    }
    cmd->ResourceBarrier(Count, barriers);
}

void GBuffer::createHeaps(ID3D12Device2* device)
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = Count;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    chkDX(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap)));
    rtvDescSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = Count;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    chkDX(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap)));
    srvDescSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void GBuffer::createTextures(ID3D12Device2* device, uint32_t width, uint32_t height)
{
    DXGI_FORMAT formats[Count] = {
        DXGI_FORMAT_R8G8B8A8_UNORM,  // Normal
        DXGI_FORMAT_R8G8B8A8_UNORM,  // Albedo
        DXGI_FORMAT_R8G8_UNORM,      // Material
        DXGI_FORMAT_R16G16_FLOAT     // Motion
    };

    for (int i = 0; i < Count; ++i) {
        D3D12_CLEAR_VALUE clear = {};
        clear.Format = formats[i];
        if (i == Normal) {
            clear.Color[0] = 0.5f;
            clear.Color[1] = 0.5f;
            clear.Color[2] = 1.0f;
            clear.Color[3] = 1.0f;
        } else {
            clear.Color[0] = 0.0f;
            clear.Color[1] = 0.0f;
            clear.Color[2] = 0.0f;
            clear.Color[3] = 0.0f;
        }

        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            formats[i], width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
        );

        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, &clear,
            IID_PPV_ARGS(&resources[i])
        ));

        device->CreateRenderTargetView(
            resources[i].Get(), nullptr, getRtv(static_cast<TextureType>(i))
        );
        device->CreateShaderResourceView(
            resources[i].Get(), nullptr, getSrvCpu(static_cast<TextureType>(i))
        );
    }
}
