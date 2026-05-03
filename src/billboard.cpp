module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <d3d12.h>
#include <directxtk12/ResourceUploadBatch.h>
#include <directxtk12/WICTextureLoader.h>
#include <Windows.h>
#include <wrl.h>
#include <algorithm>
#include <array>
#include <cstring>
#include "d3dx12_clean.h"

#include "billboard_ps_cso.h"
#include "billboard_vs_cso.h"

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

BillboardRenderer::~BillboardRenderer()
{
    if (devForDestroy) {
        if (quadVertexBuffer.isValid()) {
            devForDestroy->destroy(quadVertexBuffer);
        }
        if (instanceBuffer.isValid()) {
            devForDestroy->destroy(instanceBuffer);
        }
    }
}

void BillboardRenderer::init(gfx::IDevice& dev, const wchar_t* texturePath)
{
    devForDestroy = &dev;
    auto* device = static_cast<ID3D12Device2*>(dev.nativeHandle());

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
    psoDesc.pRootSignature = static_cast<ID3D12RootSignature*>(dev.bindlessRootSigNative());
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
    // Match the application's main depth buffer (D32_FLOAT_S8X24_UINT) — the
    // billboard pass binds depth_buffer's DSV for depth-test (no depth write).
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    psoDesc.SampleDesc = { 1, 0 };

    chkDX(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));

    auto* queue = static_cast<ID3D12CommandQueue*>(dev.graphicsQueue()->nativeHandle());
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

    spriteSrvIdx = dev.createExternalSrv(
        spriteTexture.Get(), gfx::Format::RGBA8UnormSrgb,
        static_cast<uint32_t>(spriteTexture->GetDesc().MipLevels)
    );

    const std::array<QuadVertex, 6> quadVerts = {
        QuadVertex{ { -1.0f, -1.0f }, { 0.0f, 1.0f } },
        QuadVertex{ { -1.0f, 1.0f }, { 0.0f, 0.0f } },
        QuadVertex{ { 1.0f, 1.0f }, { 1.0f, 0.0f } },
        QuadVertex{ { -1.0f, -1.0f }, { 0.0f, 1.0f } },
        QuadVertex{ { 1.0f, 1.0f }, { 1.0f, 0.0f } },
        QuadVertex{ { 1.0f, -1.0f }, { 1.0f, 1.0f } },
    };

    {
        gfx::BufferDesc bd{};
        bd.size = sizeof(quadVerts);
        bd.usage = gfx::BufferUsage::Upload;
        bd.debugName = "billboard_quad_vb";
        quadVertexBuffer = dev.createBuffer(bd);
        void* mapped = dev.map(quadVertexBuffer);
        std::memcpy(mapped, quadVerts.data(), sizeof(quadVerts));
        dev.unmap(quadVertexBuffer);
        auto* res = static_cast<ID3D12Resource*>(dev.nativeResource(quadVertexBuffer));
        quadVBV.gpuAddress = res->GetGPUVirtualAddress();
        quadVBV.sizeInBytes = sizeof(quadVerts);
        quadVBV.strideInBytes = sizeof(QuadVertex);
    }

    {
        gfx::BufferDesc bd{};
        bd.size = sizeof(BillboardInstance) * maxInstances;
        bd.usage = gfx::BufferUsage::Upload;
        bd.debugName = "billboard_instances";
        instanceBuffer = dev.createBuffer(bd);
        mappedInstances = static_cast<BillboardInstance*>(dev.map(instanceBuffer));
        std::memset(mappedInstances, 0, sizeof(BillboardInstance) * maxInstances);
        auto* res = static_cast<ID3D12Resource*>(dev.nativeResource(instanceBuffer));
        instanceVBV.gpuAddress = res->GetGPUVirtualAddress();
        instanceVBV.sizeInBytes = sizeof(BillboardInstance) * maxInstances;
        instanceVBV.strideInBytes = sizeof(BillboardInstance);
    }
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
    gfx::ICommandList& cmdRef,
    const mat4& viewProj,
    const vec3& cameraPos
)
{
    auto* cmdList = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
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

    cmdList->SetPipelineState(pipelineState.Get());
    cmdList->SetGraphicsRootSignature(
        static_cast<ID3D12RootSignature*>(devForDestroy->bindlessRootSigNative())
    );
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW views[] = {
        { quadVBV.gpuAddress, quadVBV.sizeInBytes, quadVBV.strideInBytes },
        { instanceVBV.gpuAddress, instanceVBV.sizeInBytes, instanceVBV.strideInBytes },
    };
    cmdList->IASetVertexBuffers(0, 2, views);

    auto* gfxHeap = static_cast<ID3D12DescriptorHeap*>(devForDestroy->srvHeapNative());
    auto* samplerHeap = static_cast<ID3D12DescriptorHeap*>(devForDestroy->samplerHeapNative());
    ID3D12DescriptorHeap* heaps[] = { gfxHeap, samplerHeap };
    cmdList->SetDescriptorHeaps(2, heaps);

    D3D12_GPU_DESCRIPTOR_HANDLE heapStart;
    heapStart.ptr = devForDestroy->srvGpuDescriptorHandle(0);
    cmdList->SetGraphicsRootDescriptorTable(3, heapStart);  // Slot 3: SRV table
    D3D12_GPU_DESCRIPTOR_HANDLE samplerHeapStart;
    samplerHeapStart.ptr = devForDestroy->samplerGpuDescriptorHandle(0);
    cmdList->SetGraphicsRootDescriptorTable(4, samplerHeapStart);  // Slot 4: sampler table

    struct BindlessBillboardPayload
    {
        uint32_t spriteIdx;
        uint32_t samplerIdx;
        uint32_t _pad[2];
        float viewProj[16];
        float camRight[4];
        float camUp[4];
    };
    BindlessBillboardPayload pl;
    pl.spriteIdx = spriteSrvIdx;
    pl.samplerIdx = 0;
    std::memcpy(pl.viewProj, &viewProj, 64);
    std::memcpy(pl.camRight, &right, 12);
    pl.camRight[3] = 0;
    std::memcpy(pl.camUp, &up, 12);
    pl.camUp[3] = 0;
    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(pl) / 4, &pl, 0);

    cmdList->DrawInstanced(6, instanceCount, 0, 0);
}
