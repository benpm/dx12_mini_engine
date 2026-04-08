module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <d3d12.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <flecs.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <wrl.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include "d3dx12_clean.h"
#include "grid_ps_cso.h"
#include "grid_vs_cso.h"
#include "normal_ps_cso.h"
#include "pixel_shader_cso.h"
#include "profiling.h"
#include "vertex_shader_cso.h"

module application;

#ifdef TRACY_ENABLE
extern TracyD3D12Ctx g_tracyD3d12Ctx;
#else
extern void* g_tracyD3d12Ctx;
#endif

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// Convert HSL to linear RGB. hue in [0,360), sat and light in [0,1].
static vec4 hslToLinear(float hue, float sat, float light)
{
    float c = (1.0f - std::abs(2.0f * light - 1.0f)) * sat;
    float x = c * (1.0f - std::abs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    float m = light - c * 0.5f;
    float r, g, b;
    int seg = static_cast<int>(hue / 60.0f) % 6;
    switch (seg) {
        case 0:
            r = c;
            g = x;
            b = 0;
            break;
        case 1:
            r = x;
            g = c;
            b = 0;
            break;
        case 2:
            r = 0;
            g = c;
            b = x;
            break;
        case 3:
            r = 0;
            g = x;
            b = c;
            break;
        case 4:
            r = x;
            g = 0;
            b = c;
            break;
        default:
            r = c;
            g = 0;
            b = x;
            break;
    }
    auto lin = [](float v) { return std::pow(v, 2.2f); };
    return { lin(r + m), lin(g + m), lin(b + m), 1.0f };
}

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
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    auto vsData = shaderCompiler.data(sceneVSIdx);
    auto psData = shaderCompiler.data(scenePSIdx);
    auto vs = vsData ? CD3DX12_SHADER_BYTECODE(vsData, shaderCompiler.size(sceneVSIdx))
                     : CD3DX12_SHADER_BYTECODE(g_vertex_shader, sizeof(g_vertex_shader));
    auto ps = psData ? CD3DX12_SHADER_BYTECODE(psData, shaderCompiler.size(scenePSIdx))
                     : CD3DX12_SHADER_BYTECODE(g_pixel_shader, sizeof(g_pixel_shader));

    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS PS;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencil;
    } pipelineStateStream;
    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R11G11B10_FLOAT;
    pipelineStateStream.pRootSignature = this->rootSignature.Get();
    pipelineStateStream.InputLayout = { inputLayout, _countof(inputLayout) };
    pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.VS = vs;
    pipelineStateStream.PS = ps;
    pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    pipelineStateStream.RTVFormats = rtvFormats;
    {
        CD3DX12_DEPTH_STENCIL_DESC dsDesc(D3D12_DEFAULT);
        dsDesc.StencilEnable = TRUE;
        dsDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
        dsDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        dsDesc.BackFace = dsDesc.FrontFace;
        pipelineStateStream.DepthStencil = dsDesc;
    }
    D3D12_PIPELINE_STATE_STREAM_DESC psoDesc = { sizeof(PipelineStateStream),
                                                 &pipelineStateStream };
    chkDX(this->device->CreatePipelineState(&psoDesc, IID_PPV_ARGS(&this->pipelineState)));
}

// ---------------------------------------------------------------------------
// createGridPSO — infinite grid root signature + PSO
// ---------------------------------------------------------------------------

void Application::createGridPSO()
{
    // Root signature: single CBV at b0 (GridCB: ViewProj, InvViewProj, CameraPos)
    if (!gridRootSig) {
        CD3DX12_ROOT_PARAMETER1 rootParam;
        rootParam.InitAsConstantBufferView(
            0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL
        );
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
        rsDesc.Init_1_1(1, &rootParam, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ComPtr<ID3DBlob> blob, err;
        D3D12_FEATURE_DATA_ROOT_SIGNATURE feat = { D3D_ROOT_SIGNATURE_VERSION_1_1 };
        if (FAILED(
                device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feat, sizeof(feat))
            )) {
            feat.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }
        chkDX(D3DX12SerializeVersionedRootSignature(&rsDesc, feat.HighestVersion, &blob, &err));
        chkDX(device->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gridRootSig)
        ));
    }

    auto vsData = shaderCompiler.data(gridVSIdx);
    auto psData = shaderCompiler.data(gridPSIdx);
    auto vs = vsData ? CD3DX12_SHADER_BYTECODE(vsData, shaderCompiler.size(gridVSIdx))
                     : CD3DX12_SHADER_BYTECODE(g_grid_vs, sizeof(g_grid_vs));
    auto ps = psData ? CD3DX12_SHADER_BYTECODE(psData, shaderCompiler.size(gridPSIdx))
                     : CD3DX12_SHADER_BYTECODE(g_grid_ps, sizeof(g_grid_ps));

    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS PS;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC Blend;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencil;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER Rasterizer;
    } pss;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R11G11B10_FLOAT;

    pss.pRootSignature = gridRootSig.Get();
    pss.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pss.VS = vs;
    pss.PS = ps;
    pss.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    pss.RTVFormats = rtvFormats;

    // Alpha blending
    CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pss.Blend = blendDesc;

    // Depth test enabled (read), depth write disabled (transparent)
    CD3DX12_DEPTH_STENCIL_DESC dsDesc(D3D12_DEFAULT);
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pss.DepthStencil = dsDesc;

    // No face culling
    CD3DX12_RASTERIZER_DESC rastDesc(D3D12_DEFAULT);
    rastDesc.CullMode = D3D12_CULL_MODE_NONE;
    pss.Rasterizer = rastDesc;

    D3D12_PIPELINE_STATE_STREAM_DESC psoDesc = { sizeof(pss), &pss };
    chkDX(device->CreatePipelineState(&psoDesc, IID_PPV_ARGS(&gridPSO)));
}

// ---------------------------------------------------------------------------
// createCubemapResources — environment cubemap for reflections
// ---------------------------------------------------------------------------

void Application::createCubemapResources()
{
    const uint32_t res = cubemapResolution;

    // Color cubemap (R11G11B10_FLOAT, 6 faces)
    {
        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        clearVal.Color[0] = clearVal.Color[1] = clearVal.Color[2] = 0.0f;
        clearVal.Color[3] = 1.0f;
        const CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R11G11B10_FLOAT, res, res, 6, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
        );
        chkDX(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearVal,
            IID_PPV_ARGS(&cubemapTexture)
        ));
    }

    // Depth buffer for cubemap rendering (D32_FLOAT, 6 faces)
    {
        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = DXGI_FORMAT_D32_FLOAT;
        clearVal.DepthStencil = { 1.0f, 0 };
        const CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_D32_FLOAT, res, res, 6, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
        );
        chkDX(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearVal,
            IID_PPV_ARGS(&cubemapDepth)
        ));
    }

    // RTV heap (6 descriptors, one per face)
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 6;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        chkDX(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&cubemapRtvHeap)));
        cubemapRtvDescSize =
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (uint32_t i = 0; i < 6; ++i) {
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.FirstArraySlice = i;
            rtvDesc.Texture2DArray.ArraySize = 1;
            rtvDesc.Texture2DArray.MipSlice = 0;
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
                cubemapRtvHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(i),
                cubemapRtvDescSize
            );
            device->CreateRenderTargetView(cubemapTexture.Get(), &rtvDesc, handle);
        }
    }

    // DSV heap (6 descriptors, one per face)
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 6;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        chkDX(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&cubemapDsvHeap)));
        cubemapDsvDescSize =
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        for (uint32_t i = 0; i < 6; ++i) {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dsvDesc.Texture2DArray.FirstArraySlice = i;
            dsvDesc.Texture2DArray.ArraySize = 1;
            dsvDesc.Texture2DArray.MipSlice = 0;
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
                cubemapDsvHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(i),
                cubemapDsvDescSize
            );
            device->CreateDepthStencilView(cubemapDepth.Get(), &dsvDesc, handle);
        }
    }

    // SRV in sceneSrvHeap at slot nBuffers+1 (after shadow map SRV)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.TextureCube.MipLevels = 1;
        srvDesc.TextureCube.MostDetailedMip = 0;
        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
            scene.sceneSrvHeap->GetCPUDescriptorHandleForHeapStart(),
            static_cast<INT>(app_slots::srvSlotCubemap), scene.sceneSrvDescSize
        );
        device->CreateShaderResourceView(cubemapTexture.Get(), &srvDesc, handle);
    }

    spdlog::info("Created cubemap resources ({}x{})", res, res);
}

// ---------------------------------------------------------------------------
// loadContent — creates pipeline + uploads default teapot scene
// ---------------------------------------------------------------------------

bool Application::loadContent()
{
    spdlog::info("loadContent start");

    scene.createMegaBuffers(device.Get());
    scene.createDrawDataBuffers(device.Get());
    scene.loadTeapot(device.Get(), cmdQueue);

    // Load all GLB models from resources/models/
    for (const auto& entry : std::filesystem::directory_iterator(MODELS_DIR)) {
        if (entry.path().extension() == ".glb") {
            spdlog::info("Loading model: {}", entry.path().filename().string());
            scene.loadGltf(entry.path().string(), device.Get(), cmdQueue, true);
        }
    }

    // Generate terrain
    {
        TerrainParams tp;
        std::vector<VertexPBR> terrainVerts;
        std::vector<uint32_t> terrainIndices;
        generateTerrain(tp, terrainVerts, terrainIndices);

        auto cmdList = cmdQueue.getCmdList();
        std::vector<ComPtr<ID3D12Resource>> temps;

        Material terrainMat;
        terrainMat.albedo = tp.materialAlbedo;
        terrainMat.roughness = tp.materialRoughness;
        terrainMat.metallic = 0.0f;
        terrainMat.reflective = false;
        scene.materials.push_back(terrainMat);
        int terrainMatIdx = static_cast<int>(scene.materials.size()) - 1;

        MeshRef terrainMesh =
            scene.appendToMegaBuffers(cmdList, terrainVerts, terrainIndices, terrainMatIdx, temps);
        uint64_t fv = cmdQueue.execCmdList(cmdList);
        scene.trackUploadBatch(fv, std::move(temps));

        Transform tf;
        tf.world = translate(0.0f, tp.positionY, 0.0f);
        scene.ecsWorld.entity().set(tf).set(terrainMesh).add<Pickable>().add<TerrainEntity>();
        spdlog::info(
            "Terrain: {}x{} grid, {} verts, {} tris", tp.gridSize, tp.gridSize, terrainVerts.size(),
            terrainIndices.size() / 3
        );
    }

    // DSV heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        chkDX(this->device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&this->dsvHeap)));
    }

    // Root signature (SRV descriptor table + 1 root constant)
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(this->device->CheckFeatureSupport(
            D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)
        ))) {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    const D3D12_ROOT_SIGNATURE_FLAGS rootSigFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    CD3DX12_DESCRIPTOR_RANGE1 srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);  // t0: structured buffer

    CD3DX12_DESCRIPTOR_RANGE1 shadowSrvRange;
    shadowSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);  // t1: shadow map

    CD3DX12_DESCRIPTOR_RANGE1 cubemapSrvRange;
    cubemapSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);  // t2: cubemap

    CD3DX12_DESCRIPTOR_RANGE1 ssaoSrvRange;
    ssaoSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);  // t3: SSAO

    CD3DX12_ROOT_PARAMETER1 rootParams[8];
    rootParams[app_slots::rootPerFrameCB].InitAsConstantBufferView(
        0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL
    );  // b0: PerFrameCB
    rootParams[app_slots::rootPerPassCB].InitAsConstantBufferView(
        1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL
    );  // b1: PerPassCB
    rootParams[app_slots::rootDrawIndex].InitAsConstants(
        1, 2, 0, D3D12_SHADER_VISIBILITY_ALL
    );  // b2: drawIndex
    rootParams[app_slots::rootOutlineParams].InitAsConstants(
        4, 3, 0, D3D12_SHADER_VISIBILITY_ALL
    );  // b3: outline params
    rootParams[app_slots::rootPerObjectSrv].InitAsDescriptorTable(
        1, &srvRange, D3D12_SHADER_VISIBILITY_ALL
    );  // t0: PerObjectData
    rootParams[app_slots::rootShadowSrv].InitAsDescriptorTable(
        1, &shadowSrvRange, D3D12_SHADER_VISIBILITY_PIXEL
    );  // t1: shadow map
    rootParams[app_slots::rootCubemapSrv].InitAsDescriptorTable(
        1, &cubemapSrvRange, D3D12_SHADER_VISIBILITY_PIXEL
    );  // t2: cubemap
    rootParams[app_slots::rootSsaoSrv].InitAsDescriptorTable(
        1, &ssaoSrvRange, D3D12_SHADER_VISIBILITY_PIXEL
    );  // t3: SSAO

    // Static samplers: s0 = shadow comparison, s1 = cubemap linear
    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
    // s0: shadow
    staticSamplers[0].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].RegisterSpace = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // s1: cubemap linear
    staticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[1].ShaderRegister = 1;
    staticSamplers[1].RegisterSpace = 0;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 2, staticSamplers, rootSigFlags);

    ComPtr<ID3DBlob> rootSigBlob, errorBlob;
    chkDX(D3DX12SerializeVersionedRootSignature(
        &rootSigDesc, featureData.HighestVersion, &rootSigBlob, &errorBlob
    ));
    chkDX(this->device->CreateRootSignature(
        0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
        IID_PPV_ARGS(&this->rootSignature)
    ));

    createScenePSO();

    // Shadow map resources
    {
        auto vsData = shaderCompiler.data(sceneVSIdx);
        D3D12_SHADER_BYTECODE vs =
            vsData ? D3D12_SHADER_BYTECODE{ vsData, shaderCompiler.size(sceneVSIdx) }
                   : D3D12_SHADER_BYTECODE{};
        shadow.createResources(
            device.Get(), rootSignature.Get(), vs, scene.sceneSrvHeap.Get(), scene.sceneSrvDescSize,
            static_cast<INT>(app_slots::srvSlotShadow)
        );
    }

    // Outline PSO
    {
        auto vsData = shaderCompiler.data(outlineVSIdx);
        auto psData = shaderCompiler.data(outlinePSIdx);
        D3D12_SHADER_BYTECODE vs =
            vsData ? D3D12_SHADER_BYTECODE{ vsData, shaderCompiler.size(outlineVSIdx) }
                   : D3D12_SHADER_BYTECODE{};
        D3D12_SHADER_BYTECODE ps =
            psData ? D3D12_SHADER_BYTECODE{ psData, shaderCompiler.size(outlinePSIdx) }
                   : D3D12_SHADER_BYTECODE{};
        outline.createResources(device.Get(), rootSignature.Get(), vs, ps);
    }
    createCubemapResources();
    createGridPSO();
    billboards.init(device.Get(), cmdQueue.queue.Get(), L"resources/icons/light.png");

    // Init shader hot reload
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
    }

    // Spawn animated point light entities
    {
        std::mt19937 lightRng(42u);
        std::uniform_real_distribution<float> hDist(0.0f, 360.0f);
        std::uniform_real_distribution<float> cDist(-1.0f, 1.0f);
        std::uniform_real_distribution<float> freqDist(0.2f, 0.8f);
        std::uniform_real_distribution<float> ampDist(3.0f, 8.0f);
        const float orbitR = 8.0f;
        for (int i = 0; i < PerFrameCB::maxLights; ++i) {
            float angle = (float)i * (6.2831853f / (float)PerFrameCB::maxLights);
            PointLight pl;
            pl.center = { orbitR * std::cos(angle), 3.0f + cDist(lightRng) * 2.0f,
                          orbitR * std::sin(angle) };
            pl.amp = { ampDist(lightRng), ampDist(lightRng) * 0.5f, ampDist(lightRng) };
            pl.freq = { freqDist(lightRng), freqDist(lightRng), freqDist(lightRng) };
            pl.color = hslToLinear(hDist(lightRng), 0.9f, 0.65f);
            scene.ecsWorld.entity().set(pl);
        }
    }

    picker.createResources(device, clientWidth, clientHeight, rootSignature);

    this->contentLoaded = true;
    this->resizeDepthBuffer(this->clientWidth, this->clientHeight);
    bloom.createResources(device.Get(), clientWidth, clientHeight);
    ssao.createResources(
        device.Get(), clientWidth, clientHeight, depthBuffer.Get(), scene.sceneSrvHeap.Get(),
        scene.sceneSrvDescSize, static_cast<INT>(app_slots::srvSlotSsao)
    );
    createNormalPSO();

#ifdef TRACY_ENABLE
    // Only create GPU context if the queue supports timestamp queries.
    // WARP and some adapters fail GetTimestampFrequency, causing Tracy's constructor
    // to return early with null internal pointers — those crash on first viewer connect.
    UINT64 tsFreq = 0;
    if (SUCCEEDED(cmdQueue.queue->GetTimestampFrequency(&tsFreq)) && tsFreq > 0) {
        g_tracyD3d12Ctx = TracyD3D12Context(device.Get(), cmdQueue.queue.Get());
    } else {
        spdlog::warn("Tracy GPU profiling disabled: queue does not support timestamp queries");
    }
#endif

    return true;
}

void Application::createNormalPSO()
{
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = this->rootSignature.Get();
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(g_vertex_shader, sizeof(g_vertex_shader));
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(g_normal_ps, sizeof(g_normal_ps));
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    psoDesc.SampleDesc = { 1, 0 };
    chkDX(this->device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&this->normalPSO)));
}

void Application::onResize(uint32_t width, uint32_t height)
{
    if (this->isResizing || width == 0 || height == 0) {
        return;
    }

    if (this->clientWidth != width || this->clientHeight != height) {
        this->clientWidth = std::max(1u, width);
        this->clientHeight = std::max(1u, height);

        this->cmdQueue.flush();
        // Drop imported resource references from the previous frame before resizing the swap chain.
        this->renderGraph.reset();
        for (int i = 0; i < this->nBuffers; ++i) {
            this->backBuffers[i].Reset();
        }

        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        auto getDescHr = this->swapChain->GetDesc(&swapChainDesc);
        if (FAILED(getDescHr)) {
            throw std::runtime_error("IDXGISwapChain::GetDesc failed during resize");
        }

        auto resizeHr = this->swapChain->ResizeBuffers(
            this->nBuffers, this->clientWidth, this->clientHeight, swapChainDesc.BufferDesc.Format,
            swapChainDesc.Flags
        );
        if (FAILED(resizeHr)) {
            throw std::runtime_error("IDXGISwapChain::ResizeBuffers failed during resize");
        }

        this->curBackBufIdx = this->swapChain->GetCurrentBackBufferIndex();
        this->updateRenderTargetViews(this->rtvHeap);
        this->viewport = CD3DX12_VIEWPORT(
            0.0f, 0.0f, static_cast<float>(this->clientWidth),
            static_cast<float>(this->clientHeight)
        );
        this->scissorRect = CD3DX12_RECT(
            0, 0, static_cast<LONG>(this->clientWidth), static_cast<LONG>(this->clientHeight)
        );
        this->resizeDepthBuffer(this->clientWidth, this->clientHeight);
        bloom.resize(device.Get(), clientWidth, clientHeight);
        picker.resize(device, clientWidth, clientHeight);
        ssao.resize(
            device.Get(), clientWidth, clientHeight, depthBuffer.Get(), scene.sceneSrvHeap.Get(),
            scene.sceneSrvDescSize, static_cast<INT>(Scene::nBuffers + 2)
        );
        this->flush();
    }
}
