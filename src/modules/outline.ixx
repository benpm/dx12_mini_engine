module;

#include <flecs.h>
#include <cstdint>
#include <vector>

export module outline;

export import scene;
export import ecs_components;
export import common;
export import gfx;

export struct OutlineRenderContext
{
    gfx::VertexBufferView vbv;
    gfx::IndexBufferView ibv;
    gfx::BufferHandle perObjectBuffer;
    uint64_t perFrameAddr;
    uint64_t perPassAddr;

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
    gfx::IDevice* devForDestroy = nullptr;
    ~OutlineRenderer();

    void
    createResources(gfx::IDevice& dev, gfx::ShaderBytecode vs = {}, gfx::ShaderBytecode ps = {});

    void reloadPSO(gfx::IDevice& dev, gfx::ShaderBytecode vs = {}, gfx::ShaderBytecode ps = {});

    void render(
        gfx::ICommandList& cmdRef,
        const OutlineRenderContext& ctx,
        const std::vector<DrawCmd>& drawCmds,
        const std::vector<flecs::entity>& drawIndexToEntity,
        flecs::entity hoveredEntity,
        flecs::entity selectedEntity
    );
};
