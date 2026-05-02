#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace gfx
{

    struct TextureHandle
    {
        uint32_t id = 0;
        bool isValid() const { return id != 0; }
        bool operator==(TextureHandle o) const { return id == o.id; }
    };

    struct BufferHandle
    {
        uint32_t id = 0;
        bool isValid() const { return id != 0; }
        bool operator==(BufferHandle o) const { return id == o.id; }
    };

    struct PipelineHandle
    {
        uint32_t id = 0;
        bool isValid() const { return id != 0; }
        bool operator==(PipelineHandle o) const { return id == o.id; }
    };

    struct ShaderHandle
    {
        uint32_t id = 0;
        bool isValid() const { return id != 0; }
        bool operator==(ShaderHandle o) const { return id == o.id; }
    };

    struct SamplerHandle
    {
        uint32_t id = 0;
        bool isValid() const { return id != 0; }
        bool operator==(SamplerHandle o) const { return id == o.id; }
    };

    struct AccelStructHandle
    {
        uint32_t id = 0;
        bool isValid() const { return id != 0; }
        bool operator==(AccelStructHandle o) const { return id == o.id; }
    };

    struct FenceValue
    {
        uint64_t value = 0;
        bool isValid() const { return value != 0; }
    };

    enum class Format : uint16_t
    {
        Unknown = 0,
        R8Unorm,
        RG8Unorm,
        RGBA8Unorm,
        RGBA8UnormSrgb,
        BGRA8Unorm,
        R16Float,
        RG16Float,
        RGBA16Float,
        R32Float,
        RG32Float,
        RGB32Float,
        RGBA32Float,
        R32Uint,
        RG32Uint,
        RGBA32Uint,
        R11G11B10Float,
        RGB10A2Unorm,
        D16Unorm,
        D32Float,
        D24UnormS8Uint,
        D32FloatS8X24Uint,
        // Typeless variants for depth/stencil resources whose typed view differs
        // from the underlying resource format (D3D12 idiom).
        R32Typeless,
        R32G8X24Typeless,
        // SRV-only format for reading the depth plane of a D32_FLOAT_S8X24_UINT resource.
        R32FloatX8X24Typeless,
    };

    enum class ResourceState : uint32_t
    {
        Common = 0,
        VertexBuffer = 1 << 0,
        ConstantBuffer = 1 << 1,
        IndexBuffer = 1 << 2,
        RenderTarget = 1 << 3,
        UnorderedAccess = 1 << 4,
        DepthWrite = 1 << 5,
        DepthRead = 1 << 6,
        PixelShaderResource = 1 << 7,
        NonPixelShaderResource = 1 << 8,
        ShaderResource = PixelShaderResource | NonPixelShaderResource,
        CopySource = 1 << 9,
        CopyDest = 1 << 10,
        Present = 1 << 11,
        AccelStructRead = 1 << 12,
        AccelStructWrite = 1 << 13,
    };

    inline ResourceState operator|(ResourceState a, ResourceState b)
    {
        return static_cast<ResourceState>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    enum class TextureUsage : uint32_t
    {
        None = 0,
        RenderTarget = 1 << 0,
        DepthStencil = 1 << 1,
        UnorderedAccess = 1 << 2,
        ShaderResource = 1 << 3,
    };

    inline TextureUsage operator|(TextureUsage a, TextureUsage b)
    {
        return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline bool any(TextureUsage v, TextureUsage flag)
    {
        return (static_cast<uint32_t>(v) & static_cast<uint32_t>(flag)) != 0;
    }

    enum class BufferUsage : uint32_t
    {
        None = 0,
        Vertex = 1 << 0,
        Index = 1 << 1,
        Constant = 1 << 2,
        Structured = 1 << 3,
        UnorderedAccess = 1 << 4,
        Indirect = 1 << 5,
        Upload = 1 << 6,    // CPU-writable mapped upload heap
        Readback = 1 << 7,  // CPU-readable readback heap
    };

    inline BufferUsage operator|(BufferUsage a, BufferUsage b)
    {
        return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline bool any(BufferUsage v, BufferUsage flag)
    {
        return (static_cast<uint32_t>(v) & static_cast<uint32_t>(flag)) != 0;
    }

    enum class PrimitiveTopology : uint8_t
    {
        TriangleList,
        TriangleStrip,
        LineList,
        PointList,
    };

    enum class IndexFormat : uint8_t
    {
        Uint16,
        Uint32,
    };

    enum class CompareOp : uint8_t
    {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always,
    };

    enum class StencilOp : uint8_t
    {
        Keep,
        Zero,
        Replace,
        IncrementClamp,
        DecrementClamp,
        Invert,
        IncrementWrap,
        DecrementWrap,
    };

    enum class BlendFactor : uint8_t
    {
        Zero,
        One,
        SrcColor,
        InvSrcColor,
        SrcAlpha,
        InvSrcAlpha,
        DstAlpha,
        InvDstAlpha,
        DstColor,
        InvDstColor,
    };

    enum class BlendOp : uint8_t
    {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max,
    };

    enum class CullMode : uint8_t
    {
        None,
        Front,
        Back,
    };

    enum class FillMode : uint8_t
    {
        Solid,
        Wireframe,
    };

    enum class FilterMode : uint8_t
    {
        Nearest,
        Linear,
    };

    enum class AddressMode : uint8_t
    {
        Repeat,
        MirrorRepeat,
        ClampToEdge,
        ClampToBorder,
    };

    enum class ShaderStage : uint8_t
    {
        Vertex,
        Pixel,
        Compute,
        RayGen,
        Miss,
        ClosestHit,
        AnyHit,
    };

    struct TextureDesc
    {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depthOrArraySize = 1;
        uint32_t mipLevels = 1;
        Format format = Format::Unknown;
        // Optional typed view format. When `format` is a typeless variant
        // (e.g. R32G8X24Typeless), the optimized clear value's format must be
        // a typed format compatible with the resource (e.g. D32FloatS8X24Uint).
        // If left as Format::Unknown, the gfx backend uses `format` for the
        // clear value — which is invalid for typeless resources and on WARP
        // can TDR.
        Format viewFormat = Format::Unknown;
        TextureUsage usage = TextureUsage::ShaderResource;
        bool isCubemap = false;
        ResourceState initialState = ResourceState::Common;
        // Optional optimized clear value (used when usage has RenderTarget or DepthStencil).
        bool useClearValue = false;
        float clearColor[4] = { 0.f, 0.f, 0.f, 0.f };
        float clearDepth = 1.0f;
        uint8_t clearStencil = 0;
        std::string_view debugName;
    };

    struct VertexBufferView
    {
        uint64_t gpuAddress = 0;
        uint32_t sizeInBytes = 0;
        uint32_t strideInBytes = 0;
    };

    struct IndexBufferView
    {
        uint64_t gpuAddress = 0;
        uint32_t sizeInBytes = 0;
        IndexFormat format = IndexFormat::Uint32;
    };

    struct ShaderBytecode
    {
        const void* data = nullptr;
        size_t size = 0;
    };

    struct BufferDesc
    {
        uint64_t size = 0;
        BufferUsage usage = BufferUsage::None;
        uint32_t structuredStride = 0;  // for Structured buffers
        ResourceState initialState = ResourceState::Common;
        std::string_view debugName;
    };

    struct ShaderDesc
    {
        ShaderStage stage = ShaderStage::Vertex;
        const void* bytecode = nullptr;
        size_t bytecodeSize = 0;
        std::string_view entry = "main";
        std::string_view debugName;
    };

    struct VertexAttribute
    {
        std::string_view semantic;  // "POSITION", "NORMAL", "TEXCOORD"
        uint32_t semanticIndex = 0;
        Format format = Format::Unknown;
        uint32_t offset = 0;
    };

    struct DepthStencilState
    {
        bool depthEnable = true;
        bool depthWrite = true;
        CompareOp depthCompare = CompareOp::Less;
        bool stencilEnable = false;
        uint8_t stencilReadMask = 0xFF;
        uint8_t stencilWriteMask = 0xFF;
        CompareOp stencilCompare = CompareOp::Always;
        StencilOp stencilFail = StencilOp::Keep;
        StencilOp stencilDepthFail = StencilOp::Keep;
        StencilOp stencilPass = StencilOp::Keep;
    };

    struct BlendState
    {
        bool blendEnable = false;
        BlendFactor srcColor = BlendFactor::One;
        BlendFactor dstColor = BlendFactor::Zero;
        BlendOp colorOp = BlendOp::Add;
        BlendFactor srcAlpha = BlendFactor::One;
        BlendFactor dstAlpha = BlendFactor::Zero;
        BlendOp alphaOp = BlendOp::Add;
        uint8_t writeMask = 0xF;  // RGBA
    };

    struct RasterizerState
    {
        FillMode fill = FillMode::Solid;
        CullMode cull = CullMode::Back;
        bool frontCounterClockwise = false;
        int32_t depthBias = 0;
        float depthBiasClamp = 0.0f;
        float slopeScaledDepthBias = 0.0f;
        bool depthClipEnable = true;
    };

    struct GraphicsPipelineDesc
    {
        ShaderHandle vs;
        ShaderHandle ps;
        std::span<const VertexAttribute> vertexAttributes;
        uint32_t vertexStride = 0;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        DepthStencilState depthStencil;
        BlendState blend[8];
        uint32_t numRenderTargets = 1;
        Format renderTargetFormats[8] = { Format::Unknown };
        Format depthStencilFormat = Format::Unknown;
        RasterizerState rasterizer;
        uint32_t sampleCount = 1;
        // Optional escape hatch: when non-null, the backend uses this native
        // root-signature pointer instead of its own bindless root sig. Engine
        // PSOs that use descriptor-table layouts (scene, gbuffer, grid, etc.)
        // pass an `ID3D12RootSignature*` here as a transitional measure until
        // the bindless rewrite (P2) lands. NULL means "use the bindless root
        // sig that gfx::IDevice owns".
        void* nativeRootSignatureOverride = nullptr;
        std::string_view debugName;
    };

    struct ComputePipelineDesc
    {
        ShaderHandle cs;
        void* nativeRootSignatureOverride = nullptr;
        std::string_view debugName;
    };

    struct SamplerDesc
    {
        FilterMode minFilter = FilterMode::Linear;
        FilterMode magFilter = FilterMode::Linear;
        FilterMode mipFilter = FilterMode::Linear;
        AddressMode addressU = AddressMode::Repeat;
        AddressMode addressV = AddressMode::Repeat;
        AddressMode addressW = AddressMode::Repeat;
        bool comparisonEnable = false;
        CompareOp comparison = CompareOp::Less;
        float maxAnisotropy = 1.0f;
        float borderColor[4] = { 0.f, 0.f, 0.f, 0.f };
    };

    struct Viewport
    {
        float x = 0, y = 0;
        float width = 0, height = 0;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    struct ScissorRect
    {
        int32_t x = 0, y = 0;
        int32_t width = 0, height = 0;
    };

    enum class LoadOp : uint8_t
    {
        Load,
        Clear,
        DontCare
    };
    enum class StoreOp : uint8_t
    {
        Store,
        DontCare
    };

    struct ColorAttachment
    {
        TextureHandle texture;
        uint32_t mipLevel = 0;
        uint32_t arraySlice = 0;
        LoadOp loadOp = LoadOp::Load;
        StoreOp storeOp = StoreOp::Store;
        float clearColor[4] = { 0.f, 0.f, 0.f, 0.f };
    };

    struct DepthAttachment
    {
        TextureHandle texture;
        uint32_t mipLevel = 0;
        uint32_t arraySlice = 0;
        LoadOp depthLoadOp = LoadOp::Load;
        StoreOp depthStoreOp = StoreOp::Store;
        LoadOp stencilLoadOp = LoadOp::DontCare;
        StoreOp stencilStoreOp = StoreOp::DontCare;
        float clearDepth = 1.0f;
        uint8_t clearStencil = 0;
    };

    struct RenderPassDesc
    {
        std::array<ColorAttachment, 8> colorAttachments;
        uint32_t numColorAttachments = 0;
        DepthAttachment depthAttachment;
        bool hasDepth = false;
    };

    struct Capabilities
    {
        bool raytracing = false;
        bool meshShaders = false;
        bool bindless = false;
        uint32_t maxBindlessDescriptors = 0;
    };

    enum class BackendKind : uint8_t
    {
        D3D12,
        Vulkan,
        Metal,
        Mock,
    };

    struct DeviceDesc
    {
        bool useWarp = false;
        bool enableDebugLayer = false;
        bool enableGpuValidation = false;
        uint32_t maxBindlessDescriptors = 1u << 16;
    };

    struct SwapChainDesc
    {
        void* nativeWindowHandle = nullptr;  // HWND on Windows
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t bufferCount = 3;
        Format format = Format::RGBA8Unorm;
        bool allowTearing = false;
    };

}  // namespace gfx
