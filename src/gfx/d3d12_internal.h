// Internal header shared by the D3D12 backend implementation files.
//
// This header is intentionally NOT under include/ — it is private to
// src/gfx/*.cpp. It declares the backend-internal classes (Device, Queue,
// CommandList, SwapChain) and the format / state conversion helpers that
// translate between gfx:: enums and D3D12 enums.

#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "d3dx12_clean.h"
#include "gfx.h"
#include "gfx_types.h"

namespace gfxd3d12
{

    // ---------------------------------------------------------------------------
    // Format / state conversion helpers
    // ---------------------------------------------------------------------------

    inline DXGI_FORMAT toDXGI(gfx::Format f)
    {
        using gfx::Format;
        switch (f) {
            case Format::Unknown:
                return DXGI_FORMAT_UNKNOWN;
            case Format::R8Unorm:
                return DXGI_FORMAT_R8_UNORM;
            case Format::RG8Unorm:
                return DXGI_FORMAT_R8G8_UNORM;
            case Format::RGBA8Unorm:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case Format::RGBA8UnormSrgb:
                return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case Format::BGRA8Unorm:
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            case Format::R16Float:
                return DXGI_FORMAT_R16_FLOAT;
            case Format::RG16Float:
                return DXGI_FORMAT_R16G16_FLOAT;
            case Format::RGBA16Float:
                return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case Format::R32Float:
                return DXGI_FORMAT_R32_FLOAT;
            case Format::RG32Float:
                return DXGI_FORMAT_R32G32_FLOAT;
            case Format::RGB32Float:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case Format::RGBA32Float:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case Format::R32Uint:
                return DXGI_FORMAT_R32_UINT;
            case Format::RG32Uint:
                return DXGI_FORMAT_R32G32_UINT;
            case Format::RGBA32Uint:
                return DXGI_FORMAT_R32G32B32A32_UINT;
            case Format::R11G11B10Float:
                return DXGI_FORMAT_R11G11B10_FLOAT;
            case Format::RGB10A2Unorm:
                return DXGI_FORMAT_R10G10B10A2_UNORM;
            case Format::D16Unorm:
                return DXGI_FORMAT_D16_UNORM;
            case Format::D32Float:
                return DXGI_FORMAT_D32_FLOAT;
            case Format::D24UnormS8Uint:
                return DXGI_FORMAT_D24_UNORM_S8_UINT;
            case Format::D32FloatS8X24Uint:
                return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
            case Format::R32Typeless:
                return DXGI_FORMAT_R32_TYPELESS;
            case Format::R32G8X24Typeless:
                return DXGI_FORMAT_R32G8X24_TYPELESS;
        }
        return DXGI_FORMAT_UNKNOWN;
    }

    inline D3D12_RESOURCE_STATES toD3D12States(gfx::ResourceState s)
    {
        using S = gfx::ResourceState;
        auto bits = static_cast<uint32_t>(s);
        if (bits == 0) {
            return D3D12_RESOURCE_STATE_COMMON;
        }
        D3D12_RESOURCE_STATES out = D3D12_RESOURCE_STATE_COMMON;
        auto has = [&](S flag) { return (bits & static_cast<uint32_t>(flag)) != 0; };
        if (has(S::VertexBuffer) || has(S::ConstantBuffer)) {
            out |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        }
        if (has(S::IndexBuffer)) {
            out |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
        }
        if (has(S::RenderTarget)) {
            out |= D3D12_RESOURCE_STATE_RENDER_TARGET;
        }
        if (has(S::UnorderedAccess)) {
            out |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (has(S::DepthWrite)) {
            out |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }
        if (has(S::DepthRead)) {
            out |= D3D12_RESOURCE_STATE_DEPTH_READ;
        }
        if (has(S::PixelShaderResource)) {
            out |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        if (has(S::NonPixelShaderResource)) {
            out |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
        if (has(S::CopySource)) {
            out |= D3D12_RESOURCE_STATE_COPY_SOURCE;
        }
        if (has(S::CopyDest)) {
            out |= D3D12_RESOURCE_STATE_COPY_DEST;
        }
        if (has(S::Present)) {
            out |= D3D12_RESOURCE_STATE_PRESENT;
        }
        if (has(S::AccelStructRead) || has(S::AccelStructWrite)) {
            out |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        }
        return out;
    }

    inline D3D12_RESOURCE_FLAGS toD3D12ResourceFlags(gfx::TextureUsage u)
    {
        D3D12_RESOURCE_FLAGS f = D3D12_RESOURCE_FLAG_NONE;
        if (gfx::any(u, gfx::TextureUsage::RenderTarget)) {
            f |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }
        if (gfx::any(u, gfx::TextureUsage::DepthStencil)) {
            f |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        }
        if (gfx::any(u, gfx::TextureUsage::UnorderedAccess)) {
            f |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }
        if (!gfx::any(u, gfx::TextureUsage::ShaderResource) &&
            gfx::any(u, gfx::TextureUsage::DepthStencil)) {
            f |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        }
        return f;
    }

    inline D3D12_PRIMITIVE_TOPOLOGY_TYPE toTopologyType(gfx::PrimitiveTopology t)
    {
        switch (t) {
            case gfx::PrimitiveTopology::TriangleList:
            case gfx::PrimitiveTopology::TriangleStrip:
                return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            case gfx::PrimitiveTopology::LineList:
                return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            case gfx::PrimitiveTopology::PointList:
                return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        }
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }

    inline D3D12_PRIMITIVE_TOPOLOGY toTopology(gfx::PrimitiveTopology t)
    {
        switch (t) {
            case gfx::PrimitiveTopology::TriangleList:
                return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            case gfx::PrimitiveTopology::TriangleStrip:
                return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            case gfx::PrimitiveTopology::LineList:
                return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
            case gfx::PrimitiveTopology::PointList:
                return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        }
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }

    inline D3D12_COMPARISON_FUNC toCompare(gfx::CompareOp op)
    {
        switch (op) {
            case gfx::CompareOp::Never:
                return D3D12_COMPARISON_FUNC_NEVER;
            case gfx::CompareOp::Less:
                return D3D12_COMPARISON_FUNC_LESS;
            case gfx::CompareOp::Equal:
                return D3D12_COMPARISON_FUNC_EQUAL;
            case gfx::CompareOp::LessEqual:
                return D3D12_COMPARISON_FUNC_LESS_EQUAL;
            case gfx::CompareOp::Greater:
                return D3D12_COMPARISON_FUNC_GREATER;
            case gfx::CompareOp::NotEqual:
                return D3D12_COMPARISON_FUNC_NOT_EQUAL;
            case gfx::CompareOp::GreaterEqual:
                return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
            case gfx::CompareOp::Always:
                return D3D12_COMPARISON_FUNC_ALWAYS;
        }
        return D3D12_COMPARISON_FUNC_LESS;
    }

    inline D3D12_BLEND toBlend(gfx::BlendFactor f)
    {
        switch (f) {
            case gfx::BlendFactor::Zero:
                return D3D12_BLEND_ZERO;
            case gfx::BlendFactor::One:
                return D3D12_BLEND_ONE;
            case gfx::BlendFactor::SrcColor:
                return D3D12_BLEND_SRC_COLOR;
            case gfx::BlendFactor::InvSrcColor:
                return D3D12_BLEND_INV_SRC_COLOR;
            case gfx::BlendFactor::SrcAlpha:
                return D3D12_BLEND_SRC_ALPHA;
            case gfx::BlendFactor::InvSrcAlpha:
                return D3D12_BLEND_INV_SRC_ALPHA;
            case gfx::BlendFactor::DstAlpha:
                return D3D12_BLEND_DEST_ALPHA;
            case gfx::BlendFactor::InvDstAlpha:
                return D3D12_BLEND_INV_DEST_ALPHA;
            case gfx::BlendFactor::DstColor:
                return D3D12_BLEND_DEST_COLOR;
            case gfx::BlendFactor::InvDstColor:
                return D3D12_BLEND_INV_DEST_COLOR;
        }
        return D3D12_BLEND_ONE;
    }

    inline D3D12_BLEND_OP toBlendOp(gfx::BlendOp op)
    {
        switch (op) {
            case gfx::BlendOp::Add:
                return D3D12_BLEND_OP_ADD;
            case gfx::BlendOp::Subtract:
                return D3D12_BLEND_OP_SUBTRACT;
            case gfx::BlendOp::ReverseSubtract:
                return D3D12_BLEND_OP_REV_SUBTRACT;
            case gfx::BlendOp::Min:
                return D3D12_BLEND_OP_MIN;
            case gfx::BlendOp::Max:
                return D3D12_BLEND_OP_MAX;
        }
        return D3D12_BLEND_OP_ADD;
    }

    inline D3D12_CULL_MODE toCull(gfx::CullMode m)
    {
        switch (m) {
            case gfx::CullMode::None:
                return D3D12_CULL_MODE_NONE;
            case gfx::CullMode::Front:
                return D3D12_CULL_MODE_FRONT;
            case gfx::CullMode::Back:
                return D3D12_CULL_MODE_BACK;
        }
        return D3D12_CULL_MODE_BACK;
    }

    // ---------------------------------------------------------------------------
    // BindlessHeap — simple thread-safe slot allocator over an ID3D12DescriptorHeap.
    // ---------------------------------------------------------------------------

    class BindlessHeap
    {
       public:
        void init(
            ID3D12Device2* device,
            uint32_t capacity,
            D3D12_DESCRIPTOR_HEAP_TYPE type,
            bool shaderVisible,
            const wchar_t* name
        );

        uint32_t allocate();
        void free(uint32_t index);

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle(uint32_t index) const;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle(uint32_t index) const;
        ID3D12DescriptorHeap* native() const { return heap.Get(); }
        uint32_t capacity() const { return capacity_; }

       private:
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuStart{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuStart{};
        uint32_t descSize = 0;
        uint32_t capacity_ = 0;
        std::atomic<uint32_t> next{ 0 };
        std::mutex mu;
        std::vector<uint32_t> freeList;
    };

    // ---------------------------------------------------------------------------
    // Per-handle records (object pool entries owned by Device).
    // ---------------------------------------------------------------------------

    struct TextureRecord
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        gfx::TextureDesc desc;
        D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
        uint32_t srvIndex = 0;
        uint32_t uavIndex = 0;
        bool external = false;
    };

    struct BufferRecord
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        gfx::BufferDesc desc;
        D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
        void* mapped = nullptr;
        uint32_t srvIndex = 0;
        uint32_t uavIndex = 0;
    };

    struct ShaderRecord
    {
        std::vector<uint8_t> bytecode;
        gfx::ShaderStage stage = gfx::ShaderStage::Vertex;
    };

    struct PipelineRecord
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig;
        bool isCompute = false;
        D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    };

    struct SamplerRecord
    {
        uint32_t index = 0;
        gfx::SamplerDesc desc;
    };

    class Device;

    // ---------------------------------------------------------------------------
    // CommandList
    // ---------------------------------------------------------------------------

    class CommandList final : public gfx::ICommandList
    {
       public:
        CommandList(Device* dev, ID3D12GraphicsCommandList2* cmd) : device(dev), cmd(cmd) {}

        void begin() override;
        void end() override;
        void beginRenderPass(const gfx::RenderPassDesc& d) override;
        void endRenderPass() override {}
        void setViewport(const gfx::Viewport& vp) override;
        void setScissor(const gfx::ScissorRect& sr) override;
        void bindPipeline(gfx::PipelineHandle p) override;
        void setRootConstants(uint32_t slot, const void* data, uint32_t numDwords) override;
        void setConstantBuffer(uint32_t slot, gfx::BufferHandle b, uint64_t offset) override;
        void setVertexBuffer(
            uint32_t slot,
            gfx::BufferHandle b,
            uint32_t stride,
            uint64_t offset
        ) override;
        void setIndexBuffer(gfx::BufferHandle b, gfx::IndexFormat fmt, uint64_t offset) override;
        void draw(uint32_t v, uint32_t i, uint32_t fv, uint32_t fi) override;
        void
        drawIndexed(uint32_t ic, uint32_t inst, uint32_t fi, int32_t bv, uint32_t fInst) override;
        void dispatch(uint32_t x, uint32_t y, uint32_t z) override;
        void
        barrier(gfx::TextureHandle h, gfx::ResourceState before, gfx::ResourceState after) override;
        void
        barrier(gfx::BufferHandle h, gfx::ResourceState before, gfx::ResourceState after) override;
        void uavBarrier(gfx::TextureHandle h) override;
        void copyBuffer(
            gfx::BufferHandle dst,
            uint64_t dOff,
            gfx::BufferHandle src,
            uint64_t sOff,
            uint64_t size
        ) override;
        void copyTextureToBuffer(
            gfx::BufferHandle dst,
            uint64_t dOff,
            gfx::TextureHandle src,
            uint32_t srcMip,
            uint32_t srcSlice,
            int32_t x,
            int32_t y,
            uint32_t w,
            uint32_t h
        ) override;
        void buildAccelStruct(gfx::AccelStructHandle) override
        {
            throw std::runtime_error("buildAccelStruct: RT not yet implemented");
        }
        void traceRays(uint32_t, uint32_t, uint32_t) override
        {
            throw std::runtime_error("traceRays: RT not yet implemented");
        }
        void beginGpuZone(const char*) override {}
        void endGpuZone() override {}
        void* nativeHandle() override { return cmd; }

        ID3D12GraphicsCommandList2* native() const { return cmd; }

       private:
        Device* device;
        ID3D12GraphicsCommandList2* cmd;
    };

    // ---------------------------------------------------------------------------
    // Queue
    // ---------------------------------------------------------------------------

    class Queue final : public gfx::IQueue
    {
       public:
        Queue(Device* dev, ID3D12Device2* d3dDev, D3D12_COMMAND_LIST_TYPE type);
        ~Queue() override;

        gfx::ICommandList* acquireCommandList() override;
        gfx::FenceValue submit(gfx::ICommandList* list) override;
        gfx::FenceValue signal() override;
        bool isFenceComplete(gfx::FenceValue fv) const override;
        gfx::FenceValue completedFenceValue() const override;
        void waitForFence(gfx::FenceValue fv) override;
        void flush() override;
        void* nativeHandle() override { return queue.Get(); }

        ID3D12CommandQueue* native() const { return queue.Get(); }

       private:
        Device* device;
        Microsoft::WRL::ComPtr<ID3D12Device2> d3dDevice;
        D3D12_COMMAND_LIST_TYPE type;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        HANDLE fenceEvent = nullptr;
        uint64_t fenceValue = 0;

        struct AllocEntry
        {
            uint64_t fenceValue;
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc;
        };
        std::deque<AllocEntry> allocPool;
        std::deque<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2>> listPool;
        std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2>> liveLists;
        std::unordered_map<
            ID3D12GraphicsCommandList2*,
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator>>
            listAllocs;
        std::vector<std::unique_ptr<CommandList>> wrappers;

        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> getAllocator();
    };

    // ---------------------------------------------------------------------------
    // SwapChain
    // ---------------------------------------------------------------------------

    class SwapChain final : public gfx::ISwapChain
    {
       public:
        SwapChain(Device* dev, const gfx::SwapChainDesc& desc);
        ~SwapChain() override;

        void resize(uint32_t w, uint32_t h) override;
        gfx::TextureHandle currentBackBuffer() override;
        uint32_t currentIndex() const override;
        uint32_t bufferCount() const override { return bufferCount_; }
        uint32_t width() const override { return width_; }
        uint32_t height() const override { return height_; }
        void present(bool vsync) override;
        gfx::Format backBufferFormat() const override { return format_; }
        void* nativeHandle() override { return swap.Get(); }

       private:
        void createBackBufferTextures();
        void releaseBackBufferTextures();

        Device* device;
        Microsoft::WRL::ComPtr<IDXGISwapChain4> swap;
        HWND hwnd_;
        uint32_t width_ = 0;
        uint32_t height_ = 0;
        uint32_t bufferCount_ = 0;
        gfx::Format format_ = gfx::Format::RGBA8Unorm;
        bool allowTearing_ = false;
        std::vector<gfx::TextureHandle> backBufferHandles;
    };

    // ---------------------------------------------------------------------------
    // Device
    // ---------------------------------------------------------------------------

    class Device final : public gfx::IDevice
    {
       public:
        Device(const gfx::DeviceDesc& desc);
        ~Device() override;

        gfx::BackendKind backend() const override { return gfx::BackendKind::D3D12; }
        gfx::Capabilities caps() const override { return capabilities; }

        gfx::TextureHandle createTexture(const gfx::TextureDesc& desc) override;
        gfx::BufferHandle createBuffer(const gfx::BufferDesc& desc) override;
        gfx::ShaderHandle createShader(const gfx::ShaderDesc& desc) override;
        gfx::PipelineHandle createGraphicsPipeline(const gfx::GraphicsPipelineDesc& desc) override;
        gfx::PipelineHandle createComputePipeline(const gfx::ComputePipelineDesc& desc) override;
        gfx::SamplerHandle createSampler(const gfx::SamplerDesc& desc) override;
        gfx::AccelStructHandle createAccelStruct(uint64_t sizeBytes) override;

        void destroy(gfx::TextureHandle h) override;
        void destroy(gfx::BufferHandle h) override;
        void destroy(gfx::ShaderHandle h) override;
        void destroy(gfx::PipelineHandle h) override;
        void destroy(gfx::SamplerHandle h) override;
        void destroy(gfx::AccelStructHandle h) override;

        void* map(gfx::BufferHandle h) override;
        void unmap(gfx::BufferHandle h) override;
        void uploadBuffer(
            gfx::BufferHandle dst,
            const void* data,
            uint64_t size,
            uint64_t dstOffset
        ) override;
        void uploadTexture(
            gfx::TextureHandle dst,
            const void* data,
            uint64_t rowPitch,
            uint64_t slicePitch
        ) override;

        uint32_t bindlessSrvIndex(gfx::TextureHandle h) override;
        uint32_t bindlessSrvIndex(gfx::BufferHandle h) override;
        uint32_t bindlessUavIndex(gfx::TextureHandle h) override;
        uint32_t bindlessUavIndex(gfx::BufferHandle h) override;
        uint32_t bindlessSamplerIndex(gfx::SamplerHandle h) override;

        gfx::IQueue* graphicsQueue() override { return queue.get(); }
        std::unique_ptr<gfx::ISwapChain> createSwapChain(const gfx::SwapChainDesc& desc) override;
        void retireCompletedResources() override;
        void* nativeHandle() override { return d3dDevice.Get(); }
        void* nativeResource(gfx::TextureHandle h) override
        {
            auto* r = getTexture(h);
            return r ? r->resource.Get() : nullptr;
        }
        void* nativeResource(gfx::BufferHandle h) override
        {
            auto* r = getBuffer(h);
            return r ? r->resource.Get() : nullptr;
        }

        // Internal accessors used by Queue/CommandList/SwapChain.
        ID3D12Device2* d3d() { return d3dDevice.Get(); }
        IDXGIFactory6* dxgiFactory() { return factory.Get(); }
        BindlessHeap& srvHeap() { return resourceHeap; }
        BindlessHeap& samplerHeapRef() { return samplerHeap_; }
        ID3D12RootSignature* bindlessRootSig() const { return rootSignature.Get(); }

        TextureRecord* getTexture(gfx::TextureHandle h);
        BufferRecord* getBuffer(gfx::BufferHandle h);
        ShaderRecord* getShader(gfx::ShaderHandle h);
        PipelineRecord* getPipeline(gfx::PipelineHandle h);
        SamplerRecord* getSampler(gfx::SamplerHandle h);

        gfx::TextureHandle
        adoptBackBuffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource, gfx::Format format);
        void releaseBackBuffer(gfx::TextureHandle h);

        void schedulePendingDestroy(
            uint64_t fenceValue,
            Microsoft::WRL::ComPtr<ID3D12Resource> resource
        );

       private:
        void createDeviceAndFactory();
        void createBindlessRootSignature();
        void initBindlessHeaps(uint32_t capacity);
        void writeSrvForTexture(TextureRecord& tex);

        gfx::DeviceDesc desc;
        gfx::Capabilities capabilities;
        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        Microsoft::WRL::ComPtr<ID3D12Device2> d3dDevice;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;

        std::unique_ptr<Queue> queue;
        BindlessHeap resourceHeap;
        BindlessHeap samplerHeap_;

        std::vector<TextureRecord> textures{ TextureRecord{} };
        std::vector<BufferRecord> buffers{ BufferRecord{} };
        std::vector<ShaderRecord> shaders{ ShaderRecord{} };
        std::vector<PipelineRecord> pipelines{ PipelineRecord{} };
        std::vector<SamplerRecord> samplers{ SamplerRecord{} };

        std::vector<uint32_t> freeTextures, freeBuffers, freeShaders, freePipelines, freeSamplers;

        struct PendingDestroy
        {
            uint64_t fenceValue;
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        };
        std::deque<PendingDestroy> pendingDestroys;

        std::mutex poolMu;
    };

}  // namespace gfxd3d12
