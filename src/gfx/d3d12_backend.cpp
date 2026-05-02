// gfx D3D12 backend — Device class implementation and gfx::createDevice
// factory.
//
// The Queue + CommandList live in d3d12_command.cpp; SwapChain lives in
// d3d12_swapchain.cpp; format / state conversion helpers and shared internal
// class declarations live in d3d12_internal.h.

#include <cstring>
#include <string>

#include "d3d12_internal.h"

import common;

using Microsoft::WRL::ComPtr;

namespace gfxd3d12
{

    Device::Device(const gfx::DeviceDesc& d) : desc(d)
    {
        if (d.enableDebugLayer) {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
                debug->EnableDebugLayer();
                if (d.enableGpuValidation) {
                    ComPtr<ID3D12Debug1> debug1;
                    if (SUCCEEDED(debug.As(&debug1))) {
                        debug1->SetEnableGPUBasedValidation(TRUE);
                    }
                }
            }
        }

        createDeviceAndFactory();

        queue = std::make_unique<Queue>(this, d3dDevice.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);

        initBindlessHeaps(d.maxBindlessDescriptors);
        createBindlessRootSignature();

        capabilities.bindless = true;
        capabilities.maxBindlessDescriptors = d.maxBindlessDescriptors;

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5{};
        if (SUCCEEDED(
                d3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5))
            )) {
            capabilities.raytracing = opts5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 opts7{};
        if (SUCCEEDED(
                d3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opts7, sizeof(opts7))
            )) {
            capabilities.meshShaders = opts7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
        }
    }

    Device::~Device()
    {
        if (queue) {
            queue->flush();
        }
        pendingDestroys.clear();
    }

    void Device::createDeviceAndFactory()
    {
        UINT factoryFlags = 0;
        if (desc.enableDebugLayer) {
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
        chkDX(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");

        if (desc.useWarp) {
            ComPtr<IDXGIAdapter1> warp;
            chkDX(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter");
            adapter = warp;
        } else {
            for (UINT i = 0; factory->EnumAdapterByGpuPreference(
                                 i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)
                             ) != DXGI_ERROR_NOT_FOUND;
                 ++i) {
                DXGI_ADAPTER_DESC1 ad{};
                adapter->GetDesc1(&ad);
                if ((ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                    continue;
                }
                if (SUCCEEDED(D3D12CreateDevice(
                        adapter.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device2), nullptr
                    ))) {
                    break;
                }
            }
        }
        chkDX(
            D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3dDevice)),
            "D3D12CreateDevice"
        );
    }

    void Device::initBindlessHeaps(uint32_t capacity)
    {
        resourceHeap.init(
            d3dDevice.Get(), capacity, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true,
            L"gfx_bindless_resource_heap"
        );
        samplerHeap_.init(
            d3dDevice.Get(), 256, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, true,
            L"gfx_bindless_sampler_heap"
        );
    }

    void Device::createBindlessRootSignature()
    {
        // Bindless layout:
        //   Slot 0: 16 root constants (b0)  — per-draw indices block
        //   Slot 1: CBV (b1)                — per-frame
        //   Slot 2: CBV (b2)                — per-pass
        //   Slot 3: SRV/UAV unbounded table (t0+, u0+ at space0)
        //   Slot 4: Sampler unbounded table
        CD3DX12_ROOT_PARAMETER1 params[5] = {};
        params[0].InitAsConstants(16, 0);
        params[1].InitAsConstantBufferView(1);
        params[2].InitAsConstantBufferView(2);

        CD3DX12_DESCRIPTOR_RANGE1 srvRange{};
        srvRange.Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
        );
        params[3].InitAsDescriptorTable(1, &srvRange);

        CD3DX12_DESCRIPTOR_RANGE1 sampRange{};
        sampRange.Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, UINT_MAX, 0, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
        );
        params[4].InitAsDescriptorTable(1, &sampRange);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsd;
        rsd.Init_1_1(
            _countof(params), params, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED
        );

        ComPtr<ID3DBlob> blob, err;
        HRESULT hr = D3DX12SerializeVersionedRootSignature(
            &rsd, D3D_ROOT_SIGNATURE_VERSION_1_1, &blob, &err
        );
        if (FAILED(hr)) {
            throw std::runtime_error(
                std::string("Bindless root sig serialize failed: ") +
                (err ? static_cast<const char*>(err->GetBufferPointer()) : "")
            );
        }
        chkDX(
            d3dDevice->CreateRootSignature(
                0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)
            ),
            "CreateRootSignature(bindless)"
        );
    }

    gfx::TextureHandle Device::createTexture(const gfx::TextureDesc& d)
    {
        D3D12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Tex2D(
            toDXGI(d.format), d.width, d.height, static_cast<UINT16>(d.depthOrArraySize),
            static_cast<UINT16>(d.mipLevels)
        );
        rd.Flags = toD3D12ResourceFlags(d.usage);

        D3D12_CLEAR_VALUE cv{};
        cv.Format = toDXGI(d.format);
        if (gfx::any(d.usage, gfx::TextureUsage::DepthStencil)) {
            cv.DepthStencil.Depth = d.clearDepth;
            cv.DepthStencil.Stencil = d.clearStencil;
        } else {
            std::memcpy(cv.Color, d.clearColor, sizeof(float) * 4);
        }
        const D3D12_CLEAR_VALUE* cvPtr = d.useClearValue ? &cv : nullptr;

        auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        ComPtr<ID3D12Resource> res;
        chkDX(
            d3dDevice->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &rd, toD3D12States(d.initialState), cvPtr,
                IID_PPV_ARGS(&res)
            ),
            "CreateCommittedResource(texture)"
        );
        if (!d.debugName.empty()) {
            std::wstring wname(d.debugName.begin(), d.debugName.end());
            res->SetName(wname.c_str());
        }

        std::lock_guard<std::mutex> lk(poolMu);
        uint32_t slot;
        if (!freeTextures.empty()) {
            slot = freeTextures.back();
            freeTextures.pop_back();
            textures[slot] = {};
        } else {
            slot = static_cast<uint32_t>(textures.size());
            textures.emplace_back();
        }
        auto& rec = textures[slot];
        rec.resource = std::move(res);
        rec.desc = d;
        rec.currentState = toD3D12States(d.initialState);

        if (gfx::any(d.usage, gfx::TextureUsage::ShaderResource)) {
            rec.srvIndex = resourceHeap.allocate();
            writeSrvForTexture(rec);
        }
        return { slot };
    }

    void Device::writeSrvForTexture(TextureRecord& tex)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = toDXGI(tex.desc.format);
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (tex.desc.isCubemap) {
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            sd.TextureCube.MipLevels = tex.desc.mipLevels;
        } else if (tex.desc.depthOrArraySize > 1) {
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            sd.Texture2DArray.MipLevels = tex.desc.mipLevels;
            sd.Texture2DArray.ArraySize = tex.desc.depthOrArraySize;
        } else {
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MipLevels = tex.desc.mipLevels;
        }
        d3dDevice->CreateShaderResourceView(
            tex.resource.Get(), &sd, resourceHeap.cpuHandle(tex.srvIndex)
        );
    }

    gfx::BufferHandle Device::createBuffer(const gfx::BufferDesc& d)
    {
        D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_STATES initState = toD3D12States(d.initialState);
        if (gfx::any(d.usage, gfx::BufferUsage::Upload)) {
            heapType = D3D12_HEAP_TYPE_UPLOAD;
            initState = D3D12_RESOURCE_STATE_GENERIC_READ;
        } else if (gfx::any(d.usage, gfx::BufferUsage::Readback)) {
            heapType = D3D12_HEAP_TYPE_READBACK;
            initState = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        auto heap = CD3DX12_HEAP_PROPERTIES(heapType);
        auto rd = CD3DX12_RESOURCE_DESC::Buffer(
            d.size, gfx::any(d.usage, gfx::BufferUsage::UnorderedAccess)
                        ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                        : D3D12_RESOURCE_FLAG_NONE
        );

        ComPtr<ID3D12Resource> res;
        chkDX(
            d3dDevice->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &rd, initState, nullptr, IID_PPV_ARGS(&res)
            ),
            "CreateCommittedResource(buffer)"
        );
        if (!d.debugName.empty()) {
            std::wstring wname(d.debugName.begin(), d.debugName.end());
            res->SetName(wname.c_str());
        }

        std::lock_guard<std::mutex> lk(poolMu);
        uint32_t slot;
        if (!freeBuffers.empty()) {
            slot = freeBuffers.back();
            freeBuffers.pop_back();
            buffers[slot] = {};
        } else {
            slot = static_cast<uint32_t>(buffers.size());
            buffers.emplace_back();
        }
        auto& rec = buffers[slot];
        rec.resource = std::move(res);
        rec.desc = d;
        rec.currentState = initState;
        return { slot };
    }

    gfx::ShaderHandle Device::createShader(const gfx::ShaderDesc& d)
    {
        if (!d.bytecode || d.bytecodeSize == 0) {
            return {};
        }
        std::lock_guard<std::mutex> lk(poolMu);
        uint32_t slot;
        if (!freeShaders.empty()) {
            slot = freeShaders.back();
            freeShaders.pop_back();
            shaders[slot] = {};
        } else {
            slot = static_cast<uint32_t>(shaders.size());
            shaders.emplace_back();
        }
        auto& rec = shaders[slot];
        rec.bytecode.assign(
            static_cast<const uint8_t*>(d.bytecode),
            static_cast<const uint8_t*>(d.bytecode) + d.bytecodeSize
        );
        rec.stage = d.stage;
        return { slot };
    }

    gfx::PipelineHandle Device::createGraphicsPipeline(const gfx::GraphicsPipelineDesc& d)
    {
        auto* vs = getShader(d.vs);
        auto* ps = getShader(d.ps);
        if (!vs) {
            throw std::runtime_error("createGraphicsPipeline: invalid vertex shader");
        }

        std::vector<D3D12_INPUT_ELEMENT_DESC> elems;
        std::vector<std::string> semantics;
        elems.reserve(d.vertexAttributes.size());
        semantics.reserve(d.vertexAttributes.size());
        for (const auto& a : d.vertexAttributes) {
            semantics.emplace_back(a.semantic);
            D3D12_INPUT_ELEMENT_DESC e{};
            e.SemanticName = semantics.back().c_str();
            e.SemanticIndex = a.semanticIndex;
            e.Format = toDXGI(a.format);
            e.AlignedByteOffset = a.offset;
            e.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            elems.push_back(e);
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = d.nativeRootSignatureOverride
                                ? static_cast<ID3D12RootSignature*>(d.nativeRootSignatureOverride)
                                : rootSignature.Get();
        pd.VS = { vs->bytecode.data(), vs->bytecode.size() };
        if (ps) {
            pd.PS = { ps->bytecode.data(), ps->bytecode.size() };
        }
        pd.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        for (uint32_t i = 0; i < d.numRenderTargets; ++i) {
            const auto& b = d.blend[i];
            pd.BlendState.RenderTarget[i].BlendEnable = b.blendEnable;
            pd.BlendState.RenderTarget[i].SrcBlend = toBlend(b.srcColor);
            pd.BlendState.RenderTarget[i].DestBlend = toBlend(b.dstColor);
            pd.BlendState.RenderTarget[i].BlendOp = toBlendOp(b.colorOp);
            pd.BlendState.RenderTarget[i].SrcBlendAlpha = toBlend(b.srcAlpha);
            pd.BlendState.RenderTarget[i].DestBlendAlpha = toBlend(b.dstAlpha);
            pd.BlendState.RenderTarget[i].BlendOpAlpha = toBlendOp(b.alphaOp);
            pd.BlendState.RenderTarget[i].RenderTargetWriteMask = b.writeMask;
        }
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pd.RasterizerState.CullMode = toCull(d.rasterizer.cull);
        pd.RasterizerState.FrontCounterClockwise = d.rasterizer.frontCounterClockwise;
        pd.RasterizerState.DepthBias = d.rasterizer.depthBias;
        pd.RasterizerState.DepthBiasClamp = d.rasterizer.depthBiasClamp;
        pd.RasterizerState.SlopeScaledDepthBias = d.rasterizer.slopeScaledDepthBias;
        pd.RasterizerState.DepthClipEnable = d.rasterizer.depthClipEnable;
        pd.RasterizerState.FillMode = d.rasterizer.fill == gfx::FillMode::Wireframe
                                          ? D3D12_FILL_MODE_WIREFRAME
                                          : D3D12_FILL_MODE_SOLID;

        pd.DepthStencilState.DepthEnable = d.depthStencil.depthEnable;
        pd.DepthStencilState.DepthWriteMask =
            d.depthStencil.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        pd.DepthStencilState.DepthFunc = toCompare(d.depthStencil.depthCompare);
        pd.DepthStencilState.StencilEnable = d.depthStencil.stencilEnable;
        pd.DepthStencilState.StencilReadMask = d.depthStencil.stencilReadMask;
        pd.DepthStencilState.StencilWriteMask = d.depthStencil.stencilWriteMask;
        auto toStencilOp = [](gfx::StencilOp op) {
            switch (op) {
                case gfx::StencilOp::Keep:
                    return D3D12_STENCIL_OP_KEEP;
                case gfx::StencilOp::Zero:
                    return D3D12_STENCIL_OP_ZERO;
                case gfx::StencilOp::Replace:
                    return D3D12_STENCIL_OP_REPLACE;
                case gfx::StencilOp::IncrementClamp:
                    return D3D12_STENCIL_OP_INCR_SAT;
                case gfx::StencilOp::DecrementClamp:
                    return D3D12_STENCIL_OP_DECR_SAT;
                case gfx::StencilOp::Invert:
                    return D3D12_STENCIL_OP_INVERT;
                case gfx::StencilOp::IncrementWrap:
                    return D3D12_STENCIL_OP_INCR;
                case gfx::StencilOp::DecrementWrap:
                    return D3D12_STENCIL_OP_DECR;
            }
            return D3D12_STENCIL_OP_KEEP;
        };
        D3D12_DEPTH_STENCILOP_DESC sop{};
        sop.StencilFailOp = toStencilOp(d.depthStencil.stencilFail);
        sop.StencilDepthFailOp = toStencilOp(d.depthStencil.stencilDepthFail);
        sop.StencilPassOp = toStencilOp(d.depthStencil.stencilPass);
        sop.StencilFunc = toCompare(d.depthStencil.stencilCompare);
        pd.DepthStencilState.FrontFace = sop;
        pd.DepthStencilState.BackFace = sop;

        pd.InputLayout = { elems.data(), static_cast<UINT>(elems.size()) };
        pd.PrimitiveTopologyType = toTopologyType(d.topology);
        pd.NumRenderTargets = d.numRenderTargets;
        for (uint32_t i = 0; i < d.numRenderTargets; ++i) {
            pd.RTVFormats[i] = toDXGI(d.renderTargetFormats[i]);
        }
        pd.DSVFormat = toDXGI(d.depthStencilFormat);
        pd.SampleDesc = { d.sampleCount, 0 };

        ComPtr<ID3D12PipelineState> pso;
        chkDX(
            d3dDevice->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)),
            "CreateGraphicsPipelineState"
        );

        std::lock_guard<std::mutex> lk(poolMu);
        uint32_t slot;
        if (!freePipelines.empty()) {
            slot = freePipelines.back();
            freePipelines.pop_back();
            pipelines[slot] = {};
        } else {
            slot = static_cast<uint32_t>(pipelines.size());
            pipelines.emplace_back();
        }
        auto& rec = pipelines[slot];
        rec.pso = std::move(pso);
        rec.rootSig = rootSignature;
        rec.isCompute = false;
        rec.topology = toTopology(d.topology);
        return { slot };
    }

    gfx::PipelineHandle Device::createComputePipeline(const gfx::ComputePipelineDesc& d)
    {
        auto* cs = getShader(d.cs);
        if (!cs) {
            throw std::runtime_error("createComputePipeline: invalid compute shader");
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = d.nativeRootSignatureOverride
                                ? static_cast<ID3D12RootSignature*>(d.nativeRootSignatureOverride)
                                : rootSignature.Get();
        pd.CS = { cs->bytecode.data(), cs->bytecode.size() };
        ComPtr<ID3D12PipelineState> pso;
        chkDX(
            d3dDevice->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)),
            "CreateComputePipelineState"
        );

        std::lock_guard<std::mutex> lk(poolMu);
        uint32_t slot;
        if (!freePipelines.empty()) {
            slot = freePipelines.back();
            freePipelines.pop_back();
            pipelines[slot] = {};
        } else {
            slot = static_cast<uint32_t>(pipelines.size());
            pipelines.emplace_back();
        }
        auto& rec = pipelines[slot];
        rec.pso = std::move(pso);
        rec.rootSig = rootSignature;
        rec.isCompute = true;
        return { slot };
    }

    gfx::SamplerHandle Device::createSampler(const gfx::SamplerDesc& d)
    {
        auto toAddr = [](gfx::AddressMode m) {
            switch (m) {
                case gfx::AddressMode::Repeat:
                    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                case gfx::AddressMode::MirrorRepeat:
                    return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
                case gfx::AddressMode::ClampToEdge:
                    return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                case gfx::AddressMode::ClampToBorder:
                    return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            }
            return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        };
        D3D12_SAMPLER_DESC sd{};
        sd.Filter = d.comparisonEnable ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR
                                       : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = toAddr(d.addressU);
        sd.AddressV = toAddr(d.addressV);
        sd.AddressW = toAddr(d.addressW);
        sd.MipLODBias = 0;
        sd.MaxAnisotropy = static_cast<UINT>(d.maxAnisotropy);
        sd.ComparisonFunc = toCompare(d.comparison);
        std::memcpy(sd.BorderColor, d.borderColor, sizeof(float) * 4);
        sd.MinLOD = 0.0f;
        sd.MaxLOD = D3D12_FLOAT32_MAX;

        uint32_t idx = samplerHeap_.allocate();
        d3dDevice->CreateSampler(&sd, samplerHeap_.cpuHandle(idx));

        std::lock_guard<std::mutex> lk(poolMu);
        uint32_t slot;
        if (!freeSamplers.empty()) {
            slot = freeSamplers.back();
            freeSamplers.pop_back();
            samplers[slot] = {};
        } else {
            slot = static_cast<uint32_t>(samplers.size());
            samplers.emplace_back();
        }
        samplers[slot].index = idx;
        samplers[slot].desc = d;
        return { slot };
    }

    gfx::AccelStructHandle Device::createAccelStruct(uint64_t)
    {
        if (!capabilities.raytracing) {
            throw std::runtime_error("createAccelStruct: raytracing not supported on this device");
        }
        return {};  // full DXR support is a later phase
    }

    void Device::destroy(gfx::TextureHandle h)
    {
        if (h.id == 0 || h.id >= textures.size()) {
            return;
        }
        std::lock_guard<std::mutex> lk(poolMu);
        auto& rec = textures[h.id];
        if (!rec.external && rec.resource) {
            if (rec.srvIndex) {
                resourceHeap.free(rec.srvIndex);
            }
            pendingDestroys.push_back({ queue->signal().value, std::move(rec.resource) });
        }
        rec = {};
        freeTextures.push_back(h.id);
    }

    void Device::destroy(gfx::BufferHandle h)
    {
        if (h.id == 0 || h.id >= buffers.size()) {
            return;
        }
        std::lock_guard<std::mutex> lk(poolMu);
        auto& rec = buffers[h.id];
        if (rec.resource) {
            if (rec.srvIndex) {
                resourceHeap.free(rec.srvIndex);
            }
            pendingDestroys.push_back({ queue->signal().value, std::move(rec.resource) });
        }
        rec = {};
        freeBuffers.push_back(h.id);
    }

    void Device::destroy(gfx::ShaderHandle h)
    {
        if (h.id == 0 || h.id >= shaders.size()) {
            return;
        }
        std::lock_guard<std::mutex> lk(poolMu);
        shaders[h.id] = {};
        freeShaders.push_back(h.id);
    }

    void Device::destroy(gfx::PipelineHandle h)
    {
        if (h.id == 0 || h.id >= pipelines.size()) {
            return;
        }
        std::lock_guard<std::mutex> lk(poolMu);
        pipelines[h.id] = {};
        freePipelines.push_back(h.id);
    }

    void Device::destroy(gfx::SamplerHandle h)
    {
        if (h.id == 0 || h.id >= samplers.size()) {
            return;
        }
        std::lock_guard<std::mutex> lk(poolMu);
        if (samplers[h.id].index) {
            samplerHeap_.free(samplers[h.id].index);
        }
        samplers[h.id] = {};
        freeSamplers.push_back(h.id);
    }

    void Device::destroy(gfx::AccelStructHandle) {}

    void* Device::map(gfx::BufferHandle h)
    {
        auto* rec = getBuffer(h);
        if (!rec) {
            return nullptr;
        }
        if (rec->mapped) {
            return rec->mapped;
        }
        D3D12_RANGE r{ 0, 0 };
        chkDX(rec->resource->Map(0, &r, &rec->mapped), "Buffer::Map");
        return rec->mapped;
    }

    void Device::unmap(gfx::BufferHandle h)
    {
        auto* rec = getBuffer(h);
        if (!rec || !rec->mapped) {
            return;
        }
        rec->resource->Unmap(0, nullptr);
        rec->mapped = nullptr;
    }

    void
    Device::uploadBuffer(gfx::BufferHandle dst, const void* data, uint64_t size, uint64_t dstOffset)
    {
        gfx::BufferDesc ud{};
        ud.size = size;
        ud.usage = gfx::BufferUsage::Upload;
        ud.debugName = "transient_upload";
        auto upload = createBuffer(ud);
        auto* uploadRec = getBuffer(upload);
        void* mapped = nullptr;
        D3D12_RANGE r{ 0, 0 };
        chkDX(uploadRec->resource->Map(0, &r, &mapped), "Upload::Map");
        std::memcpy(mapped, data, size);
        uploadRec->resource->Unmap(0, nullptr);

        auto* dstRec = getBuffer(dst);
        if (!dstRec) {
            throw std::runtime_error("uploadBuffer: invalid destination handle");
        }

        auto* list = static_cast<CommandList*>(queue->acquireCommandList());
        list->begin();
        auto* native = list->native();

        if (dstRec->currentState != D3D12_RESOURCE_STATE_COPY_DEST) {
            auto b = CD3DX12_RESOURCE_BARRIER::Transition(
                dstRec->resource.Get(), dstRec->currentState, D3D12_RESOURCE_STATE_COPY_DEST
            );
            native->ResourceBarrier(1, &b);
        }
        native->CopyBufferRegion(
            dstRec->resource.Get(), dstOffset, uploadRec->resource.Get(), 0, size
        );
        if (dstRec->currentState != D3D12_RESOURCE_STATE_COPY_DEST) {
            auto b = CD3DX12_RESOURCE_BARRIER::Transition(
                dstRec->resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, dstRec->currentState
            );
            native->ResourceBarrier(1, &b);
        }
        list->end();
        auto fv = queue->submit(list);
        pendingDestroys.push_back({ fv.value, uploadRec->resource });
        destroy(upload);
    }

    void Device::uploadTexture(gfx::TextureHandle, const void*, uint64_t, uint64_t)
    {
        throw std::runtime_error("uploadTexture: not yet implemented");
    }

    uint32_t Device::bindlessSrvIndex(gfx::TextureHandle h)
    {
        auto* r = getTexture(h);
        return r ? r->srvIndex : 0;
    }
    uint32_t Device::bindlessSrvIndex(gfx::BufferHandle h)
    {
        auto* r = getBuffer(h);
        return r ? r->srvIndex : 0;
    }
    uint32_t Device::bindlessUavIndex(gfx::TextureHandle h)
    {
        auto* r = getTexture(h);
        return r ? r->uavIndex : 0;
    }
    uint32_t Device::bindlessUavIndex(gfx::BufferHandle h)
    {
        auto* r = getBuffer(h);
        return r ? r->uavIndex : 0;
    }
    uint32_t Device::bindlessSamplerIndex(gfx::SamplerHandle h)
    {
        auto* r = getSampler(h);
        return r ? r->index : 0;
    }

    std::unique_ptr<gfx::ISwapChain> Device::createSwapChain(const gfx::SwapChainDesc& d)
    {
        return std::make_unique<SwapChain>(this, d);
    }

    void Device::retireCompletedResources()
    {
        auto completed = queue->completedFenceValue().value;
        while (!pendingDestroys.empty() && pendingDestroys.front().fenceValue <= completed) {
            pendingDestroys.pop_front();
        }
    }

    TextureRecord* Device::getTexture(gfx::TextureHandle h)
    {
        return (h.id == 0 || h.id >= textures.size()) ? nullptr : &textures[h.id];
    }
    BufferRecord* Device::getBuffer(gfx::BufferHandle h)
    {
        return (h.id == 0 || h.id >= buffers.size()) ? nullptr : &buffers[h.id];
    }
    ShaderRecord* Device::getShader(gfx::ShaderHandle h)
    {
        return (h.id == 0 || h.id >= shaders.size()) ? nullptr : &shaders[h.id];
    }
    PipelineRecord* Device::getPipeline(gfx::PipelineHandle h)
    {
        return (h.id == 0 || h.id >= pipelines.size()) ? nullptr : &pipelines[h.id];
    }
    SamplerRecord* Device::getSampler(gfx::SamplerHandle h)
    {
        return (h.id == 0 || h.id >= samplers.size()) ? nullptr : &samplers[h.id];
    }

    gfx::TextureHandle Device::adoptBackBuffer(ComPtr<ID3D12Resource> resource, gfx::Format format)
    {
        std::lock_guard<std::mutex> lk(poolMu);
        uint32_t slot;
        if (!freeTextures.empty()) {
            slot = freeTextures.back();
            freeTextures.pop_back();
            textures[slot] = {};
        } else {
            slot = static_cast<uint32_t>(textures.size());
            textures.emplace_back();
        }
        auto& rec = textures[slot];
        rec.resource = std::move(resource);
        rec.desc.format = format;
        rec.desc.usage = gfx::TextureUsage::RenderTarget;
        rec.currentState = D3D12_RESOURCE_STATE_PRESENT;
        rec.external = true;
        return { slot };
    }

    void Device::releaseBackBuffer(gfx::TextureHandle h)
    {
        if (h.id == 0 || h.id >= textures.size()) {
            return;
        }
        std::lock_guard<std::mutex> lk(poolMu);
        textures[h.id] = {};
        freeTextures.push_back(h.id);
    }

    void Device::schedulePendingDestroy(uint64_t fenceValue, ComPtr<ID3D12Resource> resource)
    {
        pendingDestroys.push_back({ fenceValue, std::move(resource) });
    }

}  // namespace gfxd3d12

namespace gfx
{
    std::unique_ptr<IDevice> createDevice(BackendKind kind, const DeviceDesc& desc)
    {
        if (kind != BackendKind::D3D12) {
            throw std::runtime_error("Only D3D12 backend is implemented");
        }
        return std::make_unique<gfxd3d12::Device>(desc);
    }
}  // namespace gfx
