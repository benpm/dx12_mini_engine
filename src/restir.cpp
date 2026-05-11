module;

#include <d3d12.h>
#include <wrl.h>
#include <algorithm>
#include "d3dx12_clean.h"

module restir;

using Microsoft::WRL::ComPtr;

void ReStirRenderer::createResources(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    devForDestroy = &dev;
    createTextures(dev, width, height);
    createShaders(dev);
}

void ReStirRenderer::resize(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    for (auto& h : reservoirs) {
        if (h.isValid()) {
            dev.destroy(h);
            h = {};
        }
    }
    createTextures(dev, width, height);
}

void ReStirRenderer::render(
    gfx::ICommandList& cmdRef,
    gfx::TextureHandle /*tlas*/,
    gfx::BufferHandle /*lightBuffer*/,
    uint32_t lightCount,
    gfx::TextureHandle /*normalRT*/,
    gfx::TextureHandle /*albedoRT*/,
    gfx::TextureHandle /*materialRT*/,
    gfx::TextureHandle /*motionRT*/,
    gfx::TextureHandle /*depthBuffer*/,
    gfx::TextureHandle /*outputHdrRT*/,
    uint64_t perFrameCBAddr,
    uint32_t /*width*/,
    uint32_t /*height*/,
    uint32_t /*frameIndex*/
)
{
    if (!settings.enabled || lightCount == 0) {
        return;
    }

    // ReStir dispatch is still a stub — once the compute shaders land, this
    // body will transition reservoirs[] to UnorderedAccess via the gfx
    // command list, bind the bindless root sig, dispatch the four PSOs, and
    // transition back. Avoid touching command list state today since nothing
    // actually executes.
    (void)cmdRef;
    (void)perFrameCBAddr;
}

void ReStirRenderer::createTextures(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    for (auto& h : reservoirs) {
        gfx::TextureDesc td{};
        td.width = width;
        td.height = height;
        td.format = gfx::Format::RGBA32Uint;
        td.usage = gfx::TextureUsage::UnorderedAccess | gfx::TextureUsage::ShaderResource;
        td.initialState = gfx::ResourceState::Common;
        td.debugName = "restir_reservoir";
        h = dev.createTexture(td);
    }
}

void ReStirRenderer::createShaders(gfx::IDevice& dev)
{
    auto* device = static_cast<ID3D12Device2*>(dev.nativeHandle());

    CD3DX12_DESCRIPTOR_RANGE1 uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);
    CD3DX12_DESCRIPTOR_RANGE1 srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0);

    CD3DX12_ROOT_PARAMETER1 params[5];
    params[0].InitAsDescriptorTable(1, &uavRange);
    params[1].InitAsDescriptorTable(1, &srvRange);
    params[2].InitAsShaderResourceView(0, 1);  // TLAS at t0 space1
    params[3].InitAsConstantBufferView(0, 0);  // PerFrame at b0
    params[4].InitAsConstants(4, 1, 0);        // ReSTIR params at b1

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsd;
    rsd.Init_1_1(5, params, 0, nullptr);

    ComPtr<ID3DBlob> blob, err;
    D3DX12SerializeVersionedRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1_1, &blob, &err);
    chkDX(device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSig)
    ));
}
