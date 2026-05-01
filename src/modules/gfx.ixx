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
using ::gfx::Viewport;
using ::gfx::any;

using ::gfx::ICommandList;
using ::gfx::IDevice;
using ::gfx::IQueue;
using ::gfx::ISwapChain;

using ::gfx::createDevice;

}  // export namespace gfx
