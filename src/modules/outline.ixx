module;

#include <d3d12.h>
#include <flecs.h>
#include <wrl.h>
#include <cstdint>
#include <vector>
#include "d3dx12_clean.h"

export module outline;

export import scene;
export import ecs_components;
export import common;

export class OutlineRenderer
{
   public:
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;

    void createResources(
        ID3D12Device2* device,
        ID3D12RootSignature* rootSig,
        D3D12_SHADER_BYTECODE vs,
        D3D12_SHADER_BYTECODE ps
    );

    void reloadPSO(
        ID3D12Device2* device,
        ID3D12RootSignature* rootSig,
        D3D12_SHADER_BYTECODE vs,
        D3D12_SHADER_BYTECODE ps
    );

    void render(
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> cmdList,
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
    );
};
