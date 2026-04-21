module;

#include <d3d12.h>
#include <wrl.h>
#include <algorithm>
#include "d3dx12_clean.h"

module restir;

using Microsoft::WRL::ComPtr;

void ReStirRenderer::createResources(ID3D12Device2* device, uint32_t width, uint32_t height)
{
    createTextures(device, width, height);
    createShaders(device);
}

void ReStirRenderer::resize(ID3D12Device2* device, uint32_t width, uint32_t height)
{
    reservoirs[0].Reset();
    reservoirs[1].Reset();
    createTextures(device, width, height);
}

void ReStirRenderer::render(
    ID3D12GraphicsCommandList2* cmd,
    ID3D12Resource* tlas,
    ID3D12Resource* lightBuffer,
    uint32_t lightCount,
    ID3D12Resource* normalRT,
    ID3D12Resource* albedoRT,
    ID3D12Resource* materialRT,
    ID3D12Resource* motionRT,
    ID3D12Resource* depthBuffer,
    ID3D12Resource* outputHdrRT,
    D3D12_GPU_VIRTUAL_ADDRESS perFrameCB,
    uint32_t width,
    uint32_t height,
    uint32_t frameIndex
)
{
    if (!settings.enabled || lightCount == 0) {
        return;
    }

    // Transition reservoirs to UAV
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
    cmd->SetComputeRootConstantBufferView(3, perFrameCB);
    // TLAS is a special case, passed via GPU VA for RayQuery
    cmd->SetComputeRootShaderResourceView(2, tlas->GetGPUVirtualAddress());

    // Bind descriptor tables (handled by Application usually, but here we manage our own)
    ID3D12DescriptorHeap* heaps[] = { uavHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    // TODO: Create a combined heap or use Application's heap.
    // For now, I'll just skip the actual dispatch until I have shaders ready and verified.

    // Resolve pass transitions
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

void ReStirRenderer::createTextures(ID3D12Device2* device, uint32_t width, uint32_t height)
{
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

void ReStirRenderer::createShaders(ID3D12Device2* device)
{
    // Root Signature
    {
        CD3DX12_DESCRIPTOR_RANGE1 uavRange;
        uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);
        CD3DX12_DESCRIPTOR_RANGE1 srvRange;
        srvRange.Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0
        );  // Normal, Albedo, Material, Motion, Depth, LightBuffer

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
}