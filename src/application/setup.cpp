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
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    auto vsData = shaderCompiler.data(sceneVSIdx);
    auto psData = shaderCompiler.data(scenePSIdx);

    auto vs = vsData ? CD3DX12_SHADER_BYTECODE(vsData, shaderCompiler.size(sceneVSIdx))
                     : CD3DX12_SHADER_BYTECODE(g_vertex_shader, sizeof(g_vertex_shader));
    auto ps = psData ? CD3DX12_SHADER_BYTECODE(psData, shaderCompiler.size(scenePSIdx))
                     : CD3DX12_SHADER_BYTECODE(g_pixel_shader, sizeof(g_pixel_shader));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = this->rootSignature.Get();
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.VS = vs;
    psoDesc.PS = ps;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R11G11B10_FLOAT;
    psoDesc.SampleDesc = { 1, 0 };
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    CD3DX12_DEPTH_STENCIL_DESC dsDesc(D3D12_DEFAULT);
    dsDesc.StencilEnable = TRUE;
    dsDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    dsDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    dsDesc.BackFace = dsDesc.FrontFace;
    psoDesc.DepthStencilState = dsDesc;

    chkDX(this->device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&this->pipelineState)));
}

void Application::createGridPSO()
{
    if (!gridRootSig) {
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
        chkDX(device->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gridRootSig)
        ));
    }

    auto vsData = shaderCompiler.data(gridVSIdx);
    auto psData = shaderCompiler.data(gridPSIdx);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = gridRootSig.Get();
    psoDesc.VS = vsData ? CD3DX12_SHADER_BYTECODE(vsData, shaderCompiler.size(gridVSIdx))
                        : CD3DX12_SHADER_BYTECODE(g_grid_vs, sizeof(g_grid_vs));
    psoDesc.PS = psData ? CD3DX12_SHADER_BYTECODE(psData, shaderCompiler.size(gridPSIdx))
                        : CD3DX12_SHADER_BYTECODE(g_grid_ps, sizeof(g_grid_ps));
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R11G11B10_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc = { 1, 0 };
    chkDX(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&gridPSO)));
}

bool Application::loadContent()
{
    if (this->contentLoaded) {
        return true;
    }
    this->contentLoaded = true;
    spdlog::info("loadContent start");

    scene.createMegaBuffers(device.Get());
    scene.createDrawDataBuffers(device.Get());
    scene.loadTeapot(device.Get(), cmdQueue);

    for (const auto& entry : std::filesystem::directory_iterator(MODELS_DIR)) {
        if (entry.path().extension() == ".glb") {
            scene.loadGltf(entry.path().string(), device.Get(), cmdQueue, true);
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

    auto cmdList = cmdQueue.getCmdList();
    std::vector<ComPtr<ID3D12Resource>> temps;
    {
        Material terrainMat;
        terrainMat.name = "Terrain";
        terrainMat.albedo = { 0.2f, 0.4f, 0.8f, 1.0f };
        terrainMat.roughness = 0.1f;
        terrainMat.metallic = 0.0f;
        scene.materials.push_back(terrainMat);
        int terrainMatIdx = static_cast<int>(scene.materials.size()) - 1;

        MeshRef meshRef = scene.appendToMegaBuffers(
            device.Get(), cmdQueue, cmdList, terrainVerts, terrainIndices, terrainMatIdx, temps
        );

        Transform tf;
        tf.world = translate(0, tp.positionY, 0);
        scene.ecsWorld.entity().set(tf).set(meshRef).add<TerrainEntity>();
    }
    uint64_t fv = cmdQueue.execCmdList(cmdList);
    scene.trackUploadBatch(fv, std::move(temps));

    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        chkDX(this->device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&this->dsvHeap)));
    }

    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(this->device->CheckFeatureSupport(
            D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)
        ))) {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    CD3DX12_DESCRIPTOR_RANGE1 ranges[4];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);  // t0
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);  // t1
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);  // t2
    ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);  // t3

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
    chkDX(this->device->CreateRootSignature(
        0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
        IID_PPV_ARGS(&this->rootSignature)
    ));

    createScenePSO();
    createGridPSO();
    createCubemapResources();

    shadow.createResources(
        *gfxDevice, rootSignature.Get(),
        CD3DX12_SHADER_BYTECODE(g_vertex_shader, sizeof(g_vertex_shader)), scene.sceneSrvHeap.Get(),
        scene.sceneSrvDescSize, static_cast<INT>(app_slots::srvSlotShadow)
    );
    bloom.createResources(*gfxDevice, clientWidth, clientHeight);
    outline.createResources(
        device.Get(), rootSignature.Get(), { g_outline_vs, sizeof(g_outline_vs) },
        { g_outline_ps, sizeof(g_outline_ps) }
    );
    gbuffer.createResources(*gfxDevice, clientWidth, clientHeight);
    picker.createResources(device.Get(), clientWidth, clientHeight, rootSignature);

    billboards.init(device.Get(), cmdQueue.queue.Get(), L"resources/icons/light.png");
    gizmo.init(scene, device.Get(), cmdQueue);

    resizeDepthBuffer(clientWidth, clientHeight);
    ssao.createResources(
        *gfxDevice, clientWidth, clientHeight, gbuffer.resources[GBuffer::Normal].Get(),
        depthBuffer.Get(), scene.sceneSrvHeap.Get(), scene.sceneSrvDescSize,
        static_cast<INT>(app_slots::srvSlotSsao)
    );
    createGBufferPSO();

#ifdef TRACY_ENABLE
    UINT64 tsFreq = 0;
    if (SUCCEEDED(cmdQueue.queue->GetTimestampFrequency(&tsFreq)) && tsFreq > 0) {
        g_tracyD3d12Ctx = TracyD3D12Context(device.Get(), cmdQueue.queue.Get());
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
    D3D12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R11G11B10_FLOAT, cubemapResolution, cubemapResolution, 6, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
    );
    const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    chkDX(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr, IID_PPV_ARGS(&cubemapTexture)
    ));
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 6,
                                               D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
    chkDX(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&cubemapRtvHeap)));
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = { DXGI_FORMAT_R11G11B10_FLOAT,
                                              D3D12_RTV_DIMENSION_TEXTURE2DARRAY };
    rtvDesc.Texture2DArray.ArraySize = 1;
    rtvDesc.Texture2DArray.MipSlice = 0;
    UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(cubemapRtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (int i = 0; i < 6; ++i) {
        rtvDesc.Texture2DArray.FirstArraySlice = i;
        device->CreateRenderTargetView(cubemapTexture.Get(), &rtvDesc, rtvHandle);
        rtvHandle.Offset(1, rtvSize);
    }
    D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D32_FLOAT, cubemapResolution, cubemapResolution, 6, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
    );
    D3D12_CLEAR_VALUE depthClear = { DXGI_FORMAT_D32_FLOAT, { 1.0f, 0 } };
    chkDX(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
        IID_PPV_ARGS(&cubemapDepth)
    ));
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 6,
                                               D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
    chkDX(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&cubemapDsvHeap)));
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = { DXGI_FORMAT_D32_FLOAT,
                                              D3D12_DSV_DIMENSION_TEXTURE2DARRAY };
    dsvDesc.Texture2DArray.ArraySize = 1;
    dsvDesc.Texture2DArray.MipSlice = 0;
    UINT dsvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(cubemapDsvHeap->GetCPUDescriptorHandleForHeapStart());
    for (int i = 0; i < 6; ++i) {
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        device->CreateDepthStencilView(cubemapDepth.Get(), &dsvDesc, dsvHandle);
        dsvHandle.Offset(1, dsvSize);
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { DXGI_FORMAT_R11G11B10_FLOAT,
                                                D3D12_SRV_DIMENSION_TEXTURECUBE,
                                                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING };
    srvDesc.TextureCube.MipLevels = 1;
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
        scene.sceneSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        static_cast<INT>(app_slots::srvSlotCubemap), scene.sceneSrvDescSize
    );
    device->CreateShaderResourceView(cubemapTexture.Get(), &srvDesc, srvHandle);
}

void Application::createGBufferPSO()
{
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = this->rootSignature.Get();
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(g_vertex_shader, sizeof(g_vertex_shader));
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(g_gbuffer_ps, sizeof(g_gbuffer_ps));
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 4;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.RTVFormats[2] = DXGI_FORMAT_R8G8_UNORM;
    psoDesc.RTVFormats[3] = DXGI_FORMAT_R16G16_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc = { 1, 0 };
    chkDX(this->device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&this->gbufferPSO)));
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
        for (int i = 0; i < this->nBuffers; ++i) {
            this->backBuffers[i].Reset();
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
        picker.resize(device, clientWidth, height);
        ssao.resize(
            *gfxDevice, clientWidth, height, gbuffer.resources[GBuffer::Normal].Get(),
            depthBuffer.Get(), scene.sceneSrvHeap.Get(), scene.sceneSrvDescSize,
            static_cast<INT>(Scene::nBuffers + 2)
        );
        this->flush();
    }
    this->isResizing = false;
}
