module;

#include <d3d12.h>
#include <flecs.h>
#include <wrl.h>
#include <cstdint>
#include <vector>
#include "d3dx12_clean.h"
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
    D3D12_SHADER_BYTECODE vs,
    D3D12_SHADER_BYTECODE ps
)
{
    reloadPSO(dev, rootSig, vs, ps);
}

void OutlineRenderer::reloadPSO(
    gfx::IDevice& dev,
    ID3D12RootSignature* rootSig,
    D3D12_SHADER_BYTECODE vs,
    D3D12_SHADER_BYTECODE ps
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

    if (vs.pShaderBytecode == nullptr || vs.BytecodeLength == 0) {
        vs = CD3DX12_SHADER_BYTECODE(g_outline_vs, sizeof(g_outline_vs));
    }
    if (ps.pShaderBytecode == nullptr || ps.BytecodeLength == 0) {
        ps = CD3DX12_SHADER_BYTECODE(g_outline_ps, sizeof(g_outline_ps));
    }

    gfx::ShaderDesc vsDesc{};
    vsDesc.stage = gfx::ShaderStage::Vertex;
    vsDesc.bytecode = vs.pShaderBytecode;
    vsDesc.bytecodeSize = vs.BytecodeLength;
    vsDesc.debugName = "outline_vs";
    vsHandle = dev.createShader(vsDesc);

    gfx::ShaderDesc psDesc{};
    psDesc.stage = gfx::ShaderStage::Pixel;
    psDesc.bytecode = ps.pShaderBytecode;
    psDesc.bytecodeSize = ps.BytecodeLength;
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
    cmdList->RSSetViewports(1, ctx.viewport);
    cmdList->RSSetScissorRects(1, ctx.scissorRect);
    cmdList->OMSetRenderTargets(1, &ctx.hdrRtv, true, &ctx.dsv);
    cmdList->OMSetStencilRef(1);

    cmdList->IASetVertexBuffers(0, 1, ctx.vbv);
    cmdList->IASetIndexBuffer(ctx.ibv);

    ID3D12DescriptorHeap* heaps[] = { ctx.srvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetGraphicsRootConstantBufferView(0, ctx.perFrameAddr);
    cmdList->SetGraphicsRootConstantBufferView(1, ctx.perPassAddr);

    cmdList->SetGraphicsRootDescriptorTable(4, ctx.perObjHandle);

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
