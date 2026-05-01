// Public header for the gfx graphics abstraction layer.
//
// This header declares the abstract interfaces (IDevice, ICommandList, IQueue,
// ISwapChain) and the factory entry point (gfx::createDevice). It can be
// included from either module or non-module translation units. The same types
// are also surfaced through `import gfx;` (see src/modules/gfx.ixx), which
// merely re-exports the names declared here via using-declarations.
//
// Including this header from a module's global fragment is fine — these are
// ordinary global-namespace types, not module-attached.

#pragma once

#include <cstdint>
#include <memory>

#include "gfx_types.h"

namespace gfx
{

class IDevice;
class ICommandList;
class IQueue;
class ISwapChain;

// ----------------------------------------------------------------------------
// ICommandList — recorded once per frame, submitted to a queue. Render-graph
// passes record into one of these. The exact backend type is hidden behind
// this interface.
// ----------------------------------------------------------------------------
class ICommandList
{
   public:
    virtual ~ICommandList() = default;

    virtual void begin() = 0;
    virtual void end() = 0;

    virtual void beginRenderPass(const RenderPassDesc& desc) = 0;
    virtual void endRenderPass() = 0;

    virtual void setViewport(const Viewport& vp) = 0;
    virtual void setScissor(const ScissorRect& sr) = 0;

    virtual void bindPipeline(PipelineHandle pipeline) = 0;

    // Bindless: small typed payload uploaded as root constants / push constants.
    virtual void setRootConstants(uint32_t slot, const void* data, uint32_t numDwords) = 0;

    // Per-frame and per-pass CBVs (root descriptors in D3D12, descriptor sets
    // in Vulkan).
    virtual void setConstantBuffer(uint32_t slot, BufferHandle buffer, uint64_t offset = 0) = 0;

    virtual void setVertexBuffer(uint32_t slot, BufferHandle buffer, uint32_t stride,
                                 uint64_t offset = 0) = 0;
    virtual void setIndexBuffer(BufferHandle buffer, IndexFormat fmt, uint64_t offset = 0) = 0;

    virtual void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                      uint32_t firstInstance) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                             int32_t baseVertex, uint32_t firstInstance) = 0;
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;

    // Explicit barrier — render graph generates these. Subsystems should not
    // call directly.
    virtual void barrier(TextureHandle texture, ResourceState before, ResourceState after) = 0;
    virtual void barrier(BufferHandle buffer, ResourceState before, ResourceState after) = 0;
    virtual void uavBarrier(TextureHandle texture) = 0;

    virtual void copyBuffer(BufferHandle dst, uint64_t dstOffset, BufferHandle src,
                            uint64_t srcOffset, uint64_t size) = 0;
    virtual void copyTextureToBuffer(BufferHandle dst, uint64_t dstOffset, TextureHandle src,
                                     uint32_t srcMip, uint32_t srcArraySlice, int32_t srcX,
                                     int32_t srcY, uint32_t width, uint32_t height) = 0;

    // Optional (only valid when Capabilities::raytracing is true on the owning
    // device).
    virtual void buildAccelStruct(AccelStructHandle accel) = 0;
    virtual void traceRays(uint32_t width, uint32_t height, uint32_t depth) = 0;

    // GPU profiler zones (Tracy on D3D12, no-op elsewhere by default).
    virtual void beginGpuZone(const char* name) = 0;
    virtual void endGpuZone() = 0;

    // Escape hatch for incremental migration. Returns the underlying backend
    // command-list pointer (ID3D12GraphicsCommandList* on D3D12, etc.). To be
    // removed once subsystem migration completes (P14).
    virtual void* nativeHandle() = 0;
};

// ----------------------------------------------------------------------------
// IQueue — submission and fence sync.
// ----------------------------------------------------------------------------
class IQueue
{
   public:
    virtual ~IQueue() = default;

    virtual ICommandList* acquireCommandList() = 0;
    virtual FenceValue submit(ICommandList* list) = 0;

    virtual FenceValue signal() = 0;
    virtual bool isFenceComplete(FenceValue fv) const = 0;
    virtual FenceValue completedFenceValue() const = 0;
    virtual void waitForFence(FenceValue fv) = 0;
    virtual void flush() = 0;

    virtual void* nativeHandle() = 0;
};

// ----------------------------------------------------------------------------
// ISwapChain — backbuffer rotation + present.
// ----------------------------------------------------------------------------
class ISwapChain
{
   public:
    virtual ~ISwapChain() = default;

    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual TextureHandle currentBackBuffer() = 0;
    virtual uint32_t currentIndex() const = 0;
    virtual uint32_t bufferCount() const = 0;
    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;
    virtual void present(bool vsync) = 0;
    virtual Format backBufferFormat() const = 0;
    virtual void* nativeHandle() = 0;
};

// ----------------------------------------------------------------------------
// IDevice — root object. Owns resources, the bindless heap, and the queue.
// ----------------------------------------------------------------------------
class IDevice
{
   public:
    virtual ~IDevice() = default;

    virtual BackendKind backend() const = 0;
    virtual Capabilities caps() const = 0;

    virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
    virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
    virtual PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual PipelineHandle createComputePipeline(const ComputePipelineDesc& desc) = 0;
    virtual SamplerHandle createSampler(const SamplerDesc& desc) = 0;

    virtual AccelStructHandle createAccelStruct(uint64_t sizeBytes) = 0;

    virtual void destroy(TextureHandle h) = 0;
    virtual void destroy(BufferHandle h) = 0;
    virtual void destroy(ShaderHandle h) = 0;
    virtual void destroy(PipelineHandle h) = 0;
    virtual void destroy(SamplerHandle h) = 0;
    virtual void destroy(AccelStructHandle h) = 0;

    virtual void* map(BufferHandle h) = 0;
    virtual void unmap(BufferHandle h) = 0;
    virtual void uploadBuffer(BufferHandle dst, const void* data, uint64_t size,
                              uint64_t dstOffset = 0) = 0;
    virtual void uploadTexture(TextureHandle dst, const void* data, uint64_t rowPitch,
                               uint64_t slicePitch) = 0;

    virtual uint32_t bindlessSrvIndex(TextureHandle h) = 0;
    virtual uint32_t bindlessSrvIndex(BufferHandle h) = 0;
    virtual uint32_t bindlessUavIndex(TextureHandle h) = 0;
    virtual uint32_t bindlessUavIndex(BufferHandle h) = 0;
    virtual uint32_t bindlessSamplerIndex(SamplerHandle h) = 0;

    virtual IQueue* graphicsQueue() = 0;
    virtual std::unique_ptr<ISwapChain> createSwapChain(const SwapChainDesc& desc) = 0;
    virtual void retireCompletedResources() = 0;

    virtual void* nativeHandle() = 0;
};

// Factory entry point. Returns the D3D12 backend on Windows. The Vulkan/Metal
// variants will live in their own implementation TUs and a future overload of
// this factory will switch on BackendKind.
std::unique_ptr<IDevice> createDevice(BackendKind kind, const DeviceDesc& desc);

// Wrap an externally-owned native command list (e.g. one allocated by the
// legacy CommandQueue) in a gfx::ICommandList interface. The wrapper does NOT
// take ownership of the native list — the caller remains responsible for its
// lifetime, reset(), Close(), and submission. Used during incremental
// migration so the render graph and subsystems can speak gfx::ICommandList
// without the engine first dissolving CommandQueue. Removed once subsystem
// migration completes.
//
// `nativeList` is an `ID3D12GraphicsCommandList2*` for the D3D12 backend.
std::unique_ptr<ICommandList> wrapNativeCommandList(IDevice* dev, void* nativeList);

}  // namespace gfx
