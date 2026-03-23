module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch"
#endif
#include "d3dx12.h"
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
#include <algorithm>
#include <array>
#include <cstring>
#include <directxtk12/ResourceUploadBatch.h>
#include <directxtk12/WICTextureLoader.h>

#include "billboard_vs_cso.h"
#include "billboard_ps_cso.h"

module billboard;

using Microsoft::WRL::ComPtr;

namespace
{
    struct QuadVertex
    {
        vec2 corner;
        vec2 uv;
    };
}  // namespace

void BillboardRenderer::init(
    ID3D12Device2* device,
    ID3D12CommandQueue* queue,
    const wchar_t* texturePath
)
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(device->CheckFeatureSupport(
            D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)
        ))) {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    CD3DX12_DESCRIPTOR_RANGE1 spriteSrvRange;
    spriteSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER1 rootParams[2];
    rootParams[0].InitAsConstants(24, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    rootParams[1].InitAsDescriptorTable(1, &spriteSrvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    const D3D12_ROOT_SIGNATURE_FLAGS rootSigFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 1, &sampler, rootSigFlags);

    ComPtr<ID3DBlob> rootSigBlob;
    ComPtr<ID3DBlob> errorBlob;
    chkDX(D3DX12SerializeVersionedRootSignature(
        &rootSigDesc, featureData.HighestVersion, &rootSigBlob, &errorBlob
    ));
    chkDX(device->CreateRootSignature(
        0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature)
    ));

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
          0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
          0 },
        { "INSTANCE_POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0,
          D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INSTANCE_COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 12,
          D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INSTANCE_SIZE", 0, DXGI_FORMAT_R32_FLOAT, 1, 28,
          D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(g_billboard_vs, sizeof(g_billboard_vs));
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(g_billboard_ps, sizeof(g_billboard_ps));
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R11G11B10_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc = { 1, 0 };

    chkDX(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    chkDX(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap)));

    DirectX::ResourceUploadBatch upload(device);
    upload.Begin(D3D12_COMMAND_LIST_TYPE_DIRECT);
    chkDX(
        DirectX::CreateWICTextureFromFileEx(
            device, upload, texturePath, 0, D3D12_RESOURCE_FLAG_NONE,
            DirectX::WIC_LOADER_FORCE_RGBA32 | DirectX::WIC_LOADER_FORCE_SRGB, &spriteTexture
        )
    );
    auto uploadFuture = upload.End(queue);
    uploadFuture.wait();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = spriteTexture->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = static_cast<UINT>(spriteTexture->GetDesc().MipLevels);
    srvDesc.Texture2D.MostDetailedMip = 0;
    device->CreateShaderResourceView(
        spriteTexture.Get(), &srvDesc, srvHeap->GetCPUDescriptorHandleForHeapStart()
    );

    const std::array<QuadVertex, 6> quadVerts = {
        QuadVertex{ { -1.0f, -1.0f }, { 0.0f, 1.0f } },
        QuadVertex{ { -1.0f, 1.0f }, { 0.0f, 0.0f } },
        QuadVertex{ { 1.0f, 1.0f }, { 1.0f, 0.0f } },
        QuadVertex{ { -1.0f, -1.0f }, { 0.0f, 1.0f } },
        QuadVertex{ { 1.0f, 1.0f }, { 1.0f, 0.0f } },
        QuadVertex{ { 1.0f, -1.0f }, { 1.0f, 1.0f } },
    };

    const CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    const CD3DX12_RESOURCE_DESC quadDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(quadVerts));
    chkDX(device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &quadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&quadVertexBuffer)
    ));

    void* quadMapped = nullptr;
    chkDX(quadVertexBuffer->Map(0, nullptr, &quadMapped));
    std::memcpy(quadMapped, quadVerts.data(), sizeof(quadVerts));
    quadVertexBuffer->Unmap(0, nullptr);

    quadVBV.BufferLocation = quadVertexBuffer->GetGPUVirtualAddress();
    quadVBV.SizeInBytes = sizeof(quadVerts);
    quadVBV.StrideInBytes = sizeof(QuadVertex);

    const CD3DX12_RESOURCE_DESC instanceDesc =
        CD3DX12_RESOURCE_DESC::Buffer(sizeof(BillboardInstance) * maxInstances);
    chkDX(device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &instanceDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&instanceBuffer)
    ));

    void* instanceMapped = nullptr;
    chkDX(instanceBuffer->Map(0, nullptr, &instanceMapped));
    mappedInstances = static_cast<BillboardInstance*>(instanceMapped);
    std::memset(mappedInstances, 0, sizeof(BillboardInstance) * maxInstances);

    instanceVBV.BufferLocation = instanceBuffer->GetGPUVirtualAddress();
    instanceVBV.SizeInBytes = sizeof(BillboardInstance) * maxInstances;
    instanceVBV.StrideInBytes = sizeof(BillboardInstance);
}

void BillboardRenderer::updateInstances(
    const vec4* lightPos,
    const vec4* lightColor,
    uint32_t count
)
{
    instanceCount = std::min(count, maxInstances);
    for (uint32_t i = 0; i < instanceCount; ++i) {
        mappedInstances[i].position = { lightPos[i].x, lightPos[i].y, lightPos[i].z };
        mappedInstances[i].color = { lightColor[i].x, lightColor[i].y, lightColor[i].z, 1.0f };
        mappedInstances[i].size = spriteSize;
    }
}

void BillboardRenderer::render(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    const mat4& viewProj,
    const vec3& cameraPos
)
{
    if (instanceCount == 0) {
        return;
    }

    vec3 forward = normalize(-cameraPos);
    vec3 worldUp{ 0.0f, 1.0f, 0.0f };
    if (std::abs(dot(forward, worldUp)) > 0.99f) {
        worldUp = { 0.0f, 0.0f, 1.0f };
    }
    vec3 right = normalize(cross(worldUp, forward));
    vec3 up = normalize(cross(forward, right));

    float constants[24] = {
        viewProj._11, viewProj._12, viewProj._13, viewProj._14, viewProj._21, viewProj._22,
        viewProj._23, viewProj._24, viewProj._31, viewProj._32, viewProj._33, viewProj._34,
        viewProj._41, viewProj._42, viewProj._43, viewProj._44, right.x,      right.y,
        right.z,      0.0f,         up.x,         up.y,         up.z,         0.0f,
    };

    cmdList->SetPipelineState(pipelineState.Get());
    cmdList->SetGraphicsRootSignature(rootSignature.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW views[] = { quadVBV, instanceVBV };
    cmdList->IASetVertexBuffers(0, 2, views);

    ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRoot32BitConstants(0, 24, constants, 0);
    cmdList->SetGraphicsRootDescriptorTable(1, srvHeap->GetGPUDescriptorHandleForHeapStart());

    cmdList->DrawInstanced(6, instanceCount, 0, 0);
}
