module;

#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include "d3dx12_clean.h"

export module render_graph;

import common;

namespace rg {

export struct ResourceHandle {
    uint32_t id = 0xFFFFFFFF;
    bool isValid() const { return id != 0xFFFFFFFF; }
    bool operator==(const ResourceHandle& other) const { return id == other.id; }
    bool operator<(const ResourceHandle& other) const { return id < other.id; }
};

export enum class ResourceType {
    Texture2D,
    Buffer
};

export struct TextureDesc {
    DXGI_FORMAT format;
    uint32_t width;
    uint32_t height;
    uint32_t mipLevels = 1;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_CLEAR_VALUE clearValue = {};
    bool useClearValue = false;
};

export class RenderGraphBuilder {
public:
    virtual ~RenderGraphBuilder() = default;
    virtual ResourceHandle createTexture(const std::string& name, const TextureDesc& desc) = 0;
    virtual void writeRenderTarget(ResourceHandle handle, D3D12_CPU_DESCRIPTOR_HANDLE rtv) = 0;
    virtual void writeDepthStencil(ResourceHandle handle, D3D12_CPU_DESCRIPTOR_HANDLE dsv) = 0;
    virtual void readTexture(ResourceHandle handle, D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) = 0;
    virtual ID3D12Resource* getResource(ResourceHandle handle) = 0;
};

export using PassExecuteCallback = std::function<void(ID3D12GraphicsCommandList2*, RenderGraphBuilder&)>;

export class RenderGraph {
public:
    RenderGraph(ID3D12Device2* device);
    ~RenderGraph();

    void reset();
    
    struct PassBuilder {
        std::string name;
        std::vector<ResourceHandle> inputs;
        std::vector<ResourceHandle> outputs;
        std::vector<D3D12_RESOURCE_STATES> inputStates;
        std::vector<D3D12_RESOURCE_STATES> outputStates;
        PassExecuteCallback execute;
    };

    void addPass(const std::string& name, std::function<void(RenderGraphBuilder&)> setup, PassExecuteCallback execute);

    void execute(ID3D12GraphicsCommandList2* cmdList);

    // External resources (like backbuffer, main depth)
    ResourceHandle importTexture(const std::string& name, ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState);

private:
    struct ResourceRecord {
        std::string name;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_DESC desc;
        D3D12_RESOURCE_STATES currentState;
        bool isExternal = false;
    };

    struct PassRecord {
        std::string name;
        std::vector<ResourceHandle> reads;
        std::vector<ResourceHandle> writes;
        std::vector<D3D12_RESOURCE_STATES> readStates;
        std::vector<D3D12_RESOURCE_STATES> writeStates;
        PassExecuteCallback execute;
    };

    class BuilderImpl : public RenderGraphBuilder {
    public:
        BuilderImpl(RenderGraph& graph, PassRecord& pass) : graph(graph), pass(pass) {}
        ResourceHandle createTexture(const std::string& name, const TextureDesc& desc) override;
        void writeRenderTarget(ResourceHandle handle, D3D12_CPU_DESCRIPTOR_HANDLE rtv) override;
        void writeDepthStencil(ResourceHandle handle, D3D12_CPU_DESCRIPTOR_HANDLE dsv) override;
        void readTexture(ResourceHandle handle, D3D12_RESOURCE_STATES state) override;
        ID3D12Resource* getResource(ResourceHandle handle) override;

    private:
        RenderGraph& graph;
        PassRecord& pass;
    };

    ID3D12Device2* device;
    std::vector<ResourceRecord> resources;
    std::vector<PassRecord> passes;
    std::map<std::string, ResourceHandle> externalResources;

    ResourceHandle getNextHandle() { return { static_cast<uint32_t>(resources.size()) }; }
};

} // namespace rg
