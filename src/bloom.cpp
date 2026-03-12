module;

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <algorithm>
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch"
#endif
#include "d3dx12.h"
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
#include "fullscreen_vs_cso.h"
#include "bloom_prefilter_ps_cso.h"
#include "bloom_downsample_ps_cso.h"
#include "bloom_upsample_ps_cso.h"
#include "bloom_composite_ps_cso.h"

module bloom;

using Microsoft::WRL::ComPtr;

static void transitionResource(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    ComPtr<ID3D12Resource> resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after
)
{
    CD3DX12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), before, after);
    cmdList->ResourceBarrier(1, &barrier);
}

// ---------------------------------------------------------------------------
// BloomRenderer::createTexturesAndHeaps
// ---------------------------------------------------------------------------

void BloomRenderer::createTexturesAndHeaps(ID3D12Device2* device, uint32_t width, uint32_t height)
{
    width = std::max(1u, width);
    height = std::max(1u, height);

    const DXGI_FORMAT hdrFormat = DXGI_FORMAT_R11G11B10_FLOAT;

    {
        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = hdrFormat;
        clearVal.Color[0] = 0.4f;
        clearVal.Color[1] = 0.6f;
        clearVal.Color[2] = 0.9f;
        clearVal.Color[3] = 1.0f;
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            hdrFormat, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
        );
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearVal,
            IID_PPV_ARGS(&hdrRenderTarget)
        ));
    }

    uint32_t mipW = std::max(1u, width / 2);
    uint32_t mipH = std::max(1u, height / 2);
    for (uint32_t i = 0; i < bloomMipCount; ++i) {
        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = hdrFormat;
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            hdrFormat, mipW, mipH, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
        );
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearVal,
            IID_PPV_ARGS(&bloomMips[i])
        ));
        mipW = std::max(1u, mipW / 2);
        mipH = std::max(1u, mipH / 2);
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 1 + bloomMipCount;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        chkDX(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&bloomRtvHeap)));
    }
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 1 + bloomMipCount;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        chkDX(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap)));
    }
    srvDescSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    UINT rtvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(bloomRtvHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(srvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = hdrFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    device->CreateRenderTargetView(hdrRenderTarget.Get(), nullptr, rtvHandle);
    rtvHandle.Offset(static_cast<INT>(rtvInc));
    device->CreateShaderResourceView(hdrRenderTarget.Get(), &srvDesc, srvHandle);
    srvHandle.Offset(static_cast<INT>(srvDescSize));

    for (uint32_t i = 0; i < bloomMipCount; ++i) {
        device->CreateRenderTargetView(bloomMips[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(static_cast<INT>(rtvInc));
        device->CreateShaderResourceView(bloomMips[i].Get(), &srvDesc, srvHandle);
        srvHandle.Offset(static_cast<INT>(srvDescSize));
    }
}

// ---------------------------------------------------------------------------
// BloomRenderer::createPipelines
// ---------------------------------------------------------------------------

void BloomRenderer::createPipelines(ID3D12Device2* device)
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(device->CheckFeatureSupport(
            D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)
        ))) {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    {
        CD3DX12_DESCRIPTOR_RANGE1 srvRange0, srvRange1;
        srvRange0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        srvRange1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

        CD3DX12_ROOT_PARAMETER1 bloomRootParams[3];
        bloomRootParams[0].InitAsDescriptorTable(1, &srvRange0, D3D12_SHADER_VISIBILITY_PIXEL);
        bloomRootParams[1].InitAsDescriptorTable(1, &srvRange1, D3D12_SHADER_VISIBILITY_PIXEL);
        bloomRootParams[2].InitAsConstants(4, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC staticSampler = {};
        staticSampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.ShaderRegister = 0;
        staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC bloomRootSigDesc;
        bloomRootSigDesc.Init_1_1(
            3, bloomRootParams, 1, &staticSampler,
            D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
        );

        ComPtr<ID3DBlob> bloomSigBlob, bloomErrBlob;
        chkDX(D3DX12SerializeVersionedRootSignature(
            &bloomRootSigDesc, featureData.HighestVersion, &bloomSigBlob, &bloomErrBlob
        ));
        chkDX(device->CreateRootSignature(
            0, bloomSigBlob->GetBufferPointer(), bloomSigBlob->GetBufferSize(),
            IID_PPV_ARGS(&bloomRootSignature)
        ));
    }

    auto createBloomPSO = [&](const BYTE* psData, size_t psSize, DXGI_FORMAT rtFormat,
                               bool additiveBlend) -> ComPtr<ID3D12PipelineState> {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = bloomRootSignature.Get();
        desc.VS = CD3DX12_SHADER_BYTECODE(g_fullscreen_vs, sizeof(g_fullscreen_vs));
        desc.PS = CD3DX12_SHADER_BYTECODE(psData, psSize);
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        if (additiveBlend) {
            desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
            desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        }
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.StencilEnable = FALSE;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = rtFormat;
        desc.SampleDesc.Count = 1;
        ComPtr<ID3D12PipelineState> pso;
        chkDX(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)));
        return pso;
    };

    const DXGI_FORMAT hdrFormat = DXGI_FORMAT_R11G11B10_FLOAT;
    prefilterPSO =
        createBloomPSO(g_bloom_prefilter_ps, sizeof(g_bloom_prefilter_ps), hdrFormat, false);
    downsamplePSO =
        createBloomPSO(g_bloom_downsample_ps, sizeof(g_bloom_downsample_ps), hdrFormat, false);
    upsamplePSO =
        createBloomPSO(g_bloom_upsample_ps, sizeof(g_bloom_upsample_ps), hdrFormat, true);
    compositePSO = createBloomPSO(
        g_bloom_composite_ps, sizeof(g_bloom_composite_ps), DXGI_FORMAT_R8G8B8A8_UNORM, false
    );
}

// ---------------------------------------------------------------------------
// BloomRenderer::createResources / resize
// ---------------------------------------------------------------------------

void BloomRenderer::createResources(ID3D12Device2* device, uint32_t width, uint32_t height)
{
    createPipelines(device);
    createTexturesAndHeaps(device, width, height);
}

void BloomRenderer::resize(ID3D12Device2* device, uint32_t width, uint32_t height)
{
    hdrRenderTarget.Reset();
    for (auto& m : bloomMips) {
        m.Reset();
    }
    bloomRtvHeap.Reset();
    srvHeap.Reset();
    createTexturesAndHeaps(device, width, height);
}

// ---------------------------------------------------------------------------
// BloomRenderer::render
// ---------------------------------------------------------------------------

void BloomRenderer::render(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    ComPtr<ID3D12Resource> backBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufRtv,
    uint32_t width,
    uint32_t height,
    float threshold,
    float intensity,
    int tonemapMode
)
{
    ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootSignature(bloomRootSignature.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    UINT rtvInc;
    {
        ComPtr<ID3D12Device> dev;
        srvHeap->GetDevice(IID_PPV_ARGS(&dev));
        rtvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }
    auto bloomRtvBase = bloomRtvHeap->GetCPUDescriptorHandleForHeapStart();
    auto srvGpuBase = srvHeap->GetGPUDescriptorHandleForHeapStart();

    auto getRtv = [&](uint32_t idx) -> D3D12_CPU_DESCRIPTOR_HANDLE {
        return CD3DX12_CPU_DESCRIPTOR_HANDLE(bloomRtvBase, static_cast<INT>(idx), rtvInc);
    };
    auto getSrvGpu = [&](uint32_t idx) -> D3D12_GPU_DESCRIPTOR_HANDLE {
        return CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuBase, static_cast<INT>(idx), srvDescSize);
    };

    uint32_t mipW[bloomMipCount], mipH[bloomMipCount];
    mipW[0] = std::max(1u, width / 2);
    mipH[0] = std::max(1u, height / 2);
    for (uint32_t i = 1; i < bloomMipCount; ++i) {
        mipW[i] = std::max(1u, mipW[i - 1] / 2);
        mipH[i] = std::max(1u, mipH[i - 1] / 2);
    }

    struct BloomCB
    {
        float texelSizeX, texelSizeY, param0, param1;
    };

    // Prefilter
    transitionResource(
        cmdList, hdrRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    cmdList->SetPipelineState(prefilterPSO.Get());
    cmdList->SetGraphicsRootDescriptorTable(0, getSrvGpu(0));
    auto mip0Rtv = getRtv(1);
    cmdList->OMSetRenderTargets(1, &mip0Rtv, false, nullptr);
    CD3DX12_VIEWPORT vp(0.0f, 0.0f, (float)mipW[0], (float)mipH[0]);
    D3D12_RECT sr = { 0, 0, (LONG)mipW[0], (LONG)mipH[0] };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sr);
    BloomCB cb = { 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height),
                   threshold, 0.5f };
    cmdList->SetGraphicsRoot32BitConstants(2, 4, &cb, 0);
    cmdList->DrawInstanced(3, 1, 0, 0);

    // Downsample
    cmdList->SetPipelineState(downsamplePSO.Get());
    for (uint32_t i = 0; i < bloomMipCount - 1; ++i) {
        transitionResource(
            cmdList, bloomMips[i], D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        cmdList->SetGraphicsRootDescriptorTable(0, getSrvGpu(1 + i));
        auto rtv = getRtv(2 + i);
        cmdList->OMSetRenderTargets(1, &rtv, false, nullptr);
        vp = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)mipW[i + 1], (float)mipH[i + 1]);
        sr = { 0, 0, (LONG)mipW[i + 1], (LONG)mipH[i + 1] };
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sr);
        cb = { 1.0f / static_cast<float>(mipW[i]), 1.0f / static_cast<float>(mipH[i]), 0, 0 };
        cmdList->SetGraphicsRoot32BitConstants(2, 4, &cb, 0);
        cmdList->DrawInstanced(3, 1, 0, 0);
    }

    // Upsample (additive)
    cmdList->SetPipelineState(upsamplePSO.Get());
    for (int i = bloomMipCount - 2; i >= 0; --i) {
        transitionResource(
            cmdList, bloomMips[i + 1], D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        transitionResource(
            cmdList, bloomMips[i], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        cmdList->SetGraphicsRootDescriptorTable(0, getSrvGpu(2 + i));
        auto rtv = getRtv(1 + i);
        cmdList->OMSetRenderTargets(1, &rtv, false, nullptr);
        vp = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)mipW[i], (float)mipH[i]);
        sr = { 0, 0, (LONG)mipW[i], (LONG)mipH[i] };
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sr);
        cb = { 1.0f / static_cast<float>(mipW[i + 1]), 1.0f / static_cast<float>(mipH[i + 1]),
               1.0f, 0 };
        cmdList->SetGraphicsRoot32BitConstants(2, 4, &cb, 0);
        cmdList->DrawInstanced(3, 1, 0, 0);
    }

    // Composite
    transitionResource(
        cmdList, bloomMips[0], D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    transitionResource(
        cmdList, backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
    );
    cmdList->SetPipelineState(compositePSO.Get());
    cmdList->SetGraphicsRootDescriptorTable(0, getSrvGpu(0));
    cmdList->SetGraphicsRootDescriptorTable(1, getSrvGpu(1));
    cmdList->OMSetRenderTargets(1, &backBufRtv, false, nullptr);
    vp = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)width, (float)height);
    sr = { 0, 0, (LONG)width, (LONG)height };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sr);
    struct
    {
        float a, b, c;
        uint32_t d;
    } compositeCB = { 0, 0, intensity, (uint32_t)tonemapMode };
    cmdList->SetGraphicsRoot32BitConstants(2, 4, &compositeCB, 0);
    cmdList->DrawInstanced(3, 1, 0, 0);

    // Reset bloom resources to RENDER_TARGET for next frame
    transitionResource(
        cmdList, hdrRenderTarget, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );
    for (uint32_t i = 0; i < bloomMipCount; ++i) {
        transitionResource(
            cmdList, bloomMips[i], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
    }
}
