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
    gfx::VertexBufferView vbv{};
    gfx::IndexBufferView ibv{};
    uint64_t perObjHandle = 0;
    uint64_t perFrameAddr = 0;
    uint64_t perPassAddr = 0;
    uint64_t hdrRtv = 0;
    uint64_t dsv = 0;
    const gfx::Viewport* viewport = nullptr;
    const gfx::ScissorRect* scissorRect = nullptr;
};

export class OutlineRenderer
{
   public:
    gfx::PipelineHandle pso{};
    gfx::ShaderHandle vsHandle{};
    gfx::ShaderHandle psHandle{};
    // Track the device used for create so destroy can call back into it.
    gfx::IDevice* devForDestroy = nullptr;
    ~OutlineRenderer();

    void createResources(
        gfx::IDevice& dev,
        ID3D12RootSignature* rootSig,
        gfx::ShaderBytecode vs = {},
        gfx::ShaderBytecode ps = {}
    );

    void reloadPSO(
        gfx::IDevice& dev,
        ID3D12RootSignature* rootSig,
        gfx::ShaderBytecode vs = {},
        gfx::ShaderBytecode ps = {}
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
