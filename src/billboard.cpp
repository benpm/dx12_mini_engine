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
        if (spriteTexture.isValid()) {
            devForDestroy->destroy(spriteTexture);
        }
        if (pipelineState.isValid()) {
            devForDestroy->destroy(pipelineState);
        }
        if (vsHandle.isValid()) {
            devForDestroy->destroy(vsHandle);
        }
        if (psHandle.isValid()) {
            devForDestroy->destroy(psHandle);
        }
    }
}

void BillboardRenderer::init(gfx::IDevice& dev, const wchar_t* texturePath)
{
    devForDestroy = &dev;
    auto* device = static_cast<ID3D12Device2*>(dev.nativeHandle());

    // Two input streams: slot 0 = per-vertex quad (pos + uv), slot 1 = per-instance
    // (world pos, color, size). gfx::VertexAttribute now supports inputSlot and
    // the per-stream classification is read from GraphicsPipelineDesc::vertexStreams.
    const gfx::VertexAttribute billboardAttrs[] = {
        { "POSITION", 0, gfx::Format::RG32Float, 0, 0 },
        { "TEXCOORD", 0, gfx::Format::RG32Float, 8, 0 },
        { "INSTANCE_POS", 0, gfx::Format::RGB32Float, 0, 1 },
        { "INSTANCE_COLOR", 0, gfx::Format::RGBA32Float, 12, 1 },
        { "INSTANCE_SIZE", 0, gfx::Format::R32Float, 28, 1 },
    };
    const gfx::VertexStream billboardStreams[] = {
        { 16, false },                            // slot 0: quad vertex (vec2 pos + vec2 uv)
        { sizeof(BillboardInstance), true },      // slot 1: per-instance
    };

    {
        gfx::ShaderDesc sd{};
        sd.bytecode = g_billboard_vs;
        sd.bytecodeSize = sizeof(g_billboard_vs);
        sd.stage = gfx::ShaderStage::Vertex;
        sd.debugName = "billboard_vs";
        vsHandle = dev.createShader(sd);
    }
    {
        gfx::ShaderDesc sd{};
        sd.bytecode = g_billboard_ps;
        sd.bytecodeSize = sizeof(g_billboard_ps);
        sd.stage = gfx::ShaderStage::Pixel;
        sd.debugName = "billboard_ps";
        psHandle = dev.createShader(sd);
    }

    gfx::GraphicsPipelineDesc pd{};
    pd.vs = vsHandle;
    pd.ps = psHandle;
    pd.vertexAttributes = billboardAttrs;
    pd.vertexStreams = billboardStreams;
    pd.rasterizer.cull = gfx::CullMode::None;
    pd.depthStencil.depthEnable = true;
    pd.depthStencil.depthWrite = false;
    pd.depthStencil.depthCompare = gfx::CompareOp::Less;
    pd.depthStencil.stencilEnable = false;
    // Additive-with-alpha blend (matches the existing premultiplied-alpha look).
    pd.blend[0].blendEnable = true;
    pd.blend[0].srcColor = gfx::BlendFactor::SrcAlpha;
    pd.blend[0].dstColor = gfx::BlendFactor::One;
    pd.blend[0].colorOp = gfx::BlendOp::Add;
    pd.blend[0].srcAlpha = gfx::BlendFactor::One;
    pd.blend[0].dstAlpha = gfx::BlendFactor::One;
    pd.blend[0].alphaOp = gfx::BlendOp::Add;
    pd.numRenderTargets = 1;
    pd.renderTargetFormats[0] = gfx::Format::R11G11B10Float;
    pd.depthStencilFormat = gfx::Format::D32FloatS8X24Uint;
    pd.debugName = "billboard_pso";
    pipelineState = dev.createGraphicsPipeline(pd);

    auto* queue = static_cast<ID3D12CommandQueue*>(dev.graphicsQueue()->nativeHandle());
    DirectX::ResourceUploadBatch upload(device);
    upload.Begin(D3D12_COMMAND_LIST_TYPE_DIRECT);
    ComPtr<ID3D12Resource> wicResource;
    chkDX(
        DirectX::CreateWICTextureFromFileEx(
            device, upload, texturePath, 0, D3D12_RESOURCE_FLAG_NONE,
            DirectX::WIC_LOADER_FORCE_RGBA32 | DirectX::WIC_LOADER_FORCE_SRGB, &wicResource
        )
    );
    auto uploadFuture = upload.End(queue);
    uploadFuture.wait();

    // Hand the WIC-allocated resource over to gfx so its lifetime is managed
    // by the same path as engine-created textures (pendingDestroys, fence-tracked).
    spriteTexture = dev.adoptTexture(
        wicResource.Get(), gfx::Format::RGBA8UnormSrgb,
        static_cast<uint32_t>(wicResource->GetDesc().MipLevels)
    );
    spriteSrvIdx = dev.bindlessSrvIndex(spriteTexture);

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

uint32_t BillboardRenderer::appendParticles(
    const vec3* positions, const vec4* colors, const float* sizes, uint32_t count
)
{
    uint32_t room = maxInstances > instanceCount ? maxInstances - instanceCount : 0;
    uint32_t n = std::min(count, room);
    for (uint32_t i = 0; i < n; ++i) {
        auto& slot = mappedInstances[instanceCount + i];
        slot.position = positions[i];
        slot.color = colors[i];
        slot.size = sizes[i];
    }
    instanceCount += n;
    return n;
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

    // bindPipeline also binds the matching root sig (gfx bindless).
    cmdRef.bindPipeline(pipelineState);
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
