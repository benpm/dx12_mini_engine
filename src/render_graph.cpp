module;

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <functional>
#include "d3dx12_clean.h"

module render_graph;

using Microsoft::WRL::ComPtr;

namespace rg {

RenderGraph::RenderGraph(ID3D12Device2* device) : device(device) {}

RenderGraph::~RenderGraph() {}

void RenderGraph::reset() {
    passes.clear();
    // Keep external resources, but maybe clear internal ones if we want transient resources
    // For a simple version, let's keep all resources and just clear passes
}

ResourceHandle RenderGraph::importTexture(const std::string& name, ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState) {
    if (externalResources.count(name)) {
        return externalResources[name];
    }

    ResourceRecord record;
    record.name = name;
    record.resource = resource;
    record.desc = resource->GetDesc();
    record.currentState = initialState;
    record.isExternal = true;

    ResourceHandle handle = getNextHandle();
    resources.push_back(record);
    externalResources[name] = handle;
    return handle;
}

void RenderGraph::addPass(const std::string& name, std::function<void(RenderGraphBuilder&)> setup, PassExecuteCallback execute) {
    PassRecord pass;
    pass.name = name;
    pass.execute = execute;

    BuilderImpl builder(*this, pass);
    setup(builder);

    passes.push_back(pass);
}

void RenderGraph::execute(ID3D12GraphicsCommandList2* cmdList) {
    for (auto& pass : passes) {
        // Handle transitions
        std::vector<D3D12_RESOURCE_BARRIER> barriers;

        for (size_t i = 0; i < pass.reads.size(); ++i) {
            ResourceHandle h = pass.reads[i];
            D3D12_RESOURCE_STATES targetState = pass.readStates[i];
            if (resources[h.id].currentState != targetState) {
                barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
                    resources[h.id].resource.Get(),
                    resources[h.id].currentState,
                    targetState
                ));
                resources[h.id].currentState = targetState;
            }
        }

        for (size_t i = 0; i < pass.writes.size(); ++i) {
            ResourceHandle h = pass.writes[i];
            D3D12_RESOURCE_STATES targetState = pass.writeStates[i];
            if (resources[h.id].currentState != targetState) {
                barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
                    resources[h.id].resource.Get(),
                    resources[h.id].currentState,
                    targetState
                ));
                resources[h.id].currentState = targetState;
            }
        }

        if (!barriers.empty()) {
            cmdList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        }

        // Execute pass
        BuilderImpl builder(*this, pass);
        pass.execute(cmdList, builder);
    }
}

// BuilderImpl implementation
ResourceHandle RenderGraph::BuilderImpl::createTexture(const std::string& name, const TextureDesc& desc) {
    // Simple: search for existing internal resource by name
    for (uint32_t i = 0; i < graph.resources.size(); ++i) {
        if (!graph.resources[i].isExternal && graph.resources[i].name == name) {
            // Check if desc matches (simplification: assume it does or recreate if needed)
            return { i };
        }
    }

    ResourceRecord record;
    record.name = name;
    record.isExternal = false;
    
    D3D12_RESOURCE_DESC d3dDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        desc.format, desc.width, desc.height, 1, desc.mipLevels, 1, 0, desc.flags
    );
    record.desc = d3dDesc;
    record.currentState = D3D12_RESOURCE_STATE_COMMON;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_CLEAR_VALUE* clearValPtr = desc.useClearValue ? const_cast<D3D12_CLEAR_VALUE*>(&desc.clearValue) : nullptr;

    graph.device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &d3dDesc,
        record.currentState,
        clearValPtr,
        IID_PPV_ARGS(&record.resource)
    );

    ResourceHandle handle = graph.getNextHandle();
    graph.resources.push_back(record);
    return handle;
}

void RenderGraph::BuilderImpl::writeRenderTarget(ResourceHandle handle, D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    pass.writes.push_back(handle);
    pass.writeStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void RenderGraph::BuilderImpl::writeDepthStencil(ResourceHandle handle, D3D12_CPU_DESCRIPTOR_HANDLE dsv) {
    pass.writes.push_back(handle);
    pass.writeStates.push_back(D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void RenderGraph::BuilderImpl::readTexture(ResourceHandle handle, D3D12_RESOURCE_STATES state) {
    pass.reads.push_back(handle);
    pass.readStates.push_back(state);
}

ID3D12Resource* RenderGraph::BuilderImpl::getResource(ResourceHandle handle) {
    return graph.resources[handle.id].resource.Get();
}

} // namespace rg
