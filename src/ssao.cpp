module;

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>
#include "d3dx12_clean.h"
#include "fullscreen_vs_cso.h"
#include "ssao_blur_ps_cso.h"
#include "ssao_ps_cso.h"

module ssao;

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct SsaoCBData
{
    XMFLOAT4X4 view;
    XMFLOAT4X4 proj;
    XMFLOAT4X4 invProj;
    XMFLOAT4 samples[32];
    float radius;
    float bias;
    float screenWidth;
    float screenHeight;
    int kernelSize;
    float _pad[11];
};
static_assert(sizeof(SsaoCBData) == 768);

void SsaoRenderer::transitionResource(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after
)
{
    CD3DX12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
    cmdList->ResourceBarrier(1, &b);
}

D3D12_CPU_DESCRIPTOR_HANDLE SsaoRenderer::ssaoRtvCpu() const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        rtvHeap->GetCPUDescriptorHandleForHeapStart(), 0, rtvDescSize
    );
}

D3D12_CPU_DESCRIPTOR_HANDLE SsaoRenderer::blurRtvCpu() const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        rtvHeap->GetCPUDescriptorHandleForHeapStart(), 1, rtvDescSize
    );
}

void SsaoRenderer::createRTs(
    ID3D12Device2* device,
    uint32_t width,
    uint32_t height,
    ID3D12Resource* normalBuffer,
    ID3D12Resource* depthBuffer,
    ID3D12DescriptorHeap* sceneSrvHeap,
    UINT sceneSrvDescSize,
    INT ssaoSlot
)
{
    width = std::max(1u, width);
    height = std::max(1u, height);

    // Use provided normalBuffer for SRV at internal slot 0
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        CD3DX12_CPU_DESCRIPTOR_HANDLE normalSrv(
            srvHeap->GetCPUDescriptorHandleForHeapStart(), 0, srvDescSize
        );
        device->CreateShaderResourceView(normalBuffer, &srvDesc, normalSrv);
    }

    // depthBuffer SRV at internal slot 1
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.PlaneSlice = 0;
        CD3DX12_CPU_DESCRIPTOR_HANDLE depthSrv(
            srvHeap->GetCPUDescriptorHandleForHeapStart(), 1, srvDescSize
        );
        device->CreateShaderResourceView(depthBuffer, &srvDesc, depthSrv);
    }

    // ssaoRT
    {
        const CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R8_UNORM, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
        );
        device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&ssaoRT)
        );
        device->CreateRenderTargetView(ssaoRT.Get(), nullptr, ssaoRtvCpu());
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        CD3DX12_CPU_DESCRIPTOR_HANDLE ssaoSrv(
            srvHeap->GetCPUDescriptorHandleForHeapStart(), 3, srvDescSize
        );
        device->CreateShaderResourceView(ssaoRT.Get(), &srvDesc, ssaoSrv);
    }

    // ssaoBlurRT
    {
        const CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R8_UNORM, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
        );
        device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&ssaoBlurRT)
        );
        device->CreateRenderTargetView(ssaoBlurRT.Get(), nullptr, blurRtvCpu());
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        CD3DX12_CPU_DESCRIPTOR_HANDLE sceneSrv(
            sceneSrvHeap->GetCPUDescriptorHandleForHeapStart(), ssaoSlot, sceneSrvDescSize
        );
        device->CreateShaderResourceView(ssaoBlurRT.Get(), &srvDesc, sceneSrv);
    }
}

void SsaoRenderer::createResources(
    ID3D12Device2* device,
    uint32_t width,
    uint32_t height,
    ID3D12Resource* normalBuffer,
    ID3D12Resource* depthBuffer,
    ID3D12DescriptorHeap* sceneSrvHeap,
    UINT sceneSrvDescSize,
    INT ssaoSlot
)
{
    rtvDescSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    srvDescSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 2;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtvHeap));
    }
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 4;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap));
    }

    {
        CD3DX12_DESCRIPTOR_RANGE1 srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);
        CD3DX12_ROOT_PARAMETER1 params[2];
        params[0].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
        params[1].InitAsConstantBufferView(
            0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL
        );
        D3D12_STATIC_SAMPLER_DESC samp = {};
        samp.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samp.ShaderRegister = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsd;
        rsd.Init_1_1(
            2, params, 1, &samp,
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
        );
        ComPtr<ID3DBlob> blob, err;
        D3DX12SerializeVersionedRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1_1, &blob, &err);
        device->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&ssaoRootSig)
        );
    }
    {
        CD3DX12_DESCRIPTOR_RANGE1 srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        CD3DX12_ROOT_PARAMETER1 params[1];
        params[0].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsd;
        rsd.Init_1_1(
            1, params, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
        );
        ComPtr<ID3DBlob> blob, err;
        D3DX12SerializeVersionedRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1_1, &blob, &err);
        device->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&blurRootSig)
        );
    }

    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = ssaoRootSig.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(g_fullscreen_vs, sizeof(g_fullscreen_vs));
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(g_ssao_ps, sizeof(g_ssao_ps));
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
        psoDesc.SampleDesc = { 1, 0 };
        device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&ssaoPSO));
    }
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = blurRootSig.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(g_fullscreen_vs, sizeof(g_fullscreen_vs));
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(g_ssao_blur_ps, sizeof(g_ssao_blur_ps));
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
        psoDesc.SampleDesc = { 1, 0 };
        device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&blurPSO));
    }

    {
        const CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SsaoCBData));
        device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&cbvBuffer)
        );
        cbvBuffer->Map(0, nullptr, &cbvMapped);
    }

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < 32; ++i) {
        XMFLOAT3 s = { dist(rng) * 2.0f - 1.0f, dist(rng) * 2.0f - 1.0f, dist(rng) };
        XMVECTOR sv = XMVector3Normalize(XMLoadFloat3(&s));
        float sc = static_cast<float>(i) / 32.0f;
        sv = XMVectorScale(sv, dist(rng) * (0.1f + sc * sc * 0.9f));
        XMStoreFloat4(&kernel[i], XMVectorSetW(sv, 0.0f));
    }

    {
        const CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC desc =
            CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32_FLOAT, 4, 4, 1, 1);
        device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&noiseTexture)
        );
        UINT64 uploadSize = 0;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &noiseFp, nullptr, nullptr, &uploadSize);
        const CD3DX12_HEAP_PROPERTIES uhp(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC ubDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
        ComPtr<ID3D12Resource> uploadBuf;
        device->CreateCommittedResource(
            &uhp, D3D12_HEAP_FLAG_NONE, &ubDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&uploadBuf)
        );
        XMFLOAT2 noiseData[16];
        for (int i = 0; i < 16; ++i) {
            noiseData[i] = { dist(rng) * 2.0f - 1.0f, dist(rng) * 2.0f - 1.0f };
        }
        void* mapped = nullptr;
        uploadBuf->Map(0, nullptr, &mapped);
        uint8_t* dst = static_cast<uint8_t*>(mapped) + noiseFp.Offset;
        for (uint32_t row = 0; row < 4; ++row) {
            memcpy(
                dst + row * noiseFp.Footprint.RowPitch, &noiseData[row * 4], 4 * sizeof(XMFLOAT2)
            );
        }
        uploadBuf->Unmap(0, nullptr);
        noiseUploadBuf = uploadBuf;
        noisePendingUpload = true;
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        CD3DX12_CPU_DESCRIPTOR_HANDLE noiseSrv(
            srvHeap->GetCPUDescriptorHandleForHeapStart(), 2, srvDescSize
        );
        device->CreateShaderResourceView(noiseTexture.Get(), &srvDesc, noiseSrv);
    }

    createRTs(device, width, height, normalBuffer, depthBuffer, sceneSrvHeap, sceneSrvDescSize, ssaoSlot);
}

void SsaoRenderer::resize(
    ID3D12Device2* device,
    uint32_t width,
    uint32_t height,
    ID3D12Resource* normalBuffer,
    ID3D12Resource* depthBuffer,
    ID3D12DescriptorHeap* sceneSrvHeap,
    UINT sceneSrvDescSize,
    INT ssaoSlot
)
{
    createRTs(device, width, height, normalBuffer, depthBuffer, sceneSrvHeap, sceneSrvDescSize, ssaoSlot);
}

void SsaoRenderer::render(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    const mat4& view,
    const mat4& proj,
    uint32_t width,
    uint32_t height
)
{
    if (!enabled) return;

    if (noisePendingUpload) {
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = noiseUploadBuf.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = noiseFp;
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = noiseTexture.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        transitionResource(cmdList, noiseTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        noisePendingUpload = false;
    }

    {
        SsaoCBData* cb = static_cast<SsaoCBData*>(cbvMapped);
        memcpy(&cb->view, &view, sizeof(XMFLOAT4X4));
        memcpy(&cb->proj, &proj, sizeof(XMFLOAT4X4));
        XMMATRIX invProjM = XMMatrixInverse(nullptr, XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&proj)));
        XMStoreFloat4x4(&cb->invProj, invProjM);
        memcpy(cb->samples, kernel, sizeof(kernel));
        cb->radius = radius;
        cb->bias = bias;
        cb->screenWidth = static_cast<float>(width);
        cb->screenHeight = static_cast<float>(height);
        cb->kernelSize = kernelSize;
    }

    D3D12_VIEWPORT vp = { 0, 0, (float)width, (float)height, 0, 1 };
    D3D12_RECT sr = { 0, 0, (LONG)width, (LONG)height };

    {
        transitionResource(cmdList, ssaoRT.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto rtv = ssaoRtvCpu();
        FLOAT white[] = { 1, 1, 1, 1 };
        cmdList->ClearRenderTargetView(rtv, white, 0, nullptr);
        cmdList->SetPipelineState(ssaoPSO.Get());
        cmdList->SetGraphicsRootSignature(ssaoRootSig.Get());
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sr);
        cmdList->OMSetRenderTargets(1, &rtv, false, nullptr);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
        cmdList->SetDescriptorHeaps(1, heaps);
        cmdList->SetGraphicsRootDescriptorTable(0, srvHeap->GetGPUDescriptorHandleForHeapStart());
        cmdList->SetGraphicsRootConstantBufferView(1, cbvBuffer->GetGPUVirtualAddress());
        cmdList->DrawInstanced(3, 1, 0, 0);
        transitionResource(cmdList, ssaoRT.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    {
        transitionResource(cmdList, ssaoBlurRT.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto rtv = blurRtvCpu();
        cmdList->SetPipelineState(blurPSO.Get());
        cmdList->SetGraphicsRootSignature(blurRootSig.Get());
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sr);
        cmdList->OMSetRenderTargets(1, &rtv, false, nullptr);
        ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
        cmdList->SetDescriptorHeaps(1, heaps);
        CD3DX12_GPU_DESCRIPTOR_HANDLE ssaoSrv(srvHeap->GetGPUDescriptorHandleForHeapStart(), 3, srvDescSize);
        cmdList->SetGraphicsRootDescriptorTable(0, ssaoSrv);
        cmdList->DrawInstanced(3, 1, 0, 0);
        transitionResource(cmdList, ssaoBlurRT.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
}