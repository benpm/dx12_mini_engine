module;

#include <d3d12.h>
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "vertex_shader_cso.h"

module shadow;

using namespace DirectX;

static D3D12_INPUT_ELEMENT_DESC g_inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

ShadowRenderer::~ShadowRenderer()
{
    if (devForDestroy) {
        if (shadowMap.isValid()) {
            devForDestroy->destroy(shadowMap);
        }
        if (pso.isValid()) {
            devForDestroy->destroy(pso);
        }
        if (vsHandle.isValid()) {
            devForDestroy->destroy(vsHandle);
        }
    }
}

void ShadowRenderer::createResources(gfx::IDevice& dev, gfx::ShaderBytecode vs)
{
    devForDestroy = &dev;

    {
        gfx::TextureDesc td{};
        td.width = mapSize;
        td.height = mapSize;
        td.format = gfx::Format::R32Typeless;
        td.viewFormat = gfx::Format::D32Float;
        td.usage = gfx::TextureUsage::DepthStencil;
        td.initialState = gfx::ResourceState::PixelShaderResource;
        td.useClearValue = true;
        td.clearDepth = 1.0f;
        td.clearStencil = 0;
        td.debugName = "shadow_map";
        shadowMap = dev.createTexture(td);
    }

    reloadPSO(dev, vs);
}

void ShadowRenderer::reloadPSO(gfx::IDevice& dev, gfx::ShaderBytecode vs)
{
    devForDestroy = &dev;
    if (pso.isValid()) {
        dev.destroy(pso);
    }
    if (vsHandle.isValid()) {
        dev.destroy(vsHandle);
    }
    // Fall back to embedded CSO if no hot-reload data
    if (vs.data == nullptr || vs.size == 0) {
        vs = { g_vertex_shader, sizeof(g_vertex_shader) };
    }

    gfx::ShaderDesc vsDesc{};
    vsDesc.stage = gfx::ShaderStage::Vertex;
    vsDesc.bytecode = vs.data;
    vsDesc.bytecodeSize = vs.size;
    vsDesc.debugName = "shadow_vs";
    vsHandle = dev.createShader(vsDesc);

    static constexpr gfx::VertexAttribute attrs[] = {
        { "POSITION", 0, gfx::Format::RGB32Float, 0 },
        { "NORMAL", 0, gfx::Format::RGB32Float, 12 },
        { "TEXCOORD", 0, gfx::Format::RG32Float, 24 },
    };

    gfx::GraphicsPipelineDesc gd{};
    gd.vs = vsHandle;
    // No PS — depth-only pass.
    gd.vertexAttributes = attrs;
    gd.vertexStride = 32;
    gd.topology = gfx::PrimitiveTopology::TriangleList;
    gd.rasterizer.cull = gfx::CullMode::Front;
    gd.rasterizer.depthBias = rasterDepthBias;
    gd.rasterizer.slopeScaledDepthBias = rasterSlopeBias;
    gd.rasterizer.depthBiasClamp = rasterBiasClamp;
    gd.numRenderTargets = 0;
    gd.depthStencilFormat = gfx::Format::D32Float;
    gd.depthStencil.depthEnable = true;
    gd.depthStencil.depthWrite = true;
    gd.depthStencil.depthCompare = gfx::CompareOp::Less;
    gd.nativeRootSignatureOverride = dev.bindlessRootSigNative();
    gd.debugName = "shadow_pso";
    pso = dev.createGraphicsPipeline(gd);
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
    gfx::ICommandList& cmdRef,
    const gfx::VertexBufferView& vbv,
    const gfx::IndexBufferView& ibv,
    gfx::BufferHandle perObjectBuffer,
    const std::vector<DrawCmd>& drawCmds,
    uint32_t totalSlots
)
{
    auto* cmdList = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
    D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv;
    shadowDsv.ptr = static_cast<SIZE_T>(devForDestroy->dsvHandle(shadowMap));
    cmdList->ClearDepthStencilView(shadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    cmdRef.bindPipeline(pso);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VIEWPORT shadowVP = { 0.0f, 0.0f, (float)mapSize, (float)mapSize, 0.0f, 1.0f };
    D3D12_RECT shadowScissor = { 0, 0, (LONG)mapSize, (LONG)mapSize };
    cmdList->RSSetViewports(1, &shadowVP);
    cmdList->RSSetScissorRects(1, &shadowScissor);
    cmdList->OMSetRenderTargets(0, nullptr, false, &shadowDsv);

    D3D12_VERTEX_BUFFER_VIEW d3dVbv{ vbv.gpuAddress, vbv.sizeInBytes, vbv.strideInBytes };
    D3D12_INDEX_BUFFER_VIEW d3dIbv{ ibv.gpuAddress, ibv.sizeInBytes, DXGI_FORMAT_R32_UINT };
    cmdList->IASetVertexBuffers(0, 1, &d3dVbv);
    cmdList->IASetIndexBuffer(&d3dIbv);

    auto* gfxSrvHeap = static_cast<ID3D12DescriptorHeap*>(devForDestroy->srvHeapNative());
    ID3D12DescriptorHeap* heaps[] = { gfxSrvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);

    D3D12_GPU_DESCRIPTOR_HANDLE heapStart;
    heapStart.ptr = devForDestroy->srvGpuDescriptorHandle(0);
    cmdList->SetGraphicsRootDescriptorTable(app_slots::bindlessSrvTable, heapStart);

    for (uint32_t i = 0; i < static_cast<uint32_t>(drawCmds.size()); ++i) {
        BindlessIndices bi{};
        bi.drawDataIdx = devForDestroy->bindlessSrvIndex(perObjectBuffer);
        bi.drawIndex = totalSlots + drawCmds[i].baseDrawIndex;
        cmdList->SetGraphicsRoot32BitConstants(app_slots::bindlessIndices, 16, &bi, 0);
        cmdList->DrawIndexedInstanced(
            drawCmds[i].indexCount, drawCmds[i].instanceCount, drawCmds[i].indexOffset,
            static_cast<INT>(drawCmds[i].vertexOffset), 0
        );
    }
}
