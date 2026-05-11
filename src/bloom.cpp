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

// Build (and replace) the bloom pipeline state objects through gfx. All four
// pipelines share the same fullscreen-quad VS; only the PS and blend state
// differ. The bindless root sig is used by default (passing
// nativeRootSignatureOverride = nullptr); bloom shaders compile with
// USE_BINDLESS so their register layout already matches.
static gfx::PipelineHandle makeBloomPipeline(
    gfx::IDevice& dev, gfx::ShaderHandle vs, gfx::ShaderHandle ps, gfx::Format rtFormat,
    bool additiveBlend, const char* debugName
)
{
    gfx::GraphicsPipelineDesc pd{};
    pd.vs = vs;
    pd.ps = ps;
    pd.rasterizer.cull = gfx::CullMode::None;
    pd.depthStencil.depthEnable = false;
    pd.depthStencil.depthWrite = false;
    pd.depthStencil.stencilEnable = false;
    pd.numRenderTargets = 1;
    pd.renderTargetFormats[0] = rtFormat;
    if (additiveBlend) {
        pd.blend[0].blendEnable = true;
        pd.blend[0].srcColor = gfx::BlendFactor::One;
        pd.blend[0].dstColor = gfx::BlendFactor::One;
        pd.blend[0].colorOp = gfx::BlendOp::Add;
        pd.blend[0].srcAlpha = gfx::BlendFactor::One;
        pd.blend[0].dstAlpha = gfx::BlendFactor::One;
        pd.blend[0].alphaOp = gfx::BlendOp::Add;
    }
    pd.debugName = debugName;
    return dev.createGraphicsPipeline(pd);
}

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
        for (auto h : { prefilterPSO, downsamplePSO, upsamplePSO, compositePSO }) {
            if (h.isValid()) {
                devForDestroy->destroy(h);
            }
        }
        for (auto h : { vsHandle, prefilterPSShader, downsamplePSShader, upsamplePSShader,
                        compositePSShader }) {
            if (h.isValid()) {
                devForDestroy->destroy(h);
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
    // Defer to reloadPipelines so the shader-hotreload path and the initial
    // create path share one builder.
    reloadPipelines(dev);
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

void BloomRenderer::reloadPipelines(
    gfx::IDevice& dev,
    gfx::ShaderBytecode fullscreenVS,
    gfx::ShaderBytecode prefilterPS,
    gfx::ShaderBytecode downsamplePS,
    gfx::ShaderBytecode upsamplePS,
    gfx::ShaderBytecode compositePS
)
{
    devForDestroy = &dev;

    // Resolve bytecode: caller-supplied (for hot reload) or the compiled-in
    // defaults baked into the engine at build time.
    auto resolve = [](gfx::ShaderBytecode bc, const BYTE* def, size_t defSize) {
        return bc.data ? bc : gfx::ShaderBytecode{ def, defSize };
    };
    auto vsBc = resolve(fullscreenVS, g_fullscreen_vs, sizeof(g_fullscreen_vs));
    auto preBc = resolve(prefilterPS, g_bloom_prefilter_ps, sizeof(g_bloom_prefilter_ps));
    auto downBc = resolve(downsamplePS, g_bloom_downsample_ps, sizeof(g_bloom_downsample_ps));
    auto upBc = resolve(upsamplePS, g_bloom_upsample_ps, sizeof(g_bloom_upsample_ps));
    auto compBc = resolve(compositePS, g_bloom_composite_ps, sizeof(g_bloom_composite_ps));

    // Release old handles before allocating new ones; destroy is fence-tracked
    // inside gfx so it's safe mid-frame.
    auto destroyPSO = [&](gfx::PipelineHandle& h) {
        if (h.isValid()) {
            dev.destroy(h);
            h = {};
        }
    };
    auto destroyShader = [&](gfx::ShaderHandle& h) {
        if (h.isValid()) {
            dev.destroy(h);
            h = {};
        }
    };
    destroyPSO(prefilterPSO);
    destroyPSO(downsamplePSO);
    destroyPSO(upsamplePSO);
    destroyPSO(compositePSO);
    destroyShader(vsHandle);
    destroyShader(prefilterPSShader);
    destroyShader(downsamplePSShader);
    destroyShader(upsamplePSShader);
    destroyShader(compositePSShader);

    auto makeShader = [&](gfx::ShaderBytecode bc, gfx::ShaderStage stage, const char* name) {
        gfx::ShaderDesc sd{};
        sd.bytecode = bc.data;
        sd.bytecodeSize = bc.size;
        sd.stage = stage;
        sd.debugName = name;
        return dev.createShader(sd);
    };

    vsHandle = makeShader(vsBc, gfx::ShaderStage::Vertex, "bloom_fullscreen_vs");
    prefilterPSShader = makeShader(preBc, gfx::ShaderStage::Pixel, "bloom_prefilter_ps");
    downsamplePSShader = makeShader(downBc, gfx::ShaderStage::Pixel, "bloom_downsample_ps");
    upsamplePSShader = makeShader(upBc, gfx::ShaderStage::Pixel, "bloom_upsample_ps");
    compositePSShader = makeShader(compBc, gfx::ShaderStage::Pixel, "bloom_composite_ps");

    prefilterPSO = makeBloomPipeline(
        dev, vsHandle, prefilterPSShader, gfx::Format::R11G11B10Float, false, "bloom_prefilter"
    );
    downsamplePSO = makeBloomPipeline(
        dev, vsHandle, downsamplePSShader, gfx::Format::R11G11B10Float, false, "bloom_downsample"
    );
    upsamplePSO = makeBloomPipeline(
        dev, vsHandle, upsamplePSShader, gfx::Format::R11G11B10Float, true, "bloom_upsample"
    );
    compositePSO = makeBloomPipeline(
        dev, vsHandle, compositePSShader, gfx::Format::RGBA8Unorm, false, "bloom_composite"
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
    cmdRef.bindPipeline(prefilterPSO);
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
    cmdRef.bindPipeline(downsamplePSO);
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
    cmdRef.bindPipeline(upsamplePSO);
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
    cmdRef.bindPipeline(compositePSO);
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
