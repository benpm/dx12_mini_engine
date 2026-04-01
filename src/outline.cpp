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

void OutlineRenderer::createResources(
    ID3D12Device2* device,
    ID3D12RootSignature* rootSig,
    D3D12_SHADER_BYTECODE vs,
    D3D12_SHADER_BYTECODE ps
)
{
    reloadPSO(device, rootSig, vs, ps);
}

void OutlineRenderer::reloadPSO(
    ID3D12Device2* device,
    ID3D12RootSignature* rootSig,
    D3D12_SHADER_BYTECODE vs,
    D3D12_SHADER_BYTECODE ps
)
{
    if (vs.pShaderBytecode == nullptr || vs.BytecodeLength == 0) {
        vs = CD3DX12_SHADER_BYTECODE(g_outline_vs, sizeof(g_outline_vs));
    }
    if (ps.pShaderBytecode == nullptr || ps.BytecodeLength == 0) {
        ps = CD3DX12_SHADER_BYTECODE(g_outline_ps, sizeof(g_outline_ps));
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSig;
    psoDesc.InputLayout = { g_inputLayout, _countof(g_inputLayout) };
    psoDesc.VS = vs;
    psoDesc.PS = ps;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    D3D12_DEPTH_STENCIL_DESC dsDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    dsDesc.StencilEnable = TRUE;
    dsDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_NOT_EQUAL;
    dsDesc.BackFace = dsDesc.FrontFace;
    psoDesc.DepthStencilState = dsDesc;

    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R11G11B10_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    psoDesc.SampleDesc = { 1, 0 };

    chkDX(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));
}

void OutlineRenderer::render(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    ID3D12RootSignature* rootSig,
    const D3D12_VERTEX_BUFFER_VIEW& vbv,
    const D3D12_INDEX_BUFFER_VIEW& ibv,
    ID3D12DescriptorHeap* sceneSrvHeap,
    UINT srvDescSize,
    uint32_t curBackBufIdx,
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv,
    const D3D12_VIEWPORT& viewport,
    const D3D12_RECT& scissorRect,
    const std::vector<DrawCmd>& drawCmds,
    const std::vector<flecs::entity>& drawIndexToEntity,
    flecs::entity hoveredEntity,
    flecs::entity selectedEntity
)
{
    cmdList->SetPipelineState(pso.Get());
    cmdList->SetGraphicsRootSignature(rootSig);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissorRect);
    cmdList->OMSetRenderTargets(1, &hdrRtv, true, &dsv);
    cmdList->OMSetStencilRef(1);

    cmdList->IASetVertexBuffers(0, 1, &vbv);
    cmdList->IASetIndexBuffer(&ibv);

    ID3D12DescriptorHeap* heaps[] = { sceneSrvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(
        sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(), static_cast<INT>(curBackBufIdx),
        srvDescSize
    );
    cmdList->SetGraphicsRootDescriptorTable(0, srvHandle);

    auto drawOutline = [&](flecs::entity e, float width, float r, float g, float b) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(drawIndexToEntity.size()); ++i) {
            if (drawIndexToEntity[i] == e) {
                float params[4] = { width, r, g, b };
                cmdList->SetGraphicsRoot32BitConstants(4, 4, params, 0);
                cmdList->SetGraphicsRoot32BitConstant(1, i, 0);
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
