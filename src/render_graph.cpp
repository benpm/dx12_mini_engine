module;

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

module render_graph;

namespace rg
{

    RenderGraph::RenderGraph(gfx::IDevice& dev) : device(&dev)
    {
        resources.reserve(16);
        passes.reserve(16);
    }

    RenderGraph::~RenderGraph() {}

    void RenderGraph::reset()
    {
        passes.clear();
        resources.clear();
        externalResources.clear();
    }

    ResourceHandle RenderGraph::importTexture(
        const std::string& name,
        gfx::TextureHandle handle,
        gfx::ResourceState initialState
    )
    {
        if (externalResources.count(name)) {
            return externalResources[name];
        }

        ResourceRecord record;
        record.name = name;
        record.gfxHandle = handle;
        record.currentState = initialState;
        record.isExternal = true;

        ResourceHandle rh = getNextHandle();
        resources.push_back(record);
        externalResources[name] = rh;
        return rh;
    }

    void RenderGraph::addPass(
        const std::string& name,
        std::function<void(RenderGraphBuilder&)> setup,
        PassExecuteCallback execute
    )
    {
        PassRecord pass;
        pass.name = name;
        pass.reads.reserve(8);
        pass.writes.reserve(8);
        pass.readStates.reserve(8);
        pass.writeStates.reserve(8);
        pass.execute = execute;

        BuilderImpl builder(*this, pass);
        setup(builder);

        passes.push_back(std::move(pass));
    }

    void RenderGraph::execute(gfx::ICommandList& cmd)
    {
        for (auto& pass : passes) {
            for (size_t i = 0; i < pass.reads.size(); ++i) {
                ResourceHandle h = pass.reads[i];
                gfx::ResourceState target = pass.readStates[i];
                if (resources[h.id].currentState != target) {
                    if (resources[h.id].gfxHandle.isValid()) {
                        cmd.barrier(
                            resources[h.id].gfxHandle, resources[h.id].currentState, target
                        );
                    }
                    resources[h.id].currentState = target;
                }
            }

            for (size_t i = 0; i < pass.writes.size(); ++i) {
                ResourceHandle h = pass.writes[i];
                gfx::ResourceState target = pass.writeStates[i];
                if (resources[h.id].currentState != target) {
                    if (resources[h.id].gfxHandle.isValid()) {
                        cmd.barrier(
                            resources[h.id].gfxHandle, resources[h.id].currentState, target
                        );
                    }
                    resources[h.id].currentState = target;
                }
            }

            BuilderImpl builder(*this, pass);
            pass.execute(cmd, builder);
        }
    }

    // BuilderImpl implementation
    ResourceHandle
    RenderGraph::BuilderImpl::createTexture(const std::string& name, const TextureDesc& desc)
    {
        for (uint32_t i = 0; i < graph.resources.size(); ++i) {
            if (!graph.resources[i].isExternal && graph.resources[i].name == name) {
                return { i };
            }
        }

        gfx::TextureDesc td{};
        td.format = desc.format;
        td.width = desc.width;
        td.height = desc.height;
        td.mipLevels = desc.mipLevels;
        td.usage = desc.usage;
        std::memcpy(td.clearColor, desc.clearColor, sizeof(float) * 4);
        td.clearDepth = desc.clearDepth;
        td.clearStencil = desc.clearStencil;
        td.useClearValue = desc.useClearValue;
        td.initialState = desc.initialState;

        ResourceRecord record;
        record.name = name;
        record.isExternal = false;
        record.gfxHandle = graph.device->createTexture(td);
        record.currentState = desc.initialState;

        ResourceHandle handle = graph.getNextHandle();
        graph.resources.push_back(record);
        return handle;
    }

    void RenderGraph::BuilderImpl::writeRenderTarget(ResourceHandle handle, uint32_t /*arraySlice*/)
    {
        pass.writes.push_back(handle);
        pass.writeStates.push_back(gfx::ResourceState::RenderTarget);
    }

    void RenderGraph::BuilderImpl::writeDepthStencil(ResourceHandle handle, uint32_t /*arraySlice*/)
    {
        pass.writes.push_back(handle);
        pass.writeStates.push_back(gfx::ResourceState::DepthWrite);
    }

    void RenderGraph::BuilderImpl::readTexture(ResourceHandle handle, gfx::ResourceState state)
    {
        pass.reads.push_back(handle);
        pass.readStates.push_back(state);
    }

    gfx::TextureHandle RenderGraph::BuilderImpl::getTexture(ResourceHandle handle)
    {
        return graph.resources[handle.id].gfxHandle;
    }

}  // namespace rg
