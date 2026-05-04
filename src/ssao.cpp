module;

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>
#include "d3dx12_clean.h"
#include "fullscreen_vs_cso.h"
#include "ssao_blur_ps_cso.h"
#include "ssao_ps_cso.h"

module ssao;

using Microsoft::WRL::ComPtr;
using namespace DirectX;

static ID3D12Device2* nativeDev(gfx::IDevice& dev)
{
    return static_cast<ID3D12Device2*>(dev.nativeHandle());
}
static ID3D12GraphicsCommandList2* nativeCmd(gfx::ICommandList& cmdRef)
{
    return static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
}
static void transitionSsaoResource(
    gfx::ICommandList& cmdRef,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after
)
{
    auto* cmdList = nativeCmd(cmdRef);
    CD3DX12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
    cmdList->ResourceBarrier(1, &b);
}

struct SsaoCBData
{
    XMFLOAT4X4 view;
    XMFLOAT4X4 proj;
    XMFLOAT4X4 invProj;
    XMFLOAT4 samples[32];
    float radius;
    float bias;
    float screenWidth;
    float screenHeight;
    int kernelSize;
    float _pad[11];
};
static_assert(sizeof(SsaoCBData) == 768);

SsaoRenderer::~SsaoRenderer()
{
    if (devForDestroy) {
        if (ssaoRT.isValid()) {
            devForDestroy->destroy(ssaoRT);
        }
        if (ssaoBlurRT.isValid()) {
            devForDestroy->destroy(ssaoBlurRT);
        }
        if (noiseTexture.isValid()) {
            devForDestroy->destroy(noiseTexture);
        }
        if (cbvBuffer.isValid()) {
            devForDestroy->destroy(cbvBuffer);
        }
        if (ssaoPSO.isValid()) {
            devForDestroy->destroy(ssaoPSO);
        }
        if (blurPSO.isValid()) {
            devForDestroy->destroy(blurPSO);
        }
        if (vsHandle.isValid()) {
            devForDestroy->destroy(vsHandle);
        }
        if (ssaoPsHandle.isValid()) {
            devForDestroy->destroy(ssaoPsHandle);
        }
        if (blurPsHandle.isValid()) {
            devForDestroy->destroy(blurPsHandle);
        }
    }
}

void SsaoRenderer::createRTs(
    gfx::IDevice& dev,
    uint32_t width,
    uint32_t height,
    gfx::TextureHandle normalBuffer,
    gfx::TextureHandle depthBuffer
)
{
    width = std::max(1u, width);
    height = std::max(1u, height);

    if (ssaoRT.isValid()) {
        dev.destroy(ssaoRT);
        ssaoRT = {};
    }
    if (ssaoBlurRT.isValid()) {
        dev.destroy(ssaoBlurRT);
        ssaoBlurRT = {};
    }

    // Normal SRV: auto-created by gfx backend (RGBA8Unorm + ShaderResource)
    normalSrvIdx = dev.bindlessSrvIndex(normalBuffer);

    // Depth SRV: typeless resource, needs typed R32_FLOAT_X8X24 view
    depthSrvIdx = dev.createTypedSrv(depthBuffer, gfx::Format::R32FloatX8X24Typeless);

    // ssaoRT
    {
        gfx::TextureDesc td{};
        td.width = width;
        td.height = height;
        td.format = gfx::Format::R8Unorm;
        td.usage = gfx::TextureUsage::RenderTarget | gfx::TextureUsage::ShaderResource;
        td.initialState = gfx::ResourceState::PixelShaderResource;
        td.debugName = "ssao_rt";
        ssaoRT = dev.createTexture(td);
        ssaoRtSrvIdx = dev.bindlessSrvIndex(ssaoRT);
    }

    // ssaoBlurRT — the gfx backend auto-creates an SRV in the bindless heap
    // (via TextureUsage::ShaderResource) so callers can use bindlessSrvIndex(blurRT()).
    {
        gfx::TextureDesc td{};
        td.width = width;
        td.height = height;
        td.format = gfx::Format::R8Unorm;
        td.usage = gfx::TextureUsage::RenderTarget | gfx::TextureUsage::ShaderResource;
        td.initialState = gfx::ResourceState::PixelShaderResource;
        td.debugName = "ssao_blur_rt";
        ssaoBlurRT = dev.createTexture(td);
    }
}

void SsaoRenderer::createResources(
    gfx::IDevice& dev,
    uint32_t width,
    uint32_t height,
    gfx::TextureHandle normalBuffer,
    gfx::TextureHandle depthBuffer
)
{
    devForDestroy = &dev;
    auto* device = nativeDev(dev);
    (void)device;

    {
        gfx::ShaderDesc vsd{};
        vsd.bytecode = g_fullscreen_vs;
        vsd.bytecodeSize = sizeof(g_fullscreen_vs);
        vsd.stage = gfx::ShaderStage::Vertex;
        vsHandle = dev.createShader(vsd);

        gfx::ShaderDesc psd{};
        psd.bytecode = g_ssao_ps;
        psd.bytecodeSize = sizeof(g_ssao_ps);
        psd.stage = gfx::ShaderStage::Pixel;
        ssaoPsHandle = dev.createShader(psd);

        gfx::GraphicsPipelineDesc pd{};
        pd.vs = vsHandle;
        pd.ps = ssaoPsHandle;
        pd.renderTargetFormats[0] = gfx::Format::R8Unorm;
        pd.numRenderTargets = 1;
        pd.depthStencil.depthEnable = false;
        pd.depthStencil.depthWrite = false;
        pd.nativeRootSignatureOverride = dev.bindlessRootSigNative();
        pd.debugName = "ssao_pso";
        ssaoPSO = dev.createGraphicsPipeline(pd);
    }
    {
        gfx::ShaderDesc psd{};
        psd.bytecode = g_ssao_blur_ps;
        psd.bytecodeSize = sizeof(g_ssao_blur_ps);
        psd.stage = gfx::ShaderStage::Pixel;
        blurPsHandle = dev.createShader(psd);

        gfx::GraphicsPipelineDesc pd{};
        pd.vs = vsHandle;
        pd.ps = blurPsHandle;
        pd.renderTargetFormats[0] = gfx::Format::R8Unorm;
        pd.numRenderTargets = 1;
        pd.depthStencil.depthEnable = false;
        pd.depthStencil.depthWrite = false;
        pd.nativeRootSignatureOverride = dev.bindlessRootSigNative();
        pd.debugName = "ssao_blur_pso";
        blurPSO = dev.createGraphicsPipeline(pd);
    }

    {
        gfx::BufferDesc bd{};
        bd.size = sizeof(SsaoCBData);
        bd.usage = gfx::BufferUsage::Upload;
        bd.debugName = "ssao_cb";
        cbvBuffer = dev.createBuffer(bd);
        cbvMapped = dev.map(cbvBuffer);
    }

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < 32; ++i) {
        XMFLOAT3 s = { dist(rng) * 2.0f - 1.0f, dist(rng) * 2.0f - 1.0f, dist(rng) };
        XMVECTOR sv = XMVector3Normalize(XMLoadFloat3(&s));
        float sc = static_cast<float>(i) / 32.0f;
        sv = XMVectorScale(sv, dist(rng) * (0.1f + sc * sc * 0.9f));
        XMStoreFloat4(&kernel[i], XMVectorSetW(sv, 0.0f));
    }

    {
        gfx::TextureDesc td{};
        td.width = 4;
        td.height = 4;
        td.format = gfx::Format::RG32Float;
        td.usage = gfx::TextureUsage::ShaderResource;
        td.initialState = gfx::ResourceState::CopyDest;
        td.debugName = "ssao_noise";
        noiseTexture = dev.createTexture(td);

        auto* noiseRes = static_cast<ID3D12Resource*>(dev.nativeResource(noiseTexture));
        auto noiseDesc = noiseRes->GetDesc();
        UINT64 uploadSize = 0;
        device->GetCopyableFootprints(&noiseDesc, 0, 1, 0, &noiseFp, nullptr, nullptr, &uploadSize);

        const CD3DX12_HEAP_PROPERTIES uhp(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC ubDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
        device->CreateCommittedResource(
            &uhp, D3D12_HEAP_FLAG_NONE, &ubDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&noiseUploadBuf)
        );

        XMFLOAT2 noiseData[16];
        for (int i = 0; i < 16; ++i) {
            noiseData[i] = { dist(rng) * 2.0f - 1.0f, dist(rng) * 2.0f - 1.0f };
        }
        void* mapped = nullptr;
        noiseUploadBuf->Map(0, nullptr, &mapped);
        uint8_t* dst = static_cast<uint8_t*>(mapped) + noiseFp.Offset;
        for (uint32_t row = 0; row < 4; ++row) {
            memcpy(
                dst + row * noiseFp.Footprint.RowPitch, &noiseData[row * 4], 4 * sizeof(XMFLOAT2)
            );
        }
        noiseUploadBuf->Unmap(0, nullptr);
        noisePendingUpload = true;

        // SRV for noiseTexture auto-created by gfx backend (RG32Float + ShaderResource)
        noiseSrvIdx = dev.bindlessSrvIndex(noiseTexture);
    }

    createRTs(dev, width, height, normalBuffer, depthBuffer);
}

void SsaoRenderer::resize(
    gfx::IDevice& dev,
    uint32_t width,
    uint32_t height,
    gfx::TextureHandle normalBuffer,
    gfx::TextureHandle depthBuffer
)
{
    createRTs(dev, width, height, normalBuffer, depthBuffer);
}

void SsaoRenderer::render(
    gfx::ICommandList& cmdRef,
    const mat4& view,
    const mat4& proj,
    uint32_t width,
    uint32_t height
)
{
    auto* cmdList = nativeCmd(cmdRef);
    if (!enabled) {
        return;
    }

    auto* ssaoRtRes = static_cast<ID3D12Resource*>(devForDestroy->nativeResource(ssaoRT));
    auto* ssaoBlurRtRes = static_cast<ID3D12Resource*>(devForDestroy->nativeResource(ssaoBlurRT));
    auto* noiseRes = static_cast<ID3D12Resource*>(devForDestroy->nativeResource(noiseTexture));
    auto* cbvRes = static_cast<ID3D12Resource*>(devForDestroy->nativeResource(cbvBuffer));

    if (noisePendingUpload) {
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = noiseUploadBuf.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = noiseFp;
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = noiseRes;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        transitionSsaoResource(
            cmdRef, noiseRes, D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        noisePendingUpload = false;
    }

    {
        SsaoCBData* cb = static_cast<SsaoCBData*>(cbvMapped);
        memcpy(&cb->view, &view, sizeof(XMFLOAT4X4));
        memcpy(&cb->proj, &proj, sizeof(XMFLOAT4X4));
        XMMATRIX invProjM =
            XMMatrixInverse(nullptr, XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&proj)));
        XMStoreFloat4x4(&cb->invProj, invProjM);
        memcpy(cb->samples, kernel, sizeof(kernel));
        cb->radius = radius;
        cb->bias = bias;
        cb->screenWidth = static_cast<float>(width);
        cb->screenHeight = static_cast<float>(height);
        cb->kernelSize = kernelSize;
    }

    D3D12_VIEWPORT vp = { 0, 0, (float)width, (float)height, 0, 1 };
    D3D12_RECT sr = { 0, 0, (LONG)width, (LONG)height };

    {
        transitionSsaoResource(
            cmdRef, ssaoRtRes, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        D3D12_CPU_DESCRIPTOR_HANDLE rtv;
        rtv.ptr = static_cast<SIZE_T>(devForDestroy->rtvHandle(ssaoRT));
        FLOAT white[] = { 1, 1, 1, 1 };
        cmdList->ClearRenderTargetView(rtv, white, 0, nullptr);
        // bindPipeline auto-binds the matching (bindless) root sig.
        cmdRef.bindPipeline(ssaoPSO);
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sr);
        cmdList->OMSetRenderTargets(1, &rtv, false, nullptr);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
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

        struct BindlessSsaoIndices
        {
            uint32_t normalIdx;
            uint32_t depthIdx;
            uint32_t noiseIdx;
            uint32_t _pad;
        };
        BindlessSsaoIndices bi{};
        bi.normalIdx = normalSrvIdx;
        bi.depthIdx = depthSrvIdx;
        bi.noiseIdx = noiseSrvIdx;
        cmdList->SetGraphicsRoot32BitConstants(0, 4, &bi, 0);  // Slot 0: b0
        cmdList->SetGraphicsRootConstantBufferView(
            2, cbvRes->GetGPUVirtualAddress()
        );  // Slot 2: b2 (per-pass)
        cmdList->DrawInstanced(3, 1, 0, 0);
        transitionSsaoResource(
            cmdRef, ssaoRtRes, D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
    }

    {
        transitionSsaoResource(
            cmdRef, ssaoBlurRtRes, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        D3D12_CPU_DESCRIPTOR_HANDLE rtv;
        rtv.ptr = static_cast<SIZE_T>(devForDestroy->rtvHandle(ssaoBlurRT));
        // bindPipeline auto-binds the matching (bindless) root sig.
        cmdRef.bindPipeline(blurPSO);
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sr);
        cmdList->OMSetRenderTargets(1, &rtv, false, nullptr);
        auto* gfxHeap2 = static_cast<ID3D12DescriptorHeap*>(devForDestroy->srvHeapNative());
        auto* samplerHeap2 =
            static_cast<ID3D12DescriptorHeap*>(devForDestroy->samplerHeapNative());
        ID3D12DescriptorHeap* heaps[] = { gfxHeap2, samplerHeap2 };
        cmdList->SetDescriptorHeaps(2, heaps);

        D3D12_GPU_DESCRIPTOR_HANDLE heapStart;
        heapStart.ptr = devForDestroy->srvGpuDescriptorHandle(0);
        cmdList->SetGraphicsRootDescriptorTable(3, heapStart);  // Slot 3: SRV table
        D3D12_GPU_DESCRIPTOR_HANDLE samplerHeapStart;
        samplerHeapStart.ptr = devForDestroy->samplerGpuDescriptorHandle(0);
        cmdList->SetGraphicsRootDescriptorTable(4, samplerHeapStart);  // Slot 4: sampler table

        struct BindlessBlurIndices
        {
            uint32_t ssaoIdx;
            uint32_t _pad[3];
        };
        BindlessBlurIndices bi{};
        bi.ssaoIdx = ssaoRtSrvIdx;
        cmdList->SetGraphicsRoot32BitConstants(0, 4, &bi, 0);  // Slot 0: b0
        cmdList->DrawInstanced(3, 1, 0, 0);
        transitionSsaoResource(
            cmdRef, ssaoBlurRtRes, D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
    }
}
