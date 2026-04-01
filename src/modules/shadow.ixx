module;

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <vector>
#include "d3dx12_clean.h"

export module shadow;

export import scene;
export import common;

export class ShadowRenderer
{
   public:
    // Settings
    bool enabled = true;
    float bias = 0.0002f;
    int rasterDepthBias = 1000;
    float rasterSlopeBias = 1.0f;
    float rasterBiasClamp = 0.0f;
    float lightDistance = 25.0f;
    float orthoSize = 30.0f;
    float nearPlane = 0.1f;
    float farPlane = 60.0f;

    static constexpr uint32_t mapSize = 2048;

    Microsoft::WRL::ComPtr<ID3D12Resource> shadowMap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;

    // sceneSrvHeap: shadow SRV placed at sceneSrvHeap[shadowSrvSlot]
    void createResources(
        ID3D12Device2* device,
        ID3D12RootSignature* rootSig,
        D3D12_SHADER_BYTECODE vs,
        ID3D12DescriptorHeap* sceneSrvHeap,
        UINT srvDescSize,
        INT shadowSrvSlot
    );

    void reloadPSO(ID3D12Device2* device, ID3D12RootSignature* rootSig, D3D12_SHADER_BYTECODE vs);

    // Compute the light view-proj matrix from the current config.
    // dirLightDir is the direction FROM the light (as stored in Application::dirLightDir).
    mat4 computeLightViewProj(vec3 dirLightDir) const;

    void render(
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> cmdList,
        const D3D12_VERTEX_BUFFER_VIEW& vbv,
        const D3D12_INDEX_BUFFER_VIEW& ibv,
        ID3D12DescriptorHeap* sceneSrvHeap,
        UINT srvDescSize,
        uint32_t curBackBufIdx,
        const std::vector<DrawCmd>& drawCmds,
        uint32_t totalSlots
    );
};
