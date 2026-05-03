module;

#include <functional>
#include <map>
#include <memory>
#include <span>
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

        gfx::ResourceState stateToUse = initialState;
        auto it = persistentExternalStates.find(name);
        if (it != persistentExternalStates.end() && it->second.handle.id == handle.id) {
            stateToUse = it->second.state;
        }

        ResourceRecord record;
        record.name = name;
        record.gfxHandle = handle;
        record.currentState = stateToUse;
        record.isExternal = true;

        ResourceHandle rh = getNextHandle();
        resources.push_back(record);
        externalResources[name] = rh;
        return rh;
    }

    void RenderGraph::clearPersistentState() { persistentExternalStates.clear(); }

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
        std::vector<gfx::TextureBarrier> batch;
        batch.reserve(16);
        for (auto& pass : passes) {
            batch.clear();
            auto enqueue = [&](ResourceHandle h, gfx::ResourceState target) {
                if (resources[h.id].currentState == target) {
                    return;
                }
                if (!resources[h.id].gfxHandle.isValid()) {
                    return;
                }
                batch.push_back({ resources[h.id].gfxHandle, resources[h.id].currentState, target });
                resources[h.id].currentState = target;
            };
            for (size_t i = 0; i < pass.reads.size(); ++i) {
                enqueue(pass.reads[i], pass.readStates[i]);
            }
            for (size_t i = 0; i < pass.writes.size(); ++i) {
                enqueue(pass.writes[i], pass.writeStates[i]);
            }
            if (!batch.empty()) {
                cmd.barriers(std::span<const gfx::TextureBarrier>(batch.data(), batch.size()));
            }

            BuilderImpl builder(*this, pass);
            pass.execute(cmd, builder);
        }

        // Persist final state of each external resource so the next frame's
        // importTexture matches the actual D3D12 state.
        for (auto& res : resources) {
            if (res.isExternal) {
                persistentExternalStates[res.name] = { res.gfxHandle, res.currentState };
            }
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
