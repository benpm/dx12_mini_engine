module;

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

export module render_graph;

import common;
export import gfx;

namespace rg
{

    export struct ResourceHandle
    {
        uint32_t id = 0xFFFFFFFF;
        bool isValid() const { return id != 0xFFFFFFFF; }
        bool operator==(const ResourceHandle& other) const { return id == other.id; }
        bool operator<(const ResourceHandle& other) const { return id < other.id; }
    };

    export enum class ResourceType { Texture2D, Buffer };

    export struct TextureDesc
    {
        gfx::Format format = gfx::Format::RGBA8Unorm;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipLevels = 1;
        gfx::TextureUsage usage =
            gfx::TextureUsage::RenderTarget | gfx::TextureUsage::ShaderResource;
        float clearColor[4] = { 0.f, 0.f, 0.f, 0.f };
        float clearDepth = 1.0f;
        uint8_t clearStencil = 0;
        bool useClearValue = false;
        gfx::ResourceState initialState = gfx::ResourceState::RenderTarget;
    };

    export class RenderGraphBuilder
    {
       public:
        virtual ~RenderGraphBuilder() = default;
        virtual ResourceHandle createTexture(const std::string& name, const TextureDesc& desc) = 0;
        virtual void writeRenderTarget(ResourceHandle handle, uint32_t arraySlice = 0) = 0;
        virtual void writeDepthStencil(ResourceHandle handle, uint32_t arraySlice = 0) = 0;
        virtual void readTexture(
            ResourceHandle handle,
            gfx::ResourceState state = gfx::ResourceState::PixelShaderResource
        ) = 0;
        virtual gfx::TextureHandle getTexture(ResourceHandle handle) = 0;
    };

    // Pass execute callback. The first argument is a backend-agnostic
    // gfx::ICommandList wrapper around the native list executing the graph;
    // pass `cmd.nativeHandle()` for D3D12-direct work that hasn't been
    // migrated yet.
    export using PassExecuteCallback = std::function<void(gfx::ICommandList&, RenderGraphBuilder&)>;

    export class RenderGraph
    {
       public:
        RenderGraph(gfx::IDevice& device);
        ~RenderGraph();

        void reset();

        struct PassBuilder
        {
            std::string name;
            std::vector<ResourceHandle> inputs;
            std::vector<ResourceHandle> outputs;
            std::vector<gfx::ResourceState> inputStates;
            std::vector<gfx::ResourceState> outputStates;
            PassExecuteCallback execute;
        };

        void addPass(
            const std::string& name,
            std::function<void(RenderGraphBuilder&)> setup,
            PassExecuteCallback execute
        );

        void execute(gfx::ICommandList& cmd);

        // External resources (like backbuffer, main depth)
        ResourceHandle importTexture(
            const std::string& name,
            gfx::TextureHandle handle,
            gfx::ResourceState initialState
        );

       private:
        struct ResourceRecord
        {
            std::string name;
            gfx::TextureHandle gfxHandle{};  // valid for both imported and created resources
            gfx::ResourceState currentState{};
            bool isExternal = false;
        };

        struct PassRecord
        {
            std::string name;
            std::vector<ResourceHandle> reads;
            std::vector<ResourceHandle> writes;
            std::vector<gfx::ResourceState> readStates;
            std::vector<gfx::ResourceState> writeStates;
            PassExecuteCallback execute;
        };

        class BuilderImpl : public RenderGraphBuilder
        {
           public:
            BuilderImpl(RenderGraph& graph, PassRecord& pass) : graph(graph), pass(pass) {}
            ResourceHandle createTexture(const std::string& name, const TextureDesc& desc) override;
            void writeRenderTarget(ResourceHandle handle, uint32_t arraySlice = 0) override;
            void writeDepthStencil(ResourceHandle handle, uint32_t arraySlice = 0) override;
            void readTexture(ResourceHandle handle, gfx::ResourceState state) override;
            gfx::TextureHandle getTexture(ResourceHandle handle) override;

           private:
            RenderGraph& graph;
            PassRecord& pass;
        };

        gfx::IDevice* device;
        std::vector<ResourceRecord> resources;
        std::vector<PassRecord> passes;
        std::map<std::string, ResourceHandle> externalResources;

        ResourceHandle getNextHandle() { return { static_cast<uint32_t>(resources.size()) }; }
    };

}  // namespace rg
