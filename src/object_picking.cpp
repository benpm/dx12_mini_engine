module;

#include <d3d12.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <wrl.h>
#include <cstdint>
#include "d3dx12_clean.h"
#include "id_ps_cso.h"
#include "vertex_shader_cso.h"

module object_picking;

using Microsoft::WRL::ComPtr;

ObjectPicker::~ObjectPicker()
{
    if (devForDestroy) {
        if (idRT.isValid()) {
            devForDestroy->destroy(idRT);
        }
        if (depthBuffer.isValid()) {
            devForDestroy->destroy(depthBuffer);
        }
        if (pso.isValid()) {
            devForDestroy->destroy(pso);
        }
        if (vsHandle.isValid()) {
            devForDestroy->destroy(vsHandle);
        }
        if (psHandle.isValid()) {
            devForDestroy->destroy(psHandle);
        }
        for (auto& slot : readbackSlots) {
            if (slot.buffer.isValid()) {
                devForDestroy->destroy(slot.buffer);
            }
        }
    }
}

void ObjectPicker::createResources(
    gfx::IDevice& dev,
    uint32_t width,
    uint32_t height,
    ID3D12RootSignature* rootSig
)
{
    devForDestroy = &dev;
    width_ = width;
    height_ = height;

    {
        gfx::TextureDesc td{};
        td.width = width;
        td.height = height;
        td.format = gfx::Format::R32Uint;
        td.usage = gfx::TextureUsage::RenderTarget;
        td.initialState = gfx::ResourceState::RenderTarget;
        td.useClearValue = true;
        td.clearColor[0] = static_cast<float>(invalidID);
        td.debugName = "picker_idRT";
        idRT = dev.createTexture(td);
    }

    {
        gfx::TextureDesc td{};
        td.width = width;
        td.height = height;
        td.format = gfx::Format::D32Float;
        td.usage = gfx::TextureUsage::DepthStencil;
        td.initialState = gfx::ResourceState::DepthWrite;
        td.useClearValue = true;
        td.clearDepth = 1.0f;
        td.debugName = "picker_depth";
        depthBuffer = dev.createTexture(td);
    }

    // --- Readback buffer ring ---
    for (auto& slot : readbackSlots) {
        gfx::BufferDesc bd{};
        bd.size = 256;  // 256-byte aligned for the single-pixel R32_UINT footprint
        bd.usage = gfx::BufferUsage::Readback;
        bd.debugName = "picker_readback";
        slot.buffer = dev.createBuffer(bd);
        slot.pendingRead = false;
        slot.fenceValue = 0;
    }
    writeSlot = 0;

    // --- PSO (reuses scene vertex shader, ID pixel shader, R32_UINT target) ---
    {
        gfx::ShaderDesc vsDesc{};
        vsDesc.stage = gfx::ShaderStage::Vertex;
        vsDesc.bytecode = g_vertex_shader;
        vsDesc.bytecodeSize = sizeof(g_vertex_shader);
        vsDesc.debugName = "picker_vs";
        vsHandle = dev.createShader(vsDesc);

        gfx::ShaderDesc psDesc{};
        psDesc.stage = gfx::ShaderStage::Pixel;
        psDesc.bytecode = g_id_ps;
        psDesc.bytecodeSize = sizeof(g_id_ps);
        psDesc.debugName = "picker_ps";
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
        gd.numRenderTargets = 1;
        gd.renderTargetFormats[0] = gfx::Format::R32Uint;
        gd.depthStencilFormat = gfx::Format::D32Float;
        gd.depthStencil.depthEnable = true;
        gd.depthStencil.depthWrite = true;
        gd.depthStencil.depthCompare = gfx::CompareOp::Less;
        gd.nativeRootSignatureOverride = rootSig;
        gd.debugName = "picker_pso";
        pso = dev.createGraphicsPipeline(gd);
    }
}

void ObjectPicker::resize(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    devForDestroy = &dev;
    if (width == 0 || height == 0 || (width == width_ && height == height_)) {
        return;
    }
    width_ = width;
    height_ = height;

    if (idRT.isValid()) {
        dev.destroy(idRT);
    }
    {
        gfx::TextureDesc td{};
        td.width = width;
        td.height = height;
        td.format = gfx::Format::R32Uint;
        td.usage = gfx::TextureUsage::RenderTarget;
        td.initialState = gfx::ResourceState::RenderTarget;
        td.useClearValue = true;
        td.clearColor[0] = static_cast<float>(invalidID);
        td.debugName = "picker_idRT";
        idRT = dev.createTexture(td);
    }

    if (depthBuffer.isValid()) {
        dev.destroy(depthBuffer);
    }
    {
        gfx::TextureDesc td{};
        td.width = width;
        td.height = height;
        td.format = gfx::Format::D32Float;
        td.usage = gfx::TextureUsage::DepthStencil;
        td.initialState = gfx::ResourceState::DepthWrite;
        td.useClearValue = true;
        td.clearDepth = 1.0f;
        td.debugName = "picker_depth";
        depthBuffer = dev.createTexture(td);
    }
}

uint64_t ObjectPicker::getRTV() const
{
    return devForDestroy->rtvHandle(idRT);
}

uint64_t ObjectPicker::getDSV() const
{
    return devForDestroy->dsvHandle(depthBuffer);
}

void ObjectPicker::copyPickedPixel(gfx::ICommandList& cmdRef, uint32_t x, uint32_t y)
{
    auto* cmdList = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
    if (x >= width_ || y >= height_) {
        return;
    }

    auto& slot = readbackSlots[writeSlot];
    auto* idRes = static_cast<ID3D12Resource*>(devForDestroy->nativeResource(idRT));
    auto* slotRes = static_cast<ID3D12Resource*>(devForDestroy->nativeResource(slot.buffer));

    // Transition ID RT to COPY_SOURCE
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        idRes, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE
    );
    cmdList->ResourceBarrier(1, &barrier);

    // Copy the single pixel at (x, y)
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = idRes;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = slotRes;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Offset = 0;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_UINT;
    dst.PlacedFootprint.Footprint.Width = 1;
    dst.PlacedFootprint.Footprint.Height = 1;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = 256;

    D3D12_BOX srcBox = { x, y, 0, x + 1, y + 1, 1 };
    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, &srcBox);

    // Transition back to RENDER_TARGET
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        idRes, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET
    );
    cmdList->ResourceBarrier(1, &barrier);

    slot.pendingRead = true;
    slot.fenceValue = 0;
    writeSlot = (writeSlot + 1) % readbackRingSize;
}

void ObjectPicker::setPendingReadbackFence(uint64_t fenceValue)
{
    uint32_t lastSlot = (writeSlot + readbackRingSize - 1) % readbackRingSize;
    auto& slot = readbackSlots[lastSlot];
    if (!slot.pendingRead || slot.fenceValue != 0) {
        return;
    }
    slot.fenceValue = fenceValue;
}

void ObjectPicker::readPickResult(uint64_t completedFenceValue)
{
    int bestSlot = -1;
    uint64_t bestFence = 0;
    for (uint32_t i = 0; i < readbackRingSize; ++i) {
        const auto& slot = readbackSlots[i];
        if (!slot.pendingRead || slot.fenceValue == 0 || completedFenceValue < slot.fenceValue) {
            continue;
        }
        if (slot.fenceValue >= bestFence) {
            bestFence = slot.fenceValue;
            bestSlot = static_cast<int>(i);
        }
    }
    if (bestSlot < 0) {
        return;
    }

    auto& slot = readbackSlots[bestSlot];
    slot.pendingRead = false;
    slot.fenceValue = 0;

    void* data = devForDestroy->map(slot.buffer);
    if (data) {
        pickedIndex = *static_cast<uint32_t*>(data);
        devForDestroy->unmap(slot.buffer);
    } else {
        pickedIndex = invalidID;
    }
}
