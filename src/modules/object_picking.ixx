module;

#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>
#include "d3dx12_clean.h"
#include <cstdint>

export module object_picking;

import common;

using Microsoft::WRL::ComPtr;

export class ObjectPicker
{
   public:
    void createResources(
        ComPtr<ID3D12Device2> device,
        uint32_t width,
        uint32_t height,
        ComPtr<ID3D12RootSignature> rootSig
    );
    void resize(ComPtr<ID3D12Device2> device, uint32_t width, uint32_t height);

    // Read back the result from the previous frame's copy
    void readPickResult();

    // Copy the pixel at (x, y) from the ID RT to the readback buffer
    void copyPickedPixel(ComPtr<ID3D12GraphicsCommandList2> cmdList, uint32_t x, uint32_t y);

    D3D12_CPU_DESCRIPTOR_HANDLE getRTV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE getDSV() const;

    ComPtr<ID3D12PipelineState> pso;

    static constexpr uint32_t invalidID = UINT32_MAX;
    uint32_t pickedIndex = invalidID;

   private:
    ComPtr<ID3D12Resource> idRT;
    ComPtr<ID3D12Resource> depthBuffer;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12Resource> readbackBuffer;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool pendingRead = false;
};
