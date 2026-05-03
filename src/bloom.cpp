module;

#include <d3d12.h>
#include "bloom_composite_ps_cso.h"
#include "bloom_downsample_ps_cso.h"
#include "bloom_prefilter_ps_cso.h"
#include "bloom_upsample_ps_cso.h"
#include "d3dx12_clean.h"
#include "fullscreen_vs_cso.h"

module bloom;

using Microsoft::WRL::ComPtr;

static void reloadBloomPipelinesNative(
    ID3D12Device2* device,
    ID3D12RootSignature* rootSig,
    D3D12_SHADER_BYTECODE fullscreenVS,
    D3D12_SHADER_BYTECODE prefilterPS,
    D3D12_SHADER_BYTECODE downsamplePS,
    D3D12_SHADER_BYTECODE upsamplePS,
    D3D12_SHADER_BYTECODE compositePS,
    ComPtr<ID3D12PipelineState>& outPrefilter,
    ComPtr<ID3D12PipelineState>& outDownsample,
    ComPtr<ID3D12PipelineState>& outUpsample,
    ComPtr<ID3D12PipelineState>& outComposite
);

static void transitionResource(
    ID3D12GraphicsCommandList2* cmdList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after
)
{
    CD3DX12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
    cmdList->ResourceBarrier(1, &barrier);
}

// ---------------------------------------------------------------------------
// BloomRenderer destructor
// ---------------------------------------------------------------------------

BloomRenderer::~BloomRenderer()
{
    if (devForDestroy) {
        if (hdrRT.isValid()) {
            devForDestroy->destroy(hdrRT);
        }
        for (auto& m : bloomMips) {
            if (m.isValid()) {
                devForDestroy->destroy(m);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// BloomRenderer::createTexturesAndHeaps
// ---------------------------------------------------------------------------

void BloomRenderer::createTexturesAndHeaps(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    width = std::max(1u, width);
    height = std::max(1u, height);

    {
        gfx::TextureDesc td{};
        td.width = width;
        td.height = height;
        td.format = gfx::Format::R11G11B10Float;
        td.usage = gfx::TextureUsage::RenderTarget | gfx::TextureUsage::ShaderResource;
        td.initialState = gfx::ResourceState::PixelShaderResource;
        td.useClearValue = true;
        td.clearColor[3] = 1.0f;
        td.debugName = "hdr_rt";
        hdrRT = dev.createTexture(td);
    }

    uint32_t mipW = std::max(1u, width / 2);
    uint32_t mipH = std::max(1u, height / 2);
    for (uint32_t i = 0; i < bloomMipCount; ++i) {
        gfx::TextureDesc td{};
        td.width = mipW;
        td.height = mipH;
        td.format = gfx::Format::R11G11B10Float;
        td.usage = gfx::TextureUsage::RenderTarget | gfx::TextureUsage::ShaderResource;
        td.initialState = gfx::ResourceState::RenderTarget;
        td.useClearValue = true;
        td.debugName = "bloom_mip";
        bloomMips[i] = dev.createTexture(td);
        mipW = std::max(1u, mipW / 2);
        mipH = std::max(1u, mipH / 2);
    }
}

// ---------------------------------------------------------------------------
// BloomRenderer::createPipelines
// ---------------------------------------------------------------------------

void BloomRenderer::createPipelines(gfx::IDevice& dev)
{
    auto* device = static_cast<ID3D12Device2*>(dev.nativeHandle());
    auto* psoRootSig = static_cast<ID3D12RootSignature*>(dev.bindlessRootSigNative());

    reloadBloomPipelinesNative(
        device, psoRootSig, {}, {}, {}, {}, {}, prefilterPSO, downsamplePSO, upsamplePSO,
        compositePSO
    );
}

// ---------------------------------------------------------------------------
// BloomRenderer::createResources / resize
// ---------------------------------------------------------------------------

void BloomRenderer::createResources(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    devForDestroy = &dev;
    createPipelines(dev);
    createTexturesAndHeaps(dev, width, height);
}

void BloomRenderer::resize(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    if (hdrRT.isValid()) {
        dev.destroy(hdrRT);
        hdrRT = {};
    }
    for (auto& m : bloomMips) {
        if (m.isValid()) {
            dev.destroy(m);
            m = {};
        }
    }
    createTexturesAndHeaps(dev, width, height);
}

// ---------------------------------------------------------------------------
// BloomRenderer::reloadPipelines
// ---------------------------------------------------------------------------

static void reloadBloomPipelinesNative(
    ID3D12Device2* device,
    ID3D12RootSignature* rootSig,
    D3D12_SHADER_BYTECODE fullscreenVS,
    D3D12_SHADER_BYTECODE prefilterPS,
    D3D12_SHADER_BYTECODE downsamplePS,
    D3D12_SHADER_BYTECODE upsamplePS,
    D3D12_SHADER_BYTECODE compositePS,
    ComPtr<ID3D12PipelineState>& outPrefilter,
    ComPtr<ID3D12PipelineState>& outDownsample,
    ComPtr<ID3D12PipelineState>& outUpsample,
    ComPtr<ID3D12PipelineState>& outComposite
)
{
    auto resolve = [](D3D12_SHADER_BYTECODE bc, const BYTE* def, size_t defSize) {
        return bc.pShaderBytecode ? bc : D3D12_SHADER_BYTECODE{ def, defSize };
    };
    auto vs = resolve(fullscreenVS, g_fullscreen_vs, sizeof(g_fullscreen_vs));
    auto pre = resolve(prefilterPS, g_bloom_prefilter_ps, sizeof(g_bloom_prefilter_ps));
    auto down = resolve(downsamplePS, g_bloom_downsample_ps, sizeof(g_bloom_downsample_ps));
    auto up = resolve(upsamplePS, g_bloom_upsample_ps, sizeof(g_bloom_upsample_ps));
    auto comp = resolve(compositePS, g_bloom_composite_ps, sizeof(g_bloom_composite_ps));

    auto createPSO = [&](D3D12_SHADER_BYTECODE ps, DXGI_FORMAT rtFormat,
                         bool additiveBlend) -> ComPtr<ID3D12PipelineState> {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = rootSig;
        desc.VS = vs;
        desc.PS = ps;
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
    outPrefilter = createPSO(pre, hdrFormat, false);
    outDownsample = createPSO(down, hdrFormat, false);
    outUpsample = createPSO(up, hdrFormat, true);
    outComposite = createPSO(comp, DXGI_FORMAT_R8G8B8A8_UNORM, false);
}

void BloomRenderer::reloadPipelines(
    gfx::IDevice& dev,
    gfx::ShaderBytecode fullscreenVS,
    gfx::ShaderBytecode prefilterPS,
    gfx::ShaderBytecode downsamplePS,
    gfx::ShaderBytecode upsamplePS,
    gfx::ShaderBytecode compositePS
)
{
    auto* psoRootSig = static_cast<ID3D12RootSignature*>(dev.bindlessRootSigNative());
    reloadBloomPipelinesNative(
        static_cast<ID3D12Device2*>(dev.nativeHandle()), psoRootSig,
        { fullscreenVS.data, fullscreenVS.size }, { prefilterPS.data, prefilterPS.size },
        { downsamplePS.data, downsamplePS.size }, { upsamplePS.data, upsamplePS.size },
        { compositePS.data, compositePS.size }, prefilterPSO, downsamplePSO, upsamplePSO,
        compositePSO
    );
}

// ---------------------------------------------------------------------------
// BloomRenderer::render
// ---------------------------------------------------------------------------

void BloomRenderer::render(
    gfx::ICommandList& cmdRef,
    uint64_t backBufRtvVal,
    uint32_t width,
    uint32_t height,
    float threshold,
    float intensity,
    int tonemapMode,
    const SkyParams& sky
)
{
    auto* cmdList = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
    D3D12_CPU_DESCRIPTOR_HANDLE backBufRtv{ backBufRtvVal };
    auto* gfxSrvHeap = static_cast<ID3D12DescriptorHeap*>(devForDestroy->srvHeapNative());
    auto* samplerHeap = static_cast<ID3D12DescriptorHeap*>(devForDestroy->samplerHeapNative());
    ID3D12DescriptorHeap* heaps[] = { gfxSrvHeap, samplerHeap };
    cmdList->SetDescriptorHeaps(2, heaps);

    cmdList->SetGraphicsRootSignature(
        static_cast<ID3D12RootSignature*>(devForDestroy->bindlessRootSigNative())
    );
    D3D12_GPU_DESCRIPTOR_HANDLE heapStart;
    heapStart.ptr = devForDestroy->srvGpuDescriptorHandle(0);
    cmdList->SetGraphicsRootDescriptorTable(3, heapStart);  // Slot 3: SRV table
    D3D12_GPU_DESCRIPTOR_HANDLE samplerHeapStart;
    samplerHeapStart.ptr = devForDestroy->samplerGpuDescriptorHandle(0);
    cmdList->SetGraphicsRootDescriptorTable(4, samplerHeapStart);  // Slot 4: sampler table
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto getRtv = [&](gfx::TextureHandle h) -> D3D12_CPU_DESCRIPTOR_HANDLE {
        D3D12_CPU_DESCRIPTOR_HANDLE rv;
        rv.ptr = static_cast<SIZE_T>(devForDestroy->rtvHandle(h));
        return rv;
    };

    auto mipRes = [&](uint32_t i) -> ID3D12Resource* {
        return static_cast<ID3D12Resource*>(devForDestroy->nativeResource(bloomMips[i]));
    };

    uint32_t mipW[bloomMipCount], mipH[bloomMipCount];
    mipW[0] = std::max(1u, width / 2);
    mipH[0] = std::max(1u, height / 2);
    for (uint32_t i = 1; i < bloomMipCount; ++i) {
        mipW[i] = std::max(1u, mipW[i - 1] / 2);
        mipH[i] = std::max(1u, mipH[i - 1] / 2);
    }

    // Prefilter — hdr_rt is already in PixelShaderResource state because the
    // bloom render-graph pass declared `readTexture(hHdrRT)`.
    cmdList->SetPipelineState(prefilterPSO.Get());
    struct BindlessPrefilterPayload
    {
        uint32_t srcIdx;
        uint32_t samplerIdx;
        uint32_t _pad[2];
        float texelSizeX, texelSizeY, threshold, softKnee;
    };
    BindlessPrefilterPayload pp;
    pp.srcIdx = devForDestroy->bindlessSrvIndex(hdrRT);
    pp.samplerIdx = 0;
    pp.texelSizeX = 1.0f / static_cast<float>(width);
    pp.texelSizeY = 1.0f / static_cast<float>(height);
    pp.threshold = threshold;
    pp.softKnee = 0.5f;
    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(pp) / 4, &pp, 0);
    auto mip0Rtv = getRtv(bloomMips[0]);
    cmdList->OMSetRenderTargets(1, &mip0Rtv, false, nullptr);
    CD3DX12_VIEWPORT vp(0.0f, 0.0f, (float)mipW[0], (float)mipH[0]);
    D3D12_RECT sr = { 0, 0, (LONG)mipW[0], (LONG)mipH[0] };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sr);
    cmdList->DrawInstanced(3, 1, 0, 0);

    // Downsample
    cmdList->SetPipelineState(downsamplePSO.Get());
    for (uint32_t i = 0; i < bloomMipCount - 1; ++i) {
        transitionResource(
            cmdList, mipRes(i), D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        struct BindlessDownsamplePayload
        {
            uint32_t srcIdx;
            uint32_t samplerIdx;
            uint32_t _pad[2];
            float texelSizeX, texelSizeY;
        };
        BindlessDownsamplePayload dp;
        dp.srcIdx = devForDestroy->bindlessSrvIndex(bloomMips[i]);
        dp.samplerIdx = 0;
        dp.texelSizeX = 1.0f / static_cast<float>(mipW[i]);
        dp.texelSizeY = 1.0f / static_cast<float>(mipH[i]);
        cmdList->SetGraphicsRoot32BitConstants(0, sizeof(dp) / 4, &dp, 0);
        auto rtv = getRtv(bloomMips[i + 1]);
        cmdList->OMSetRenderTargets(1, &rtv, false, nullptr);
        vp = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)mipW[i + 1], (float)mipH[i + 1]);
        sr = { 0, 0, (LONG)mipW[i + 1], (LONG)mipH[i + 1] };
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sr);
        cmdList->DrawInstanced(3, 1, 0, 0);
    }

    // Upsample (additive)
    cmdList->SetPipelineState(upsamplePSO.Get());
    for (int i = bloomMipCount - 2; i >= 0; --i) {
        transitionResource(
            cmdList, mipRes(i + 1), D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        transitionResource(
            cmdList, mipRes(i), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        struct BindlessUpsamplePayload
        {
            uint32_t srcIdx;
            uint32_t samplerIdx;
            uint32_t _pad[2];
            float texelSizeX, texelSizeY, intensity, _pad0;
        };
        BindlessUpsamplePayload up;
        up.srcIdx = devForDestroy->bindlessSrvIndex(bloomMips[i + 1]);
        up.samplerIdx = 0;
        up.texelSizeX = 1.0f / static_cast<float>(mipW[i + 1]);
        up.texelSizeY = 1.0f / static_cast<float>(mipH[i + 1]);
        up.intensity = 1.0f;
        cmdList->SetGraphicsRoot32BitConstants(0, sizeof(up) / 4, &up, 0);
        auto rtv = getRtv(bloomMips[i]);
        cmdList->OMSetRenderTargets(1, &rtv, false, nullptr);
        vp = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)mipW[i], (float)mipH[i]);
        sr = { 0, 0, (LONG)mipW[i], (LONG)mipH[i] };
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sr);
        cmdList->DrawInstanced(3, 1, 0, 0);
    }

    // Composite
    transitionResource(
        cmdList, mipRes(0), D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    cmdList->SetPipelineState(compositePSO.Get());
    struct BindlessCompositePayload
    {
        uint32_t sceneIdx;
        uint32_t bloomIdx;
        uint32_t samplerIdx;
        uint32_t _pad;
        float texelSizeX, texelSizeY;
        float bloomIntensity;
        uint32_t tonemapMode;
        float camForwardX, camForwardY, camForwardZ, _pad1;
        float camRightX, camRightY, camRightZ, _pad2;
        float camUpX, camUpY, camUpZ, _pad3;
        float sunDirX, sunDirY, sunDirZ, aspectRatio, tanHalfFov, frameTime;
    };
    BindlessCompositePayload cp;
    cp.sceneIdx = devForDestroy->bindlessSrvIndex(hdrRT);
    cp.bloomIdx = devForDestroy->bindlessSrvIndex(bloomMips[0]);
    cp.samplerIdx = 0;
    cp.texelSizeX = 1.0f / static_cast<float>(width);
    cp.texelSizeY = 1.0f / static_cast<float>(height);
    cp.bloomIntensity = intensity;
    cp.tonemapMode = (uint32_t)tonemapMode;
    cp.camForwardX = sky.camForward.x;
    cp.camForwardY = sky.camForward.y;
    cp.camForwardZ = sky.camForward.z;
    cp.camRightX = sky.camRight.x;
    cp.camRightY = sky.camRight.y;
    cp.camRightZ = sky.camRight.z;
    cp.camUpX = sky.camUp.x;
    cp.camUpY = sky.camUp.y;
    cp.camUpZ = sky.camUp.z;
    cp.sunDirX = sky.sunDir.x;
    cp.sunDirY = sky.sunDir.y;
    cp.sunDirZ = sky.sunDir.z;
    cp.aspectRatio = sky.aspectRatio;
    cp.tanHalfFov = sky.tanHalfFov;
    cp.frameTime = sky.time;
    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(cp) / 4, &cp, 0);
    cmdList->OMSetRenderTargets(1, &backBufRtv, false, nullptr);
    vp = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)width, (float)height);
    sr = { 0, 0, (LONG)width, (LONG)height };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sr);
    cmdList->DrawInstanced(3, 1, 0, 0);

    // hdr_rt: leave in PixelShaderResource state — render graph will lift it
    // back to RenderTarget before the next frame's scene pass.
    // Reset bloom mip chain to RENDER_TARGET for next frame's prefilter/downsample/upsample.
    for (uint32_t i = 0; i < bloomMipCount; ++i) {
        transitionResource(
            cmdList, mipRes(i), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
    }
}
