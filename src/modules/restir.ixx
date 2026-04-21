module;

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <vector>
#include "d3dx12_clean.h"

export module restir;

export import common;
export import math;

export class ReStirRenderer
{
   public:
    struct Settings
    {
        bool enabled = true;
        int initialSamples = 32;
        int temporalMClamp = 20;
        float spatialRadius = 16.0f;
        int spatialSamples = 4;
    } settings;

    void createResources(ID3D12Device2* device, uint32_t width, uint32_t height);
    void resize(ID3D12Device2* device, uint32_t width, uint32_t height);

    void render(
        ID3D12GraphicsCommandList2* cmd,
        ID3D12Resource* tlas,
        ID3D12Resource* lightBuffer,
        uint32_t lightCount,
        ID3D12Resource* normalRT,
        ID3D12Resource* albedoRT,
        ID3D12Resource* materialRT,
        ID3D12Resource* motionRT,
        ID3D12Resource* depthBuffer,
        ID3D12Resource* outputHdrRT,
        D3D12_GPU_VIRTUAL_ADDRESS perFrameCB,
        uint32_t width,
        uint32_t height,
        uint32_t frameIndex
    );

   private:
    Microsoft::WRL::ComPtr<ID3D12Resource> reservoirs[2];  // uint4 {lightIdx, w_sum, M, W}

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> uavHeap;
    UINT uavDescSize = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> initialPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> temporalPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spatialPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> resolvePSO;

    void createShaders(ID3D12Device2* device);
    void createTextures(ID3D12Device2* device, uint32_t width, uint32_t height);
};