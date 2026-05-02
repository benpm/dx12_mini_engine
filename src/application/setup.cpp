module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <flecs.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <wrl.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>
#include <vector>
#include "d3dx12_clean.h"
#include "gbuffer_ps_cso.h"
#include "grid_ps_cso.h"
#include "grid_vs_cso.h"
#include "id_ps_cso.h"
#include "normal_ps_cso.h"
#include "outline_ps_cso.h"
#include "outline_vs_cso.h"
#include "pixel_shader_cso.h"
#include "profiling.h"
#include "vertex_shader_cso.h"

module application;

#ifdef TRACY_ENABLE
extern TracyD3D12Ctx g_tracyD3d12Ctx;
#endif

using Microsoft::WRL::ComPtr;
using namespace DirectX;

#ifndef DXC_PATH
    #define DXC_PATH ""
#endif
#ifndef SHADER_SRC_DIR
    #define SHADER_SRC_DIR ""
#endif
#ifndef MODELS_DIR
    #define MODELS_DIR ""
#endif

// ---------------------------------------------------------------------------
// createScenePSO — (re)creates the scene pipeline state object
// ---------------------------------------------------------------------------

void Application::createScenePSO()
{
    // Tear down the previous PSO + shader handles (hot reload path).
    if (pipelineState.isValid()) {
        gfxDevice->destroy(pipelineState);
    }
    if (scenePsoVS.isValid()) {
        gfxDevice->destroy(scenePsoVS);
    }
    if (scenePsoPS.isValid()) {
        gfxDevice->destroy(scenePsoPS);
    }

    auto vsData = shaderCompiler.data(sceneVSIdx);
    auto psData = shaderCompiler.data(scenePSIdx);

    gfx::ShaderDesc vsDesc{};
    vsDesc.stage = gfx::ShaderStage::Vertex;
    vsDesc.bytecode = vsData ? vsData : static_cast<const void*>(g_vertex_shader);
    vsDesc.bytecodeSize = vsData ? shaderCompiler.size(sceneVSIdx) : sizeof(g_vertex_shader);
    vsDesc.debugName = "scene_vs";
    scenePsoVS = gfxDevice->createShader(vsDesc);

    gfx::ShaderDesc psDesc{};
    psDesc.stage = gfx::ShaderStage::Pixel;
    psDesc.bytecode = psData ? psData : static_cast<const void*>(g_pixel_shader);
    psDesc.bytecodeSize = psData ? shaderCompiler.size(scenePSIdx) : sizeof(g_pixel_shader);
    psDesc.debugName = "scene_ps";
    scenePsoPS = gfxDevice->createShader(psDesc);

    static constexpr gfx::VertexAttribute attrs[] = {
        { "POSITION", 0, gfx::Format::RGB32Float, 0 },
        { "NORMAL", 0, gfx::Format::RGB32Float, 12 },
        { "TEXCOORD", 0, gfx::Format::RG32Float, 24 },
    };

    gfx::GraphicsPipelineDesc gd{};
    gd.vs = scenePsoVS;
    gd.ps = scenePsoPS;
    gd.vertexAttributes = attrs;
    gd.vertexStride = 32;
    gd.topology = gfx::PrimitiveTopology::TriangleList;
    gd.numRenderTargets = 1;
    gd.renderTargetFormats[0] = gfx::Format::R11G11B10Float;
    gd.depthStencilFormat = gfx::Format::D24UnormS8Uint;
    gd.depthStencil.depthEnable = true;
    gd.depthStencil.depthWrite = true;
    gd.depthStencil.depthCompare = gfx::CompareOp::Less;
    gd.depthStencil.stencilEnable = true;
    gd.depthStencil.stencilCompare = gfx::CompareOp::Always;
    gd.depthStencil.stencilPass = gfx::StencilOp::Replace;
    gd.nativeRootSignatureOverride = this->rootSignature.Get();
    gd.debugName = "scene_pso";
    pipelineState = gfxDevice->createGraphicsPipeline(gd);
}

void Application::createGridPSO()
{
    if (!gridRootSig) {
        auto* d3dDev = static_cast<ID3D12Device2*>(gfxDevice->nativeHandle());
        CD3DX12_ROOT_PARAMETER1 rootParam;
        rootParam.InitAsConstantBufferView(
            0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL
        );
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
        rsDesc.Init_1_1(
            1, &rootParam, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
        );
        ComPtr<ID3DBlob> blob, err;
        chkDX(D3DX12SerializeVersionedRootSignature(
            &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &blob, &err
        ));
        chkDX(d3dDev->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gridRootSig)
        ));
    }

    if (gridPSO.isValid()) {
        gfxDevice->destroy(gridPSO);
    }
    if (gridVS.isValid()) {
        gfxDevice->destroy(gridVS);
    }
    if (gridPS.isValid()) {
        gfxDevice->destroy(gridPS);
    }

    auto vsData = shaderCompiler.data(gridVSIdx);
    auto psData = shaderCompiler.data(gridPSIdx);

    gfx::ShaderDesc vsDesc{};
    vsDesc.stage = gfx::ShaderStage::Vertex;
    vsDesc.bytecode = vsData ? vsData : static_cast<const void*>(g_grid_vs);
    vsDesc.bytecodeSize = vsData ? shaderCompiler.size(gridVSIdx) : sizeof(g_grid_vs);
    vsDesc.debugName = "grid_vs";
    gridVS = gfxDevice->createShader(vsDesc);

    gfx::ShaderDesc psDesc{};
    psDesc.stage = gfx::ShaderStage::Pixel;
    psDesc.bytecode = psData ? psData : static_cast<const void*>(g_grid_ps);
    psDesc.bytecodeSize = psData ? shaderCompiler.size(gridPSIdx) : sizeof(g_grid_ps);
    psDesc.debugName = "grid_ps";
    gridPS = gfxDevice->createShader(psDesc);

    gfx::GraphicsPipelineDesc gd{};
    gd.vs = gridVS;
    gd.ps = gridPS;
    // No vertex attributes — grid_vs synthesises a fullscreen tri from SV_VertexID.
    gd.topology = gfx::PrimitiveTopology::TriangleList;
    gd.rasterizer.cull = gfx::CullMode::None;
    gd.numRenderTargets = 1;
    gd.renderTargetFormats[0] = gfx::Format::R11G11B10Float;
    gd.depthStencilFormat = gfx::Format::D24UnormS8Uint;
    gd.depthStencil.depthEnable = true;
    gd.depthStencil.depthWrite = false;
    gd.depthStencil.depthCompare = gfx::CompareOp::Less;
    gd.blend[0].blendEnable = true;
    gd.blend[0].srcColor = gfx::BlendFactor::SrcAlpha;
    gd.blend[0].dstColor = gfx::BlendFactor::InvSrcAlpha;
    gd.blend[0].colorOp = gfx::BlendOp::Add;
    gd.blend[0].srcAlpha = gfx::BlendFactor::One;
    gd.blend[0].dstAlpha = gfx::BlendFactor::Zero;
    gd.blend[0].alphaOp = gfx::BlendOp::Add;
    gd.blend[0].writeMask = 0xF;
    gd.nativeRootSignatureOverride = gridRootSig.Get();
    gd.debugName = "grid_pso";
    gridPSO = gfxDevice->createGraphicsPipeline(gd);
}

bool Application::loadContent()
{
    if (this->contentLoaded) {
        return true;
    }
    this->contentLoaded = true;
    spdlog::info("loadContent start");
    auto* d3dDev = static_cast<ID3D12Device2*>(gfxDevice->nativeHandle());

    scene.createMegaBuffers(*gfxDevice);
    scene.createDrawDataBuffers(*gfxDevice);
    scene.loadTeapot(*gfxDevice, cmdQueue);

    for (const auto& entry : std::filesystem::directory_iterator(MODELS_DIR)) {
        if (entry.path().extension() == ".glb") {
            scene.loadGltf(entry.path().string(), *gfxDevice, cmdQueue, true);
        }
    }

    TerrainParams tp;
    tp.gridSize = 256;
    tp.worldSize = 128.0f;
    tp.heightScale = 8.0f;
    tp.octaves = 4;
    tp.frequency = 2.0f;
    tp.positionY = -30.0f;

    std::vector<VertexPBR> terrainVerts;
    std::vector<uint32_t> terrainIndices;
    generateTerrain(tp, terrainVerts, terrainIndices);

    {
        Material terrainMat;
        terrainMat.name = "Terrain";
        terrainMat.albedo = { 0.2f, 0.4f, 0.8f, 1.0f };
        terrainMat.roughness = 0.1f;
        terrainMat.metallic = 0.0f;
        scene.materials.push_back(terrainMat);
        int terrainMatIdx = static_cast<int>(scene.materials.size()) - 1;

        MeshRef meshRef = scene.appendToMegaBuffers(
            *gfxDevice, cmdQueue, terrainVerts, terrainIndices, terrainMatIdx
        );

        Transform tf;
        tf.world = translate(0, tp.positionY, 0);
        scene.ecsWorld.entity().set(tf).set(meshRef).add<TerrainEntity>();
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        chkDX(d3dDev->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&this->dsvHeap)));
    }

    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(d3dDev->CheckFeatureSupport(
            D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)
        ))) {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    // VOLATILE: descriptor contents are stable but offsets into the bindless heap vary per draw.
    CD3DX12_DESCRIPTOR_RANGE1 ranges[4];
    ranges[0].Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
    );  // t0
    ranges[1].Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
    );  // t1
    ranges[2].Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
    );  // t2
    ranges[3].Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
    );  // t3

    CD3DX12_ROOT_PARAMETER1 rootParams[8];
    rootParams[app_slots::rootPerFrameCB].InitAsConstantBufferView(
        0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL
    );
    rootParams[app_slots::rootPerPassCB].InitAsConstantBufferView(
        1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL
    );
    rootParams[app_slots::rootDrawIndex].InitAsConstants(1, 2, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[app_slots::rootOutlineParams].InitAsConstants(4, 3, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[app_slots::rootPerObjectSrv].InitAsDescriptorTable(
        1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL
    );
    rootParams[app_slots::rootShadowSrv].InitAsDescriptorTable(
        1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL
    );
    rootParams[app_slots::rootCubemapSrv].InitAsDescriptorTable(
        1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL
    );
    rootParams[app_slots::rootSsaoSrv].InitAsDescriptorTable(
        1, &ranges[3], D3D12_SHADER_VISIBILITY_PIXEL
    );

    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
    staticSamplers[0].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    staticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[1].ShaderRegister = 1;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    const D3D12_ROOT_SIGNATURE_FLAGS rootSigFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(8, rootParams, 2, staticSamplers, rootSigFlags);

    ComPtr<ID3DBlob> rootSigBlob, errorBlob;
    chkDX(D3DX12SerializeVersionedRootSignature(
        &rootSigDesc, featureData.HighestVersion, &rootSigBlob, &errorBlob
    ));
    chkDX(d3dDev->CreateRootSignature(
        0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
        IID_PPV_ARGS(&this->rootSignature)
    ));

    createScenePSO();
    createGridPSO();
    createCubemapResources();

    shadow.createResources(
        *gfxDevice, rootSignature.Get(),
        CD3DX12_SHADER_BYTECODE(g_vertex_shader, sizeof(g_vertex_shader))
    );
    // Create typed R32_FLOAT SRV for the shadow map (R32Typeless resource) in the gfx heap.
    shadowSrvIdx = gfxDevice->createTypedSrv(shadow.shadowMap, gfx::Format::R32Float);

    bloom.createResources(*gfxDevice, clientWidth, clientHeight);
    outline.createResources(
        *gfxDevice, rootSignature.Get(), { g_outline_vs, sizeof(g_outline_vs) },
        { g_outline_ps, sizeof(g_outline_ps) }
    );
    gbuffer.createResources(*gfxDevice, clientWidth, clientHeight);
    picker.createResources(*gfxDevice, clientWidth, clientHeight, rootSignature);

    billboards.init(*gfxDevice, L"resources/icons/light.png");
    gizmo.init(scene, *gfxDevice, cmdQueue);

    resizeDepthBuffer(clientWidth, clientHeight);
    ssao.createResources(
        *gfxDevice, clientWidth, clientHeight, gbuffer.resources[GBuffer::Normal], depthBuffer
    );
    createGBufferPSO();

#ifdef TRACY_ENABLE
    UINT64 tsFreq = 0;
    if (SUCCEEDED(cmdQueue.queue->GetTimestampFrequency(&tsFreq)) && tsFreq > 0) {
        g_tracyD3d12Ctx = TracyD3D12Context(d3dDev, cmdQueue.queue.Get());
    }
#endif

    if (shaderCompiler.init(DXC_PATH, SHADER_SRC_DIR)) {
        sceneVSIdx = shaderCompiler.watch("vertex_shader.hlsl", "vs_6_0");
        scenePSIdx = shaderCompiler.watch("pixel_shader.hlsl", "ps_6_0");
        outlineVSIdx = shaderCompiler.watch("outline_vs.hlsl", "vs_6_0");
        outlinePSIdx = shaderCompiler.watch("outline_ps.hlsl", "ps_6_0");
        bloomFsVsIdx = shaderCompiler.watch("fullscreen_vs.hlsl", "vs_6_0");
        bloomPreIdx = shaderCompiler.watch("bloom_prefilter_ps.hlsl", "ps_6_0");
        bloomDownIdx = shaderCompiler.watch("bloom_downsample_ps.hlsl", "ps_6_0");
        bloomUpIdx = shaderCompiler.watch("bloom_upsample_ps.hlsl", "ps_6_0");
        bloomCompIdx = shaderCompiler.watch("bloom_composite_ps.hlsl", "ps_6_0");
        gridVSIdx = shaderCompiler.watch("grid_vs.hlsl", "vs_6_0");
        gridPSIdx = shaderCompiler.watch("grid_ps.hlsl", "ps_6_0");
        shaderCompiler.watch("gbuffer_ps.hlsl", "ps_6_0");
        spdlog::info("Shader hot reload enabled (DXC: {})", DXC_PATH);
    }

    spdlog::info("loadContent finished");
    return true;
}

void Application::createCubemapResources()
{
    auto* d3dDev = static_cast<ID3D12Device2*>(gfxDevice->nativeHandle());
    if (cubemapTexture.isValid()) {
        gfxDevice->destroy(cubemapTexture);
    }
    if (cubemapDepth.isValid()) {
        gfxDevice->destroy(cubemapDepth);
    }

    {
        gfx::TextureDesc td{};
        td.width = cubemapResolution;
        td.height = cubemapResolution;
        td.depthOrArraySize = 6;
        td.format = gfx::Format::R11G11B10Float;
        td.usage = gfx::TextureUsage::RenderTarget | gfx::TextureUsage::ShaderResource;
        td.initialState = gfx::ResourceState::PixelShaderResource;
        td.isCubemap = true;
        td.debugName = "cubemap";
        cubemapTexture = gfxDevice->createTexture(td);
    }
    auto* cubemapRes = static_cast<ID3D12Resource*>(gfxDevice->nativeResource(cubemapTexture));

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 6,
                                               D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
    chkDX(d3dDev->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&cubemapRtvHeap)));
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = { DXGI_FORMAT_R11G11B10_FLOAT,
                                              D3D12_RTV_DIMENSION_TEXTURE2DARRAY };
    rtvDesc.Texture2DArray.ArraySize = 1;
    rtvDesc.Texture2DArray.MipSlice = 0;
    UINT rtvSize = d3dDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(cubemapRtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (int i = 0; i < 6; ++i) {
        rtvDesc.Texture2DArray.FirstArraySlice = i;
        d3dDev->CreateRenderTargetView(cubemapRes, &rtvDesc, rtvHandle);
        rtvHandle.Offset(1, rtvSize);
    }

    {
        gfx::TextureDesc td{};
        td.width = cubemapResolution;
        td.height = cubemapResolution;
        td.depthOrArraySize = 6;
        td.format = gfx::Format::D32Float;
        td.usage = gfx::TextureUsage::DepthStencil;
        td.initialState = gfx::ResourceState::DepthWrite;
        td.useClearValue = true;
        td.clearDepth = 1.0f;
        td.debugName = "cubemap_depth";
        cubemapDepth = gfxDevice->createTexture(td);
    }
    auto* cubemapDepthRes = static_cast<ID3D12Resource*>(gfxDevice->nativeResource(cubemapDepth));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 6,
                                               D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
    chkDX(d3dDev->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&cubemapDsvHeap)));
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = { DXGI_FORMAT_D32_FLOAT,
                                              D3D12_DSV_DIMENSION_TEXTURE2DARRAY };
    dsvDesc.Texture2DArray.ArraySize = 1;
    dsvDesc.Texture2DArray.MipSlice = 0;
    UINT dsvSize = d3dDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(cubemapDsvHeap->GetCPUDescriptorHandleForHeapStart());
    for (int i = 0; i < 6; ++i) {
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        d3dDev->CreateDepthStencilView(cubemapDepthRes, &dsvDesc, dsvHandle);
        dsvHandle.Offset(1, dsvSize);
    }
    // The cubemap SRV is auto-created in the gfx bindless heap via TextureUsage::ShaderResource.
    // Access it via gfxDevice->bindlessSrvIndex(cubemapTexture) in the render pass.
    (void)cubemapRes;
}

void Application::createGBufferPSO()
{
    if (gbufferPSO.isValid()) {
        gfxDevice->destroy(gbufferPSO);
    }
    if (gbufferVS.isValid()) {
        gfxDevice->destroy(gbufferVS);
    }
    if (gbufferPS.isValid()) {
        gfxDevice->destroy(gbufferPS);
    }

    gfx::ShaderDesc vsDesc{};
    vsDesc.stage = gfx::ShaderStage::Vertex;
    vsDesc.bytecode = g_vertex_shader;
    vsDesc.bytecodeSize = sizeof(g_vertex_shader);
    vsDesc.debugName = "gbuffer_vs";
    gbufferVS = gfxDevice->createShader(vsDesc);

    gfx::ShaderDesc psDesc{};
    psDesc.stage = gfx::ShaderStage::Pixel;
    psDesc.bytecode = g_gbuffer_ps;
    psDesc.bytecodeSize = sizeof(g_gbuffer_ps);
    psDesc.debugName = "gbuffer_ps";
    gbufferPS = gfxDevice->createShader(psDesc);

    static constexpr gfx::VertexAttribute attrs[] = {
        { "POSITION", 0, gfx::Format::RGB32Float, 0 },
        { "NORMAL", 0, gfx::Format::RGB32Float, 12 },
        { "TEXCOORD", 0, gfx::Format::RG32Float, 24 },
    };

    gfx::GraphicsPipelineDesc gd{};
    gd.vs = gbufferVS;
    gd.ps = gbufferPS;
    gd.vertexAttributes = attrs;
    gd.vertexStride = 32;
    gd.topology = gfx::PrimitiveTopology::TriangleList;
    gd.numRenderTargets = 4;
    gd.renderTargetFormats[0] = gfx::Format::RGBA8Unorm;
    gd.renderTargetFormats[1] = gfx::Format::RGBA8Unorm;
    gd.renderTargetFormats[2] = gfx::Format::RG8Unorm;
    gd.renderTargetFormats[3] = gfx::Format::RG16Float;
    gd.depthStencilFormat = gfx::Format::D24UnormS8Uint;
    gd.depthStencil.depthEnable = true;
    gd.depthStencil.depthWrite = true;
    gd.depthStencil.depthCompare = gfx::CompareOp::Less;
    gd.nativeRootSignatureOverride = this->rootSignature.Get();
    gd.debugName = "gbuffer_pso";
    gbufferPSO = gfxDevice->createGraphicsPipeline(gd);
}

void Application::onResize(uint32_t width, uint32_t height)
{
    if (this->isResizing || width == 0 || height == 0) {
        return;
    }
    this->isResizing = true;
    this->clientWidth = width;
    this->clientHeight = height;
    if (this->isInitialized) {
        this->cmdQueue.flush();
        this->renderGraph.reset();
        // gfxSwapChain->resize() releases its old back-buffer texture handles
        // internally before calling ResizeBuffers. Just clear our cached
        // handles — the new ones come from updateRenderTargetViews below.
        for (int i = 0; i < this->nBuffers; ++i) {
            this->backBuffers[i] = {};
        }
        this->gfxSwapChain->resize(this->clientWidth, this->clientHeight);
        this->curBackBufIdx = this->gfxSwapChain->currentIndex();
        this->updateRenderTargetViews(this->rtvHeap);
        this->viewport = CD3DX12_VIEWPORT(
            0.0f, 0.0f, static_cast<float>(this->clientWidth),
            static_cast<float>(this->clientHeight)
        );
        this->scissorRect = CD3DX12_RECT(
            0, 0, static_cast<LONG>(this->clientWidth), static_cast<LONG>(this->clientHeight)
        );
        this->resizeDepthBuffer(this->clientWidth, this->clientHeight);
        gbuffer.resize(*gfxDevice, clientWidth, height);
        bloom.resize(*gfxDevice, clientWidth, height);
        picker.resize(*gfxDevice, clientWidth, height);
        ssao.resize(
            *gfxDevice, clientWidth, height, gbuffer.resources[GBuffer::Normal], depthBuffer
        );
        this->flush();
    }
    this->isResizing = false;
}
