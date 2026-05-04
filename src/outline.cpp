module;

#include <d3d12.h>
#include <flecs.h>
#include <wrl.h>
#include <cstdint>
#include <vector>
#include "outline_ps_cso.h"
#include "outline_vs_cso.h"

module outline;

static D3D12_INPUT_ELEMENT_DESC g_inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

OutlineRenderer::~OutlineRenderer()
{
    if (devForDestroy) {
        if (pso.isValid()) {
            devForDestroy->destroy(pso);
        }
        if (vsHandle.isValid()) {
            devForDestroy->destroy(vsHandle);
        }
        if (psHandle.isValid()) {
            devForDestroy->destroy(psHandle);
        }
    }
}

void OutlineRenderer::createResources(
    gfx::IDevice& dev,
    gfx::ShaderBytecode vs,
    gfx::ShaderBytecode ps
)
{
    reloadPSO(dev, vs, ps);
}

void OutlineRenderer::reloadPSO(gfx::IDevice& dev, gfx::ShaderBytecode vs, gfx::ShaderBytecode ps)
{
    devForDestroy = &dev;
    if (pso.isValid()) {
        dev.destroy(pso);
    }
    if (vsHandle.isValid()) {
        dev.destroy(vsHandle);
    }
    if (psHandle.isValid()) {
        dev.destroy(psHandle);
    }

    if (vs.data == nullptr || vs.size == 0) {
        vs = { g_outline_vs, sizeof(g_outline_vs) };
    }
    if (ps.data == nullptr || ps.size == 0) {
        ps = { g_outline_ps, sizeof(g_outline_ps) };
    }

    gfx::ShaderDesc vsDesc{};
    vsDesc.stage = gfx::ShaderStage::Vertex;
    vsDesc.bytecode = vs.data;
    vsDesc.bytecodeSize = vs.size;
    vsDesc.debugName = "outline_vs";
    vsHandle = dev.createShader(vsDesc);

    gfx::ShaderDesc psDesc{};
    psDesc.stage = gfx::ShaderStage::Pixel;
    psDesc.bytecode = ps.data;
    psDesc.bytecodeSize = ps.size;
    psDesc.debugName = "outline_ps";
    psHandle = dev.createShader(psDesc);

    static constexpr gfx::VertexAttribute attrs[] = {
        { "POSITION", 0, gfx::Format::RGB32Float, 0 },
        { "NORMAL", 0, gfx::Format::RGB32Float, 12 },
        { "TEXCOORD", 0, gfx::Format::RG32Float, 24 },
    };

    gfx::GraphicsPipelineDesc gd{};
    gd.vs = vsHandle;
    gd.ps = psHandle;
    gd.vertexAttributes = attrs;
    gd.vertexStride = 32;
    gd.topology = gfx::PrimitiveTopology::TriangleList;
    gd.rasterizer.cull = gfx::CullMode::None;
    gd.numRenderTargets = 1;
    gd.renderTargetFormats[0] = gfx::Format::R11G11B10Float;
    gd.depthStencilFormat = gfx::Format::D32FloatS8X24Uint;
    gd.depthStencil.depthEnable = true;
    gd.depthStencil.depthWrite = false;
    gd.depthStencil.depthCompare = gfx::CompareOp::Less;
    gd.depthStencil.stencilEnable = true;
    gd.depthStencil.stencilCompare = gfx::CompareOp::NotEqual;
    gd.depthStencil.stencilFail = gfx::StencilOp::Keep;
    gd.depthStencil.stencilDepthFail = gfx::StencilOp::Keep;
    gd.depthStencil.stencilPass = gfx::StencilOp::Keep;
    // Outline only reads stencil (NotEqual compare, all KEEP ops) — set
    // stencilWriteMask=0 so D3D12 lets us bind depth_buffer in DEPTH_READ.
    gd.depthStencil.stencilWriteMask = 0;
    gd.nativeRootSignatureOverride = dev.bindlessRootSigNative();
    gd.debugName = "outline_pso";
    pso = dev.createGraphicsPipeline(gd);
}

void OutlineRenderer::render(
    gfx::ICommandList& cmdRef,
    const OutlineRenderContext& ctx,
    const std::vector<DrawCmd>& drawCmds,
    const std::vector<flecs::entity>& drawIndexToEntity,
    flecs::entity hoveredEntity,
    flecs::entity selectedEntity
)
{
    auto* cmdList = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
    // bindPipeline auto-binds the matching root sig (bindless).
    cmdRef.bindPipeline(pso);
    auto* gfxSrvHeap = static_cast<ID3D12DescriptorHeap*>(devForDestroy->srvHeapNative());
    auto* samplerHeap = static_cast<ID3D12DescriptorHeap*>(devForDestroy->samplerHeapNative());
    ID3D12DescriptorHeap* heaps[] = { gfxSrvHeap, samplerHeap };
    cmdList->SetDescriptorHeaps(2, heaps);

    D3D12_GPU_DESCRIPTOR_HANDLE heapStart;
    heapStart.ptr = devForDestroy->srvGpuDescriptorHandle(0);
    cmdList->SetGraphicsRootDescriptorTable(app_slots::bindlessSrvTable, heapStart);
    D3D12_GPU_DESCRIPTOR_HANDLE samplerHeapStart;
    samplerHeapStart.ptr = devForDestroy->samplerGpuDescriptorHandle(0);
    cmdList->SetGraphicsRootDescriptorTable(app_slots::bindlessSamplerTable, samplerHeapStart);

    cmdList->SetGraphicsRootConstantBufferView(app_slots::bindlessPerFrameCB, ctx.perFrameAddr);
    cmdList->SetGraphicsRootConstantBufferView(app_slots::bindlessPerPassCB, ctx.perPassAddr);

    auto drawOutline = [&](flecs::entity e, float width, float r, float g, float b) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(drawIndexToEntity.size()); ++i) {
            if (drawIndexToEntity[i] == e) {
                struct OutlinePayload
                {
                    uint32_t drawDataIdx;
                    uint32_t drawIndex;
                    float outlineWidth;
                    float outlineR, outlineG, outlineB;
                };
                OutlinePayload pl;
                pl.drawDataIdx = devForDestroy->bindlessSrvIndex(ctx.perObjectBuffer);
                pl.drawIndex = i;
                pl.outlineWidth = width;
                pl.outlineR = r;
                pl.outlineG = g;
                pl.outlineB = b;
                cmdList->SetGraphicsRoot32BitConstants(
                    app_slots::bindlessIndices, sizeof(pl) / 4, &pl, 0
                );
                cmdList->DrawIndexedInstanced(
                    drawCmds[i].indexCount, 1, drawCmds[i].indexOffset,
                    static_cast<INT>(drawCmds[i].vertexOffset), 0
                );
                break;
            }
        }
    };

    if (hoveredEntity) {
        drawOutline(hoveredEntity, 0.03f, 0.3f, 0.8f, 1.0f);
    }
    if (selectedEntity) {
        drawOutline(selectedEntity, 0.06f, 1.0f, 0.75f, 0.1f);
    }
}
