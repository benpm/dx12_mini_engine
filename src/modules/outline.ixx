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
export import gfx;

export struct OutlineRenderContext
{
    ID3D12RootSignature* rootSig = nullptr;
    const D3D12_VERTEX_BUFFER_VIEW* vbv = nullptr;
    const D3D12_INDEX_BUFFER_VIEW* ibv = nullptr;
    ID3D12DescriptorHeap* sceneSrvHeap = nullptr;
    UINT srvDescSize = 0;
    uint32_t curBackBufIdx = 0;
    D3D12_GPU_VIRTUAL_ADDRESS perFrameAddr = 0;
    D3D12_GPU_VIRTUAL_ADDRESS perPassAddr = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    const D3D12_VIEWPORT* viewport = nullptr;
    const D3D12_RECT* scissorRect = nullptr;
};

export class OutlineRenderer
{
   public:
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;

    void createResources(
        gfx::IDevice& dev,
        ID3D12RootSignature* rootSig,
        D3D12_SHADER_BYTECODE vs,
        D3D12_SHADER_BYTECODE ps
    );

    void reloadPSO(
        gfx::IDevice& dev,
        ID3D12RootSignature* rootSig,
        D3D12_SHADER_BYTECODE vs,
        D3D12_SHADER_BYTECODE ps
    );

    void render(
        gfx::ICommandList& cmdRef,
        const OutlineRenderContext& ctx,
        const std::vector<DrawCmd>& drawCmds,
        const std::vector<flecs::entity>& drawIndexToEntity,
        flecs::entity hoveredEntity,
        flecs::entity selectedEntity
    );
};
