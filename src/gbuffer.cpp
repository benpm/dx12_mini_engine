module;

#include <d3d12.h>
#include <wrl.h>
#include "d3dx12_clean.h"

module gbuffer;

using Microsoft::WRL::ComPtr;

GBuffer::~GBuffer()
{
    if (devForDestroy) {
        for (int i = 0; i < Count; ++i) {
            if (resources[i].isValid()) {
                devForDestroy->destroy(resources[i]);
            }
        }
    }
}

void GBuffer::createResources(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    devForDestroy = &dev;
    auto* device = nativeDev(dev);
    createHeaps(device);
    createTextures(dev, width, height);
}

void GBuffer::resize(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    devForDestroy = &dev;
    for (int i = 0; i < Count; ++i) {
        if (resources[i].isValid()) {
            dev.destroy(resources[i]);
            resources[i] = {};
        }
    }
    createTextures(dev, width, height);
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
        auto* res = static_cast<ID3D12Resource*>(devForDestroy->nativeResource(resources[i]));
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(res, before, after);
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

void GBuffer::createTextures(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    auto* device = nativeDev(dev);

    static constexpr gfx::Format formats[Count] = {
        gfx::Format::RGBA8Unorm,  // Normal
        gfx::Format::RGBA8Unorm,  // Albedo
        gfx::Format::RG8Unorm,    // Material
        gfx::Format::RG16Float,   // Motion
    };

    for (int i = 0; i < Count; ++i) {
        gfx::TextureDesc td{};
        td.width = width;
        td.height = height;
        td.format = formats[i];
        td.usage = gfx::TextureUsage::RenderTarget;
        td.initialState = gfx::ResourceState::Common;
        td.useClearValue = true;
        if (i == Normal) {
            td.clearColor[0] = 0.5f;
            td.clearColor[1] = 0.5f;
            td.clearColor[2] = 1.0f;
            td.clearColor[3] = 1.0f;
        }
        td.debugName = i == Normal     ? "gbuffer_normal"
                       : i == Albedo   ? "gbuffer_albedo"
                       : i == Material ? "gbuffer_material"
                                       : "gbuffer_motion";
        resources[i] = dev.createTexture(td);

        auto* res = static_cast<ID3D12Resource*>(dev.nativeResource(resources[i]));
        device->CreateRenderTargetView(res, nullptr, getRtv(static_cast<TextureType>(i)));
        device->CreateShaderResourceView(res, nullptr, getSrvCpu(static_cast<TextureType>(i)));
    }
}
