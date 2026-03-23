module;

#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>
#include "d3dx12_clean.h"
#include <cstdint>
#include <spdlog/spdlog.h>
#include "id_ps_cso.h"
#include "vertex_shader_cso.h"

module object_picking;

using Microsoft::WRL::ComPtr;

void ObjectPicker::createResources(
    ComPtr<ID3D12Device2> device,
    uint32_t width,
    uint32_t height,
    ComPtr<ID3D12RootSignature> rootSig
)
{
    width_ = width;
    height_ = height;

    // --- ID render target (R32_UINT) ---
    {
        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = DXGI_FORMAT_R32_UINT;
        clearVal.Color[0] = static_cast<float>(invalidID);  // Clear to max uint
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_UINT, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
        );
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearVal,
            IID_PPV_ARGS(&idRT)
        ));
    }

    // --- Depth buffer ---
    {
        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = DXGI_FORMAT_D32_FLOAT;
        clearVal.DepthStencil = { 1.0f, 0 };
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_D32_FLOAT, width, height, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
        );
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearVal,
            IID_PPV_ARGS(&depthBuffer)
        ));
    }

    // --- RTV heap ---
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 1;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        chkDX(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap)));
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = DXGI_FORMAT_R32_UINT;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(
            idRT.Get(), &rtvDesc, rtvHeap->GetCPUDescriptorHandleForHeapStart()
        );
    }

    // --- DSV heap ---
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 1;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        chkDX(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&dsvHeap)));
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(
            depthBuffer.Get(), &dsvDesc, dsvHeap->GetCPUDescriptorHandleForHeapStart()
        );
    }

    // --- Readback buffer (256-byte aligned row pitch for a single R32_UINT pixel) ---
    {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_READBACK);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(256);
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&readbackBuffer)
        ));
    }

    // --- PSO (reuses scene vertex shader, ID pixel shader, R32_UINT target) ---
    {
        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = rootSig.Get();
        psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
        psoDesc.VS = { g_vertex_shader, sizeof(g_vertex_shader) };
        psoDesc.PS = { g_id_ps, sizeof(g_id_ps) };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_UINT;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc = { 1, 0 };
        chkDX(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));
    }
}

void ObjectPicker::resize(ComPtr<ID3D12Device2> device, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || (width == width_ && height == height_)) {
        return;
    }
    width_ = width;
    height_ = height;

    // Recreate ID RT
    {
        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = DXGI_FORMAT_R32_UINT;
        clearVal.Color[0] = static_cast<float>(invalidID);
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_UINT, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
        );
        idRT.Reset();
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearVal,
            IID_PPV_ARGS(&idRT)
        ));
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = DXGI_FORMAT_R32_UINT;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(
            idRT.Get(), &rtvDesc, rtvHeap->GetCPUDescriptorHandleForHeapStart()
        );
    }

    // Recreate depth buffer
    {
        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = DXGI_FORMAT_D32_FLOAT;
        clearVal.DepthStencil = { 1.0f, 0 };
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_D32_FLOAT, width, height, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
        );
        depthBuffer.Reset();
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearVal,
            IID_PPV_ARGS(&depthBuffer)
        ));
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(
            depthBuffer.Get(), &dsvDesc, dsvHeap->GetCPUDescriptorHandleForHeapStart()
        );
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE ObjectPicker::getRTV() const
{
    return rtvHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE ObjectPicker::getDSV() const
{
    return dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void ObjectPicker::copyPickedPixel(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    uint32_t x,
    uint32_t y
)
{
    if (x >= width_ || y >= height_) {
        pendingRead = false;
        return;
    }

    // Transition ID RT to COPY_SOURCE
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        idRT.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE
    );
    cmdList->ResourceBarrier(1, &barrier);

    // Copy the single pixel at (x, y)
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = idRT.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = readbackBuffer.Get();
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
        idRT.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET
    );
    cmdList->ResourceBarrier(1, &barrier);

    pendingRead = true;
}

void ObjectPicker::readPickResult()
{
    if (!pendingRead) {
        return;
    }
    pendingRead = false;

    void* data = nullptr;
    D3D12_RANGE readRange = { 0, sizeof(uint32_t) };
    if (SUCCEEDED(readbackBuffer->Map(0, &readRange, &data))) {
        pickedIndex = *static_cast<uint32_t*>(data);
        D3D12_RANGE writeRange = { 0, 0 };
        readbackBuffer->Unmap(0, &writeRange);
    } else {
        pickedIndex = invalidID;
    }
}
