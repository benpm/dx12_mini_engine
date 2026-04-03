module;

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "d3dx12_clean.h"
#include "vertex_shader_cso.h"

module shadow;

using Microsoft::WRL::ComPtr;
using namespace DirectX;

static D3D12_INPUT_ELEMENT_DESC g_inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

static void transitionRes(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after
)
{
    auto b = CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
    cmdList->ResourceBarrier(1, &b);
}

void ShadowRenderer::createResources(
    ID3D12Device2* device,
    ID3D12RootSignature* rootSig,
    D3D12_SHADER_BYTECODE vs,
    ID3D12DescriptorHeap* sceneSrvHeap,
    UINT srvDescSize,
    INT shadowSrvSlot
)
{
    // Shadow depth texture (R32_TYPELESS / D32_FLOAT)
    {
        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = DXGI_FORMAT_D32_FLOAT;
        clearVal.DepthStencil = { 1.0f, 0 };
        const CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_TYPELESS, mapSize, mapSize, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
        );
        chkDX(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearVal,
            IID_PPV_ARGS(&shadowMap)
        ));
    }

    // DSV heap + view
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
        dsvDesc.NumDescriptors = 1;
        dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        chkDX(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap)));

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvViewDesc = {};
        dsvViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(
            shadowMap.Get(), &dsvViewDesc, dsvHeap->GetCPUDescriptorHandleForHeapStart()
        );
    }

    // SRV in sceneSrvHeap at the given slot
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
            sceneSrvHeap->GetCPUDescriptorHandleForHeapStart(), shadowSrvSlot, srvDescSize
        );
        device->CreateShaderResourceView(shadowMap.Get(), &srvDesc, srvHandle);
    }

    reloadPSO(device, rootSig, vs);
}

void ShadowRenderer::reloadPSO(
    ID3D12Device2* device,
    ID3D12RootSignature* rootSig,
    D3D12_SHADER_BYTECODE vs
)
{
    // Fall back to embedded CSO if no hot-reload data
    if (vs.pShaderBytecode == nullptr || vs.BytecodeLength == 0) {
        vs = CD3DX12_SHADER_BYTECODE(g_vertex_shader, sizeof(g_vertex_shader));
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSig;
    psoDesc.InputLayout = { g_inputLayout, _countof(g_inputLayout) };
    psoDesc.VS = vs;
    psoDesc.PS = {};  // depth-only
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
    psoDesc.RasterizerState.DepthBias = rasterDepthBias;
    psoDesc.RasterizerState.SlopeScaledDepthBias = rasterSlopeBias;
    psoDesc.RasterizerState.DepthBiasClamp = rasterBiasClamp;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 0;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc = { 1, 0 };

    chkDX(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));
}

mat4 ShadowRenderer::computeLightViewProj(vec3 dirLightDir) const
{
    // dirLightDir is the direction FROM the light; negate to get toward-light
    XMVECTOR fromLight =
        XMVector3Normalize(XMVectorSet(dirLightDir.x, dirLightDir.y, dirLightDir.z, 0.0f));
    XMVECTOR toLight = XMVectorNegate(fromLight);

    XMVECTOR lightP = XMVectorScale(toLight, lightDistance);
    XMVECTOR target = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    float dotUp = fabsf(XMVectorGetByIndex(
        XMVector3Dot(XMVector3Normalize(XMVectorSubtract(target, lightP)), up), 0
    ));
    if (dotUp > 0.99f) {
        up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }

    XMMATRIX lightView = XMMatrixLookAtLH(lightP, target, up);
    XMMATRIX lightProj = XMMatrixOrthographicLH(
        orthoSize, orthoSize, std::max(0.001f, nearPlane), std::max(nearPlane + 0.001f, farPlane)
    );
    XMMATRIX lvp = XMMatrixMultiply(lightView, lightProj);

    mat4 result;
    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&result), lvp);
    return result;
}

void ShadowRenderer::render(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    const D3D12_VERTEX_BUFFER_VIEW& vbv,
    const D3D12_INDEX_BUFFER_VIEW& ibv,
    ID3D12DescriptorHeap* sceneSrvHeap,
    UINT srvDescSize,
    uint32_t curBackBufIdx,
    const std::vector<DrawCmd>& drawCmds,
    uint32_t totalSlots
)
{
    auto shadowDsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    cmdList->ClearDepthStencilView(shadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    cmdList->SetPipelineState(pso.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VIEWPORT shadowVP = { 0.0f, 0.0f, (float)mapSize, (float)mapSize, 0.0f, 1.0f };
    D3D12_RECT shadowScissor = { 0, 0, (LONG)mapSize, (LONG)mapSize };
    cmdList->RSSetViewports(1, &shadowVP);
    cmdList->RSSetScissorRects(1, &shadowScissor);
    cmdList->OMSetRenderTargets(0, nullptr, false, &shadowDsv);

    cmdList->IASetVertexBuffers(0, 1, &vbv);
    cmdList->IASetIndexBuffer(&ibv);

    ID3D12DescriptorHeap* heaps[] = { sceneSrvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(
        sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(), static_cast<INT>(curBackBufIdx),
        srvDescSize
    );
    cmdList->SetGraphicsRootDescriptorTable(0, srvGpuHandle);

    for (uint32_t i = 0; i < static_cast<uint32_t>(drawCmds.size()); ++i) {
        cmdList->SetGraphicsRoot32BitConstant(1, totalSlots + drawCmds[i].baseDrawIndex, 0);
        cmdList->DrawIndexedInstanced(
            drawCmds[i].indexCount, drawCmds[i].instanceCount, drawCmds[i].indexOffset,
            static_cast<INT>(drawCmds[i].vertexOffset), 0
        );
    }
}
