module;

#include <d3d12.h>
#include <flecs.h>
#include <wrl.h>
#include <cstdint>
#include <vector>
#include "outline_ps_cso.h"
#include "outline_vs_cso.h"

module outline;

using Microsoft::WRL::ComPtr;

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
    ID3D12RootSignature* rootSig,
    gfx::ShaderBytecode vs,
    gfx::ShaderBytecode ps
)
{
    reloadPSO(dev, rootSig, vs, ps);
}

void OutlineRenderer::reloadPSO(
    gfx::IDevice& dev,
    ID3D12RootSignature* rootSig,
    gfx::ShaderBytecode vs,
    gfx::ShaderBytecode ps
)
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
    gd.nativeRootSignatureOverride = rootSig;
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
    cmdRef.bindPipeline(pso);
    cmdList->SetGraphicsRootSignature(ctx.rootSig);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdRef.setViewport(*ctx.viewport);
    cmdRef.setScissor(*ctx.scissorRect);
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv{ ctx.hdrRtv };
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{ ctx.dsv };
    cmdList->OMSetRenderTargets(1, &hdrRtv, true, &dsv);
    cmdList->OMSetStencilRef(1);

    D3D12_VERTEX_BUFFER_VIEW d3dVbv{ ctx.vbv.gpuAddress, ctx.vbv.sizeInBytes,
                                     ctx.vbv.strideInBytes };
    D3D12_INDEX_BUFFER_VIEW d3dIbv{ ctx.ibv.gpuAddress, ctx.ibv.sizeInBytes, DXGI_FORMAT_R32_UINT };
    cmdList->IASetVertexBuffers(0, 1, &d3dVbv);
    cmdList->IASetIndexBuffer(&d3dIbv);

    auto* gfxSrvHeap = static_cast<ID3D12DescriptorHeap*>(devForDestroy->srvHeapNative());
    ID3D12DescriptorHeap* heaps[] = { gfxSrvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetGraphicsRootConstantBufferView(0, ctx.perFrameAddr);
    cmdList->SetGraphicsRootConstantBufferView(1, ctx.perPassAddr);

    D3D12_GPU_DESCRIPTOR_HANDLE perObjH{ ctx.perObjHandle };
    cmdList->SetGraphicsRootDescriptorTable(4, perObjH);

    auto drawOutline = [&](flecs::entity e, float width, float r, float g, float b) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(drawIndexToEntity.size()); ++i) {
            if (drawIndexToEntity[i] == e) {
                float params[4] = { width, r, g, b };
                cmdList->SetGraphicsRoot32BitConstants(3, 4, params, 0);
                cmdList->SetGraphicsRoot32BitConstant(2, i, 0);
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
