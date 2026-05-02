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
    reservoirs[0].Reset();
    reservoirs[1].Reset();
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

    auto* cmd = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());

    D3D12_RESOURCE_BARRIER barriers[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            reservoirs[0].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
        ),
        CD3DX12_RESOURCE_BARRIER::Transition(
            reservoirs[1].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
        )
    };
    cmd->ResourceBarrier(2, barriers);

    cmd->SetComputeRootSignature(rootSig.Get());
    cmd->SetComputeRootConstantBufferView(3, perFrameCBAddr);

    ID3D12DescriptorHeap* heaps[] = { uavHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    // TODO: dispatch once shaders are ready.

    D3D12_RESOURCE_BARRIER resolveBarriers[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            reservoirs[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON
        ),
        CD3DX12_RESOURCE_BARRIER::Transition(
            reservoirs[1].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON
        )
    };
    cmd->ResourceBarrier(2, resolveBarriers);
}

void ReStirRenderer::createTextures(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    auto* device = static_cast<ID3D12Device2*>(dev.nativeHandle());
    const CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32G32B32A32_UINT, width, height, 1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    );

    for (int i = 0; i < 2; ++i) {
        chkDX(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&reservoirs[i])
        ));
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
