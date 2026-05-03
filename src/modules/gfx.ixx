// gfx module — thin re-export wrapper around the public header `gfx.h`.
//
// All interface and POD types live in include/gfx.h and include/gfx_types.h
// so non-module translation units (the D3D12 backend split into multiple
// .cpp files, plus tests) can include them directly without going through
// the module system. The module exists for clients of `import gfx;`.

module;

#include "gfx.h"

export module gfx;

export namespace gfx
{

    using ::gfx::AccelStructHandle;
    using ::gfx::AddressMode;
    using ::gfx::any;
    using ::gfx::BackendKind;
    using ::gfx::BlendFactor;
    using ::gfx::BlendOp;
    using ::gfx::BlendState;
    using ::gfx::BufferDesc;
    using ::gfx::BufferHandle;
    using ::gfx::BufferUsage;
    using ::gfx::Capabilities;
    using ::gfx::ColorAttachment;
    using ::gfx::CompareOp;
    using ::gfx::ComputePipelineDesc;
    using ::gfx::CullMode;
    using ::gfx::DepthAttachment;
    using ::gfx::DepthStencilState;
    using ::gfx::DeviceDesc;
    using ::gfx::FenceValue;
    using ::gfx::FillMode;
    using ::gfx::FilterMode;
    using ::gfx::Format;
    using ::gfx::GraphicsPipelineDesc;
    using ::gfx::IndexBufferView;
    using ::gfx::IndexFormat;
    using ::gfx::LoadOp;
    using ::gfx::PipelineHandle;
    using ::gfx::PrimitiveTopology;
    using ::gfx::RasterizerState;
    using ::gfx::RenderPassDesc;
    using ::gfx::ResourceState;
    using ::gfx::SamplerDesc;
    using ::gfx::SamplerHandle;
    using ::gfx::ScissorRect;
    using ::gfx::ShaderBytecode;
    using ::gfx::ShaderDesc;
    using ::gfx::ShaderHandle;
    using ::gfx::ShaderStage;
    using ::gfx::StencilOp;
    using ::gfx::StoreOp;
    using ::gfx::SwapChainDesc;
    using ::gfx::TextureDesc;
    using ::gfx::TextureHandle;
    using ::gfx::TextureUsage;
    using ::gfx::VertexAttribute;
    using ::gfx::VertexBufferView;
    using ::gfx::Viewport;

    using ::gfx::operator|;

    using ::gfx::ICommandList;
    using ::gfx::IDevice;
    using ::gfx::IQueue;
    using ::gfx::ISwapChain;

    using ::gfx::createDevice;
    using ::gfx::wrapNativeCommandList;

}  // export namespace gfx

export struct BindlessIndices
{
    uint32_t drawDataIdx;
    uint32_t shadowMapIdx;
    uint32_t envMapIdx;
    uint32_t ssaoIdx;
    uint32_t shadowSamplerIdx;
    uint32_t envSamplerIdx;
    uint32_t drawIndex;
    uint32_t miscIdx;  // Can be used for source texture in post-fx
    uint32_t _pad[8];  // Reserved for 64-uint root constant block (256 bytes)
};

export namespace app_slots
{
    // Legacy root signature slot assignments (8-param layout, VOLATILE descriptor tables)
    inline constexpr uint32_t rootPerFrameCB = 0;
    inline constexpr uint32_t rootPerPassCB = 1;
    inline constexpr uint32_t rootDrawIndex = 2;
    inline constexpr uint32_t rootOutlineParams = 3;
    inline constexpr uint32_t rootPerObjectSrv = 4;  // SRV for per-object StructuredBuffer
    inline constexpr uint32_t rootShadowSrv = 5;
    inline constexpr uint32_t rootCubemapSrv = 6;
    inline constexpr uint32_t rootSsaoSrv = 7;

    // Bindless root signature slot assignments (5-param layout)
    inline constexpr uint32_t bindlessIndices = 0;       // 64 root constants (b0)
    inline constexpr uint32_t bindlessPerFrameCB = 1;    // CBV (b1)
    inline constexpr uint32_t bindlessPerPassCB = 2;     // CBV (b2)
    inline constexpr uint32_t bindlessSrvTable = 3;      // SRV table (t0+, space0,1,2,3)
    inline constexpr uint32_t bindlessSamplerTable = 4;  // Sampler table (s0+, space0,1)
}  // namespace app_slots
