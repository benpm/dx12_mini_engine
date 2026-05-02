module;

#include <d3d12.h>
#include <wrl.h>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "d3dx12_clean.h"

module render_graph;

using Microsoft::WRL::ComPtr;

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
                    if (resources[h.id].isExternal) {
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
                    if (resources[h.id].isExternal) {
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

        ResourceRecord record;
        record.name = name;
        record.isExternal = false;
        record.currentState = gfx::ResourceState::Common;

        D3D12_RESOURCE_DESC d3dDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            desc.format, desc.width, desc.height, 1, desc.mipLevels, 1, 0, desc.flags
        );
        record.desc = d3dDesc;

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_CLEAR_VALUE* clearValPtr =
            desc.useClearValue ? const_cast<D3D12_CLEAR_VALUE*>(&desc.clearValue) : nullptr;

        auto* d3dDevice = static_cast<ID3D12Device2*>(graph.device->nativeHandle());
        d3dDevice->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &d3dDesc, D3D12_RESOURCE_STATE_COMMON, clearValPtr,
            IID_PPV_ARGS(&record.resource)
        );

        ResourceHandle handle = graph.getNextHandle();
        graph.resources.push_back(record);
        return handle;
    }

    void RenderGraph::BuilderImpl::writeRenderTarget(
        ResourceHandle handle,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv
    )
    {
        pass.writes.push_back(handle);
        pass.writeStates.push_back(gfx::ResourceState::RenderTarget);
    }

    void RenderGraph::BuilderImpl::writeDepthStencil(
        ResourceHandle handle,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv
    )
    {
        pass.writes.push_back(handle);
        pass.writeStates.push_back(gfx::ResourceState::DepthWrite);
    }

    void RenderGraph::BuilderImpl::readTexture(ResourceHandle handle, gfx::ResourceState state)
    {
        pass.reads.push_back(handle);
        pass.readStates.push_back(state);
    }

    ID3D12Resource* RenderGraph::BuilderImpl::getResource(ResourceHandle handle)
    {
        auto& rec = graph.resources[handle.id];
        if (rec.isExternal) {
            return static_cast<ID3D12Resource*>(graph.device->nativeResource(rec.gfxHandle));
        }
        return rec.resource.Get();
    }

}  // namespace rg
