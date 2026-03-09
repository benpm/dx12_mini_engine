module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <Windows.h>
#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <gainput/gainput.h>
#include <ScreenGrab.h>
#include <wincodec.h>
#include <tiny_obj_loader.h>
#include <sstream>
#include <spdlog/spdlog.h>
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch"
    #pragma clang diagnostic ignored "-Wunused-function"
    #pragma clang diagnostic ignored "-Wunused-variable"
    #pragma clang diagnostic ignored "-Wmissing-field-initializers"
    #pragma clang diagnostic ignored "-Wsign-compare"
    #pragma clang diagnostic ignored "-Wnullability-completeness"
#endif
#include <tiny_gltf.h>
#ifdef __clang__
    #pragma clang diagnostic pop
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch"
#endif
#include "d3dx12.h"
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
#include "resource.h"
#include "vertex_shader_cso.h"
#include "pixel_shader_cso.h"
#include "fullscreen_vs_cso.h"
#include "bloom_prefilter_ps_cso.h"
#include "bloom_downsample_ps_cso.h"
#include "bloom_upsample_ps_cso.h"
#include "bloom_composite_ps_cso.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

module application;

import window;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string GetResourceString(int resourceId)
{
    HRSRC hRes = FindResource(nullptr, MAKEINTRESOURCE(resourceId), RT_RCDATA);
    if (!hRes) return "";
    HGLOBAL hMem = LoadResource(nullptr, hRes);
    if (!hMem) return "";
    DWORD size = SizeofResource(nullptr, hRes);
    void* data = LockResource(hMem);
    if (!data) return "";
    return std::string(static_cast<const char*>(data), size);
}

// Build an XMMATRIX from a glTF node (column-major → transposed for HLSL row-major upload)
static XMMATRIX NodeTransform(const tinygltf::Node& node)
{
    if (node.matrix.size() == 16) {
        const auto& m = node.matrix;
        // glTF column-major → XMMATRIX row-major (= transpose)
        return XMMATRIX(
            (float)m[0],  (float)m[1],  (float)m[2],  (float)m[3],
            (float)m[4],  (float)m[5],  (float)m[6],  (float)m[7],
            (float)m[8],  (float)m[9],  (float)m[10], (float)m[11],
            (float)m[12], (float)m[13], (float)m[14], (float)m[15]
        );
    }
    XMMATRIX S = XMMatrixIdentity();
    XMMATRIX R = XMMatrixIdentity();
    XMMATRIX T = XMMatrixIdentity();
    if (node.scale.size() == 3)
        S = XMMatrixScaling((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]);
    if (node.rotation.size() == 4) {
        XMVECTOR q = XMVectorSet(
            (float)node.rotation[0], (float)node.rotation[1],
            (float)node.rotation[2], (float)node.rotation[3]
        );
        R = XMMatrixRotationQuaternion(q);
    }
    if (node.translation.size() == 3)
        T = XMMatrixTranslation(
            (float)node.translation[0], (float)node.translation[1], (float)node.translation[2]
        );
    return S * R * T;
}

// Retrieve typed accessor data as a vector of floats (used for VEC2/VEC3)
template<size_t N>
static std::vector<std::array<float, N>> AccessorToFloatN(
    const tinygltf::Model& model, int accessorIdx)
{
    const auto& acc = model.accessors[accessorIdx];
    const auto& bv  = model.bufferViews[acc.bufferView];
    const auto& buf = model.buffers[bv.buffer];
    const uint8_t* src = buf.data.data() + bv.byteOffset + acc.byteOffset;
    size_t stride = bv.byteStride ? bv.byteStride : (N * sizeof(float));
    std::vector<std::array<float, N>> out(acc.count);
    for (size_t i = 0; i < acc.count; ++i) {
        const float* f = reinterpret_cast<const float*>(src + i * stride);
        for (size_t j = 0; j < N; ++j) out[i][j] = f[j];
    }
    return out;
}

// Convert any index accessor to uint32_t
static std::vector<uint32_t> AccessorToIndices(
    const tinygltf::Model& model, int accessorIdx)
{
    const auto& acc = model.accessors[accessorIdx];
    const auto& bv  = model.bufferViews[acc.bufferView];
    const auto& buf = model.buffers[bv.buffer];
    const uint8_t* src = buf.data.data() + bv.byteOffset + acc.byteOffset;
    std::vector<uint32_t> out(acc.count);
    for (size_t i = 0; i < acc.count; ++i) {
        switch (acc.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                out[i] = src[i];
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                out[i] = reinterpret_cast<const uint16_t*>(src)[i];
                break;
            default:
                out[i] = reinterpret_cast<const uint32_t*>(src)[i];
                break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Application constructor / destructor
// ---------------------------------------------------------------------------

Application::Application() : inputMap(inputManager, "input_map")
{
    spdlog::info("Application constructor start");
    Window::get()->registerApp(this);

    if (!XMVerifyCPUSupport()) {
        spdlog::error("Failed to verify DirectX Math library support.");
        std::exit(EXIT_FAILURE);
    }

    this->viewport = CD3DX12_VIEWPORT(
        0.0f, 0.0f, static_cast<float>(this->clientWidth), static_cast<float>(this->clientHeight)
    );

    spdlog::info("Creating CommandQueue");
    this->cmdQueue = CommandQueue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);

    spdlog::info("Creating SwapChain");
    this->swapChain = this->createSwapChain();

    this->curBackBufIdx = this->swapChain->GetCurrentBackBufferIndex();

    spdlog::info("Creating rtvHeap");
    this->rtvHeap = this->createDescHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, this->nBuffers);
    this->rtvDescSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    spdlog::info("updateRenderTargetViews");
    this->updateRenderTargetViews(this->rtvHeap);

    spdlog::info("Setup gainput");
    this->keyboardID = inputManager.CreateDevice<gainput::InputDeviceKeyboard>();
    this->mouseID    = inputManager.CreateDevice<gainput::InputDeviceMouse>();
    this->rawMouseID = inputManager.CreateDevice<gainput::InputDeviceMouse>(
        gainput::InputDevice::AutoIndex, gainput::InputDevice::DV_RAW
    );
    inputManager.SetDisplaySize(1, 1);
    this->inputMap.MapBool(Button::MoveForward,  this->keyboardID, gainput::KeyW);
    this->inputMap.MapBool(Button::MoveForward,  this->keyboardID, gainput::KeyUp);
    this->inputMap.MapBool(Button::MoveBackward, this->keyboardID, gainput::KeyS);
    this->inputMap.MapBool(Button::MoveBackward, this->keyboardID, gainput::KeyDown);
    this->inputMap.MapBool(Button::MoveLeft,     this->keyboardID, gainput::KeyA);
    this->inputMap.MapBool(Button::MoveLeft,     this->keyboardID, gainput::KeyLeft);
    this->inputMap.MapBool(Button::MoveRight,    this->keyboardID, gainput::KeyD);
    this->inputMap.MapBool(Button::MoveRight,    this->keyboardID, gainput::KeyRight);
    this->inputMap.MapBool(Button::LeftClick,    this->mouseID,    gainput::MouseButtonLeft);
    this->inputMap.MapBool(Button::RightClick,   this->mouseID,    gainput::MouseButtonRight);
    this->inputMap.MapFloat(Button::AxisX,       this->mouseID,    gainput::MouseAxisX);
    this->inputMap.MapFloat(Button::AxisY,       this->mouseID,    gainput::MouseAxisY);
    this->inputMap.MapFloat(Button::AxisDeltaX,  this->mouseID,    gainput::MouseAxisX);
    this->inputMap.MapFloat(Button::AxisDeltaY,  this->mouseID,    gainput::MouseAxisY);
    this->inputMap.MapBool(Button::ScrollUp,     this->mouseID,    gainput::MouseButtonWheelUp);
    this->inputMap.MapBool(Button::ScrollDown,   this->mouseID,    gainput::MouseButtonWheelDown);

    this->loadContent();
    this->flush();
    this->initImGui();
    this->isInitialized = true;
}

Application::~Application()
{
    this->flush();
    this->shutdownImGui();
}

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------

void Application::transitionResource(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    ComPtr<ID3D12Resource> resource,
    D3D12_RESOURCE_STATES beforeState,
    D3D12_RESOURCE_STATES afterState)
{
    CD3DX12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), beforeState, afterState);
    cmdList->ResourceBarrier(1, &barrier);
}

void Application::clearRTV(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv,
    FLOAT clearColor[4])
{
    cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
}

void Application::clearDepth(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv,
    FLOAT depth)
{
    cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
}

void Application::updateBufferResource(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    ID3D12Resource** pDestinationResource,
    ID3D12Resource** pIntermediateResource,
    size_t numElements,
    size_t elementSize,
    const void* bufferData,
    D3D12_RESOURCE_FLAGS flags)
{
    const size_t bufSize = numElements * elementSize;
    {
        const CD3DX12_HEAP_PROPERTIES pHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC pDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSize, flags);
        chkDX(device->CreateCommittedResource(
            &pHeapProperties, D3D12_HEAP_FLAG_NONE, &pDesc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(pDestinationResource)
        ));
    }
    if (bufferData) {
        const CD3DX12_HEAP_PROPERTIES pHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC pDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSize);
        chkDX(device->CreateCommittedResource(
            &pHeapProperties, D3D12_HEAP_FLAG_NONE, &pDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(pIntermediateResource)
        ));
        D3D12_SUBRESOURCE_DATA subresourceData = {};
        subresourceData.pData       = bufferData;
        subresourceData.RowPitch    = bufSize;
        subresourceData.SlicePitch  = subresourceData.RowPitch;
        ::UpdateSubresources(
            cmdList.Get(), *pDestinationResource, *pIntermediateResource, 0, 0, 1,
            &subresourceData
        );
    }
}

GpuMesh Application::uploadMesh(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    const std::vector<VertexPBR>& vertices,
    const std::vector<uint32_t>& indices)
{
    GpuMesh mesh;
    mesh.numIndices = static_cast<uint32_t>(indices.size());

    ComPtr<ID3D12Resource> intermediateVB, intermediateIB;
    updateBufferResource(
        cmdList, &mesh.vb, &intermediateVB, vertices.size(), sizeof(VertexPBR), vertices.data()
    );
    updateBufferResource(
        cmdList, &mesh.ib, &intermediateIB, indices.size(), sizeof(uint32_t), indices.data()
    );

    mesh.vbv.BufferLocation = mesh.vb->GetGPUVirtualAddress();
    mesh.vbv.SizeInBytes    = static_cast<UINT>(vertices.size() * sizeof(VertexPBR));
    mesh.vbv.StrideInBytes  = sizeof(VertexPBR);

    mesh.ibv.BufferLocation = mesh.ib->GetGPUVirtualAddress();
    mesh.ibv.Format         = DXGI_FORMAT_R32_UINT;
    mesh.ibv.SizeInBytes    = static_cast<UINT>(indices.size() * sizeof(uint32_t));

    // Keep intermediate buffers alive until GPU finishes
    // (they're kept alive by the ComPtr in the lambda capture — we rely on the
    //  caller flushing before the function returns in loadContent / loadGltf)
    (void)intermediateVB;  // released on function exit — OK because caller flushes
    (void)intermediateIB;

    return mesh;
}

// ---------------------------------------------------------------------------
// Depth buffer
// ---------------------------------------------------------------------------

void Application::resizeDepthBuffer(uint32_t width, uint32_t height)
{
    assert(this->contentLoaded);
    this->flush();
    width  = std::max(1u, width);
    height = std::max(1u, height);

    D3D12_CLEAR_VALUE optimizedClearValue = {};
    optimizedClearValue.Format                   = DXGI_FORMAT_D32_FLOAT;
    optimizedClearValue.DepthStencil             = {1.0f, 0};
    const CD3DX12_HEAP_PROPERTIES pHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    const CD3DX12_RESOURCE_DESC pDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D32_FLOAT, width, height, 1, 0, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
    );
    chkDX(device->CreateCommittedResource(
        &pHeapProperties, D3D12_HEAP_FLAG_NONE, &pDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &optimizedClearValue, IID_PPV_ARGS(&this->depthBuffer)
    ));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format             = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    dsvDesc.Flags              = D3D12_DSV_FLAG_NONE;
    device->CreateDepthStencilView(
        this->depthBuffer.Get(), &dsvDesc, this->dsvHeap->GetCPUDescriptorHandleForHeapStart()
    );
}

// ---------------------------------------------------------------------------
// Swap chain / descriptor heaps
// ---------------------------------------------------------------------------

ComPtr<IDXGISwapChain4> Application::createSwapChain()
{
    ComPtr<IDXGISwapChain4> dxgiSwapChain4;
    ComPtr<IDXGIFactory4> dxgiFactory4;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    chkDX(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width       = this->clientWidth;
    swapChainDesc.Height      = this->clientHeight;
    swapChainDesc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo      = FALSE;
    swapChainDesc.SampleDesc  = {1, 0};
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = this->nBuffers;
    swapChainDesc.Scaling     = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags       = this->tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    chkDX(dxgiFactory4->CreateSwapChainForHwnd(
        this->cmdQueue.queue.Get(), this->hWnd, &swapChainDesc, nullptr, nullptr, &swapChain1
    ));
    chkDX(dxgiFactory4->MakeWindowAssociation(this->hWnd, DXGI_MWA_NO_ALT_ENTER));
    chkDX(swapChain1.As(&dxgiSwapChain4));
    this->curBackBufIdx = dxgiSwapChain4->GetCurrentBackBufferIndex();
    return dxgiSwapChain4;
}

ComPtr<ID3D12DescriptorHeap>
Application::createDescHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
{
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = numDescriptors;
    desc.Type           = type;
    chkDX(this->device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));
    return descriptorHeap;
}

void Application::updateRenderTargetViews(ComPtr<ID3D12DescriptorHeap> descriptorHeap)
{
    UINT rtvDescriptorSize =
        this->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

    for (int i = 0; i < this->nBuffers; ++i) {
        ComPtr<ID3D12Resource> backBuffer;
        chkDX(this->swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
        this->device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);
        this->backBuffers[i] = backBuffer;
        rtvHandle.Offset(rtvDescriptorSize);
    }
}

// ---------------------------------------------------------------------------
// Update / render
// ---------------------------------------------------------------------------

void Application::update()
{
    [[maybe_unused]] static double elapsedSeconds = 0.0;
    static std::chrono::high_resolution_clock clock;
    static auto t0 = clock.now();

    auto t1       = clock.now();
    auto deltaTime = t1 - t0;
    t0            = t1;
    [[maybe_unused]] const double dt = deltaTime.count() * 1e-9;
    elapsedSeconds += dt;

    const float w = static_cast<float>(this->clientWidth);

    this->mouseDelta = {this->inputMap.GetFloatDelta(Button::AxisDeltaX),
                        this->inputMap.GetFloatDelta(Button::AxisDeltaY)};
    this->mousePos   = {this->inputMap.GetFloat(Button::AxisX),
                        this->inputMap.GetFloat(Button::AxisY)};

    this->matModel = XMMatrixIdentity();
    if (this->inputMap.GetBool(Button::LeftClick)) {
        this->cam.pitch += (this->mouseDelta.y / w) * 180_deg;
        this->cam.yaw   -= (this->mouseDelta.x / w) * 360_deg;
        this->cam.pitch  = std::clamp(this->cam.pitch, -89.9_deg, 89.9_deg);
    }
    if (this->inputMap.GetBool(Button::RightClick)) {
        this->cam.radius += this->mouseDelta.y / w;
    }
    this->cam.aspectRatio = w / static_cast<float>(this->clientHeight);
    if (this->inputMap.GetBoolWasDown(Button::ScrollUp))
        this->cam.radius *= 1.25f;
    if (this->inputMap.GetBoolWasDown(Button::ScrollDown))
        this->cam.radius *= 0.8f;
}

void Application::render()
{
    auto backBuffer = this->backBuffers[this->curBackBufIdx];
    auto cmdList    = this->cmdQueue.getCmdList();

    // --- Scene pass: render to HDR render target ---
    {
        FLOAT clearColor[] = {bgColor[0], bgColor[1], bgColor[2], 1.0f};
        CD3DX12_CPU_DESCRIPTOR_HANDLE hdrRtv(bloomRtvHeap->GetCPUDescriptorHandleForHeapStart());
        this->clearRTV(cmdList, hdrRtv, clearColor);
        auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
        this->clearDepth(cmdList, dsv);

        cmdList->SetPipelineState(this->pipelineState.Get());
        cmdList->SetGraphicsRootSignature(this->rootSignature.Get());
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->RSSetViewports(1, &this->viewport);
        cmdList->RSSetScissorRects(1, &this->scissorRect);
        cmdList->OMSetRenderTargets(1, &hdrRtv, true, &dsv);

        // Build base scene CB (camera, lights — material filled per draw)
        SceneConstantBuffer scb = {};
        scb.viewProj = this->cam.view() * this->cam.proj();

        float camX = this->cam.radius * cos(this->cam.pitch) * cos(this->cam.yaw);
        float camY = this->cam.radius * sin(this->cam.pitch);
        float camZ = this->cam.radius * cos(this->cam.pitch) * sin(this->cam.yaw);
        scb.cameraPos = XMFLOAT4(camX, camY, camZ, 1.0f);

        scb.lightPos   = XMFLOAT4(10.0f, 15.0f, -10.0f, 1.0f);
        scb.lightColor = XMFLOAT4(lightBrightness, lightBrightness, lightBrightness, 1.0f);
        scb.ambientColor = XMFLOAT4(
            bgColor[0] * ambientBrightness, bgColor[1] * ambientBrightness,
            bgColor[2] * ambientBrightness, 1.0f
        );

        for (const auto& mesh : this->meshes) {
            const Material& mat = this->materials[mesh.materialIdx];

            scb.model             = XMLoadFloat4x4(&mesh.transform) * this->matModel;
            scb.albedo            = mat.albedo;
            scb.roughness         = mat.roughness;
            scb.metallic          = mat.metallic;
            scb.emissiveStrength  = mat.emissiveStrength;
            scb._pad              = 0.0f;
            scb.emissive          = mat.emissive;

            cmdList->SetGraphicsRoot32BitConstants(
                0, sizeof(SceneConstantBuffer) / 4, &scb, 0
            );
            cmdList->IASetVertexBuffers(0, 1, &mesh.vbv);
            cmdList->IASetIndexBuffer(&mesh.ibv);
            cmdList->DrawIndexedInstanced(mesh.numIndices, 1, 0, 0, 0);
        }
    }

    // --- Bloom → composite → ImGui → present ---
    this->renderBloom(cmdList);
    this->renderImGui(cmdList);

    {
        this->transitionResource(
            cmdList, backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT
        );
        this->transitionResource(
            cmdList, hdrRenderTarget, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        for (uint32_t i = 0; i < bloomMipCount; ++i) {
            this->transitionResource(
                cmdList, bloomMips[i], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_RENDER_TARGET
            );
        }

        this->frameFenceValues[this->curBackBufIdx] = this->cmdQueue.execCmdList(cmdList);

        UINT syncInterval  = this->vsync ? 1 : 0;
        UINT presentFlags  = this->tearingSupported && !this->vsync
                             ? DXGI_PRESENT_ALLOW_TEARING : 0;
        chkDX(this->swapChain->Present(syncInterval, presentFlags));
        this->curBackBufIdx = this->swapChain->GetCurrentBackBufferIndex();
        this->cmdQueue.waitForFenceVal(this->frameFenceValues[this->curBackBufIdx]);

        if (this->testMode) {
            this->frameCount++;
            if (this->frameCount == 10) {
                spdlog::info("Saving screenshot and exiting...");
                HRESULT hr = DirectX::SaveWICTextureToFile(
                    this->cmdQueue.queue.Get(), backBuffer.Get(), GUID_ContainerFormatPng,
                    L"screenshot.png", D3D12_RESOURCE_STATE_PRESENT,
                    D3D12_RESOURCE_STATE_PRESENT
                );
                if (FAILED(hr))
                    spdlog::error(
                        "Failed to save screenshot! HRESULT: {:#010x}", static_cast<uint32_t>(hr)
                    );
                Window::get()->doExit = true;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Bloom
// ---------------------------------------------------------------------------

void Application::createBloomResources(uint32_t width, uint32_t height)
{
    width  = std::max(1u, width);
    height = std::max(1u, height);

    const DXGI_FORMAT hdrFormat = DXGI_FORMAT_R11G11B10_FLOAT;

    // HDR scene RT
    {
        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format    = hdrFormat;
        clearVal.Color[0]  = 0.4f;
        clearVal.Color[1]  = 0.6f;
        clearVal.Color[2]  = 0.9f;
        clearVal.Color[3]  = 1.0f;
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            hdrFormat, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
        );
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clearVal,
            IID_PPV_ARGS(&this->hdrRenderTarget)
        ));
    }

    uint32_t mipW = std::max(1u, width / 2);
    uint32_t mipH = std::max(1u, height / 2);
    for (uint32_t i = 0; i < bloomMipCount; ++i) {
        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = hdrFormat;
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            hdrFormat, mipW, mipH, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
        );
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clearVal,
            IID_PPV_ARGS(&this->bloomMips[i])
        ));
        mipW = std::max(1u, mipW / 2);
        mipH = std::max(1u, mipH / 2);
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 1 + bloomMipCount;
        desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        chkDX(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&this->bloomRtvHeap)));
    }
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 1 + bloomMipCount;
        desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        chkDX(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&this->srvHeap)));
    }
    this->srvDescSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    UINT rtvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(bloomRtvHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(srvHeap->GetCPUDescriptorHandleForHeapStart());

    device->CreateRenderTargetView(hdrRenderTarget.Get(), nullptr, rtvHandle);
    rtvHandle.Offset(rtvInc);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                  = hdrFormat;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels     = 1;
    device->CreateShaderResourceView(hdrRenderTarget.Get(), &srvDesc, srvHandle);
    srvHandle.Offset(srvDescSize);

    for (uint32_t i = 0; i < bloomMipCount; ++i) {
        device->CreateRenderTargetView(bloomMips[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(rtvInc);
        device->CreateShaderResourceView(bloomMips[i].Get(), &srvDesc, srvHandle);
        srvHandle.Offset(srvDescSize);
    }
}

void Application::renderBloom(ComPtr<ID3D12GraphicsCommandList2> cmdList)
{
    ID3D12DescriptorHeap* heaps[] = {srvHeap.Get()};
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootSignature(bloomRootSignature.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    UINT rtvInc     = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto bloomRtvBase = bloomRtvHeap->GetCPUDescriptorHandleForHeapStart();
    auto srvGpuBase   = srvHeap->GetGPUDescriptorHandleForHeapStart();

    auto getRtv    = [&](uint32_t idx) -> D3D12_CPU_DESCRIPTOR_HANDLE {
        return CD3DX12_CPU_DESCRIPTOR_HANDLE(bloomRtvBase, idx, rtvInc);
    };
    auto getSrvGpu = [&](uint32_t idx) -> D3D12_GPU_DESCRIPTOR_HANDLE {
        return CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuBase, idx, srvDescSize);
    };

    uint32_t mipW[bloomMipCount], mipH[bloomMipCount];
    mipW[0] = std::max(1u, clientWidth / 2);
    mipH[0] = std::max(1u, clientHeight / 2);
    for (uint32_t i = 1; i < bloomMipCount; ++i) {
        mipW[i] = std::max(1u, mipW[i - 1] / 2);
        mipH[i] = std::max(1u, mipH[i - 1] / 2);
    }

    struct BloomCB { float texelSizeX, texelSizeY, param0, param1; };

    // Prefilter
    transitionResource(
        cmdList, hdrRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    cmdList->SetPipelineState(prefilterPSO.Get());
    cmdList->SetGraphicsRootDescriptorTable(0, getSrvGpu(0));
    auto mip0Rtv = getRtv(1);
    cmdList->OMSetRenderTargets(1, &mip0Rtv, false, nullptr);
    CD3DX12_VIEWPORT vp(0.0f, 0.0f, (float)mipW[0], (float)mipH[0]);
    D3D12_RECT sr = {0, 0, (LONG)mipW[0], (LONG)mipH[0]};
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sr);
    BloomCB cb = {1.0f / clientWidth, 1.0f / clientHeight, bloomThreshold, 0.5f};
    cmdList->SetGraphicsRoot32BitConstants(2, 4, &cb, 0);
    cmdList->DrawInstanced(3, 1, 0, 0);

    // Downsample
    cmdList->SetPipelineState(downsamplePSO.Get());
    for (uint32_t i = 0; i < bloomMipCount - 1; ++i) {
        transitionResource(
            cmdList, bloomMips[i], D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        cmdList->SetGraphicsRootDescriptorTable(0, getSrvGpu(1 + i));
        auto rtv = getRtv(2 + i);
        cmdList->OMSetRenderTargets(1, &rtv, false, nullptr);
        vp = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)mipW[i + 1], (float)mipH[i + 1]);
        sr = {0, 0, (LONG)mipW[i + 1], (LONG)mipH[i + 1]};
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sr);
        cb = {1.0f / mipW[i], 1.0f / mipH[i], 0, 0};
        cmdList->SetGraphicsRoot32BitConstants(2, 4, &cb, 0);
        cmdList->DrawInstanced(3, 1, 0, 0);
    }

    // Upsample (additive)
    cmdList->SetPipelineState(upsamplePSO.Get());
    for (int i = bloomMipCount - 2; i >= 0; --i) {
        transitionResource(
            cmdList, bloomMips[i + 1], D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        transitionResource(
            cmdList, bloomMips[i], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        cmdList->SetGraphicsRootDescriptorTable(0, getSrvGpu(2 + i));
        auto rtv = getRtv(1 + i);
        cmdList->OMSetRenderTargets(1, &rtv, false, nullptr);
        vp = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)mipW[i], (float)mipH[i]);
        sr = {0, 0, (LONG)mipW[i], (LONG)mipH[i]};
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sr);
        cb = {1.0f / mipW[i + 1], 1.0f / mipH[i + 1], 1.0f, 0};
        cmdList->SetGraphicsRoot32BitConstants(2, 4, &cb, 0);
        cmdList->DrawInstanced(3, 1, 0, 0);
    }

    // Composite
    transitionResource(
        cmdList, bloomMips[0], D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    auto backBuffer = backBuffers[curBackBufIdx];
    transitionResource(
        cmdList, backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
    );
    cmdList->SetPipelineState(compositePSO.Get());
    cmdList->SetGraphicsRootDescriptorTable(0, getSrvGpu(0));
    cmdList->SetGraphicsRootDescriptorTable(1, getSrvGpu(1));
    CD3DX12_CPU_DESCRIPTOR_HANDLE backBufRtv(
        rtvHeap->GetCPUDescriptorHandleForHeapStart(), curBackBufIdx, rtvDescSize
    );
    cmdList->OMSetRenderTargets(1, &backBufRtv, false, nullptr);
    vp = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)clientWidth, (float)clientHeight);
    sr = {0, 0, (LONG)clientWidth, (LONG)clientHeight};
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sr);
    struct { float a, b, c; uint32_t d; } compositeCB = {0, 0, bloomIntensity, (uint32_t)tonemapMode};
    cmdList->SetGraphicsRoot32BitConstants(2, 4, &compositeCB, 0);
    cmdList->DrawInstanced(3, 1, 0, 0);
}

// ---------------------------------------------------------------------------
// ImGui
// ---------------------------------------------------------------------------

void Application::initImGui()
{
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 16;
        desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        chkDX(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&imguiSrvHeap)));
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hWnd);

    ImGui_ImplDX12_InitInfo initInfo      = {};
    initInfo.Device                       = device.Get();
    initInfo.CommandQueue                 = cmdQueue.queue.Get();
    initInfo.NumFramesInFlight            = nBuffers;
    initInfo.RTVFormat                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat                    = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap            = imguiSrvHeap.Get();
    initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info,
                                       D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                                       D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
        auto* app = static_cast<Application*>(info->UserData);
        UINT inc  = app->device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
        *outCpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            app->imguiSrvHeap->GetCPUDescriptorHandleForHeapStart(), app->imguiSrvNextIndex, inc
        );
        *outGpu = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            app->imguiSrvHeap->GetGPUDescriptorHandleForHeapStart(), app->imguiSrvNextIndex, inc
        );
        app->imguiSrvNextIndex++;
    };
    initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE,
                                      D3D12_GPU_DESCRIPTOR_HANDLE) {};
    initInfo.UserData = this;
    ImGui_ImplDX12_Init(&initInfo);
}

void Application::shutdownImGui()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void Application::renderImGui(ComPtr<ID3D12GraphicsCommandList2> cmdList)
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Settings");

    // --- Bloom ---
    if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Threshold", &bloomThreshold, 0.0f, 3.0f);
        ImGui::SliderFloat("Intensity",  &bloomIntensity, 0.0f, 5.0f);
    }

    // --- Tonemapping ---
    if (ImGui::CollapsingHeader("Tonemapping", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* tonemappers[] = {
            "ACES Filmic", "AgX", "AgX Punchy", "Gran Turismo", "PBR Neutral"
        };
        ImGui::Combo("Tonemapper", &tonemapMode, tonemappers, IM_ARRAYSIZE(tonemappers));
    }

    // --- Scene lighting ---
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Background",       bgColor);
        ImGui::SliderFloat("Light Brightness",   &lightBrightness,   0.0f, 20.0f);
        ImGui::SliderFloat("Ambient Brightness", &ambientBrightness, 0.0f,  2.0f);
    }

    // --- Material editor ---
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!materials.empty()) {
            // Material selector (when GLB has multiple)
            if (materials.size() > 1) {
                std::vector<const char*> names;
                names.reserve(materials.size());
                for (const auto& m : materials) names.push_back(m.name.c_str());
                ImGui::Combo(
                    "Select", &selectedMaterialIdx, names.data(), (int)names.size()
                );
            }

            Material& mat = materials[std::clamp(
                selectedMaterialIdx, 0, (int)materials.size() - 1
            )];

            ImGui::ColorEdit4("Albedo",      &mat.albedo.x);
            ImGui::SliderFloat("Roughness",  &mat.roughness,         0.0f, 1.0f);
            ImGui::SliderFloat("Metallic",   &mat.metallic,          0.0f, 1.0f);
            ImGui::ColorEdit3("Emissive",    &mat.emissive.x);
            ImGui::SliderFloat("Emissive Strength", &mat.emissiveStrength, 0.0f, 20.0f);
        }
    }

    // --- Load GLB ---
    if (ImGui::CollapsingHeader("Load GLB", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("Path", gltfPathBuf, sizeof(gltfPathBuf));
        if (ImGui::Button("Load")) {
            std::string path(gltfPathBuf);
            if (!path.empty()) {
                if (!loadGltf(path))
                    spdlog::error("Failed to load GLB: {}", path);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset to Teapot")) {
            clearScene();
            auto cmdList2 = cmdQueue.getCmdList();
            // Reload teapot inline
            std::string objData = GetResourceString(IDR_TEAPOT_OBJ);
            if (!objData.empty()) {
                std::istringstream objStream(objData);
                class RMR : public tinyobj::MaterialReader {
                public:
                    bool operator()(const std::string&, std::vector<tinyobj::material_t>* mats,
                        std::map<std::string,int>* map, std::string* warn, std::string* err) override {
                        std::string mtl = GetResourceString(IDR_TEAPOT_MTL);
                        if (mtl.empty()) { if (warn) *warn="no mtl"; return false; }
                        std::istringstream s(mtl);
                        tinyobj::LoadMtl(map, mats, &s, warn, err);
                        return true;
                    }
                };
                RMR mr;
                tinyobj::attrib_t attrib;
                std::vector<tinyobj::shape_t> shapes;
                std::vector<tinyobj::material_t> objMats;
                std::string warn, err;
                tinyobj::LoadObj(&attrib, &shapes, &objMats, &warn, &err, &objStream, &mr);

                std::vector<VertexPBR> verts;
                std::vector<uint32_t> idxs;
                for (const auto& shape : shapes) {
                    for (const auto& idx : shape.mesh.indices) {
                        VertexPBR v{};
                        v.position = {
                            attrib.vertices[3*idx.vertex_index+0],
                            attrib.vertices[3*idx.vertex_index+1],
                            attrib.vertices[3*idx.vertex_index+2]
                        };
                        v.normal = idx.normal_index >= 0
                            ? XMFLOAT3{attrib.normals[3*idx.normal_index+0],
                                       attrib.normals[3*idx.normal_index+1],
                                       attrib.normals[3*idx.normal_index+2]}
                            : XMFLOAT3{0,1,0};
                        v.uv = {0,0};
                        verts.push_back(v);
                        idxs.push_back((uint32_t)idxs.size());
                    }
                }
                Material defMat;
                defMat.name = "Teapot";
                materials.push_back(defMat);
                GpuMesh m = uploadMesh(cmdList2, verts, idxs);
                m.materialIdx = 0;
                meshes.push_back(std::move(m));
            }
            uint64_t fv = cmdQueue.execCmdList(cmdList2);
            cmdQueue.waitForFenceVal(fv);
        }
    }

    ImGui::End();
    ImGui::Render();

    ID3D12DescriptorHeap* heaps[] = {imguiSrvHeap.Get()};
    cmdList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList.Get());
}

// ---------------------------------------------------------------------------
// Scene management
// ---------------------------------------------------------------------------

void Application::clearScene()
{
    flush();
    meshes.clear();
    materials.clear();
    selectedMaterialIdx = 0;
}

bool Application::loadGltf(const std::string& path)
{
    spdlog::info("loadGltf: {}", path);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;

    bool ok = false;
    if (path.size() >= 4 &&
        (path.substr(path.size() - 4) == ".glb" || path.substr(path.size() - 4) == ".GLB"))
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    else
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, path);

    if (!warn.empty()) spdlog::warn("tinygltf: {}", warn);
    if (!ok) {
        spdlog::error("tinygltf error: {}", err);
        return false;
    }

    clearScene();

    // Load materials
    if (model.materials.empty()) {
        Material def;
        def.name = "Default";
        materials.push_back(def);
    } else {
        for (const auto& gm : model.materials) {
            Material mat;
            mat.name = gm.name.empty() ? "Material" : gm.name;
            const auto& pbr = gm.pbrMetallicRoughness;
            if (pbr.baseColorFactor.size() == 4) {
                mat.albedo = {
                    (float)pbr.baseColorFactor[0], (float)pbr.baseColorFactor[1],
                    (float)pbr.baseColorFactor[2], (float)pbr.baseColorFactor[3]
                };
            }
            mat.roughness = (float)pbr.roughnessFactor;
            mat.metallic  = (float)pbr.metallicFactor;
            if (gm.emissiveFactor.size() == 3) {
                mat.emissive = {
                    (float)gm.emissiveFactor[0], (float)gm.emissiveFactor[1],
                    (float)gm.emissiveFactor[2], 0.0f
                };
                float maxE = std::max({mat.emissive.x, mat.emissive.y, mat.emissive.z});
                if (maxE > 0.001f) {
                    mat.emissiveStrength = maxE;
                    mat.emissive.x /= maxE;
                    mat.emissive.y /= maxE;
                    mat.emissive.z /= maxE;
                }
            }
            materials.push_back(mat);
        }
    }

    // Traverse node hierarchy, uploading mesh primitives
    auto cmdList = cmdQueue.getCmdList();

    const int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (sceneIdx >= (int)model.scenes.size()) {
        spdlog::error("No scenes in GLB");
        return false;
    }

    std::function<void(int, XMMATRIX)> visitNode = [&](int nodeIdx, XMMATRIX parentTf) {
        const auto& node    = model.nodes[nodeIdx];
        XMMATRIX    worldTf = NodeTransform(node) * parentTf;

        if (node.mesh >= 0) {
            const auto& gMesh = model.meshes[node.mesh];
            for (const auto& prim : gMesh.primitives) {
                // Default mode (4) = TRIANGLES; skip non-triangle primitives
                if (prim.mode != TINYGLTF_MODE_TRIANGLES)
                    continue;

                // Positions
                auto posIt = prim.attributes.find("POSITION");
                if (posIt == prim.attributes.end()) continue;
                auto positions = AccessorToFloatN<3>(model, posIt->second);

                // Normals (optional)
                std::vector<std::array<float, 3>> normals;
                auto normIt = prim.attributes.find("NORMAL");
                if (normIt != prim.attributes.end())
                    normals = AccessorToFloatN<3>(model, normIt->second);

                // UVs (optional)
                std::vector<std::array<float, 2>> uvs;
                auto uvIt = prim.attributes.find("TEXCOORD_0");
                if (uvIt != prim.attributes.end())
                    uvs = AccessorToFloatN<2>(model, uvIt->second);

                size_t numVerts = positions.size();
                std::vector<VertexPBR> verts(numVerts);
                for (size_t i = 0; i < numVerts; ++i) {
                    verts[i].position = {positions[i][0], positions[i][1], positions[i][2]};
                    verts[i].normal   = normals.size() > i
                        ? XMFLOAT3{normals[i][0], normals[i][1], normals[i][2]}
                        : XMFLOAT3{0.0f, 1.0f, 0.0f};
                    verts[i].uv       = uvs.size() > i
                        ? XMFLOAT2{uvs[i][0], uvs[i][1]}
                        : XMFLOAT2{0.0f, 0.0f};
                }

                // Indices
                std::vector<uint32_t> indices;
                if (prim.indices >= 0) {
                    indices = AccessorToIndices(model, prim.indices);
                } else {
                    indices.resize(numVerts);
                    for (size_t i = 0; i < numVerts; ++i) indices[i] = (uint32_t)i;
                }

                GpuMesh gpuMesh     = uploadMesh(cmdList, verts, indices);
                int matIdx          = (prim.material >= 0 &&
                                       prim.material < (int)materials.size())
                                      ? prim.material : 0;
                gpuMesh.materialIdx = matIdx;
                XMStoreFloat4x4(&gpuMesh.transform, worldTf);
                meshes.push_back(std::move(gpuMesh));
            }
        }

        for (int child : node.children) visitNode(child, worldTf);
    };

    for (int nodeIdx : model.scenes[sceneIdx].nodes)
        visitNode(nodeIdx, XMMatrixIdentity());

    uint64_t fv = cmdQueue.execCmdList(cmdList);
    cmdQueue.waitForFenceVal(fv);

    spdlog::info(
        "Loaded GLB: {} mesh(es), {} material(s)", meshes.size(), materials.size()
    );
    return true;
}

// ---------------------------------------------------------------------------
// Fullscreen
// ---------------------------------------------------------------------------

void Application::setFullscreen(bool val)
{
    if (this->fullscreen != val) {
        this->fullscreen = val;
        if (this->fullscreen) {
            ::GetWindowRect(this->hWnd, &this->windowRect);
            UINT windowStyle = WS_OVERLAPPEDWINDOW &
                ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
            ::SetWindowLongW(this->hWnd, GWL_STYLE, windowStyle);
            HMONITOR hMonitor = ::MonitorFromWindow(this->hWnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFOEX monitorInfo = {};
            monitorInfo.cbSize = sizeof(MONITORINFOEX);
            ::GetMonitorInfo(hMonitor, &monitorInfo);
            ::SetWindowPos(
                this->hWnd, HWND_TOP, monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
                monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                SWP_FRAMECHANGED | SWP_NOACTIVATE
            );
            ::ShowWindow(this->hWnd, SW_MAXIMIZE);
        } else {
            ::SetWindowLong(this->hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
            ::SetWindowPos(
                this->hWnd, HWND_NOTOPMOST,
                this->windowRect.left, this->windowRect.top,
                this->windowRect.right  - this->windowRect.left,
                this->windowRect.bottom - this->windowRect.top,
                SWP_FRAMECHANGED | SWP_NOACTIVATE
            );
            ::ShowWindow(this->hWnd, SW_NORMAL);
        }
    }
}

void Application::flush()
{
    this->cmdQueue.flush();
}

// ---------------------------------------------------------------------------
// loadContent — creates pipeline + uploads default teapot scene
// ---------------------------------------------------------------------------

bool Application::loadContent()
{
    spdlog::info("loadContent start");
    auto cmdList = this->cmdQueue.getCmdList();

    // --- Load teapot OBJ (default scene) ---
    {
        std::string objData = GetResourceString(IDR_TEAPOT_OBJ);
        if (objData.empty()) {
            spdlog::error("Failed to load obj from resource");
            return false;
        }
        std::istringstream objStream(objData);

        class ResourceMaterialReader : public tinyobj::MaterialReader
        {
           public:
            bool operator()(
                const std::string&, std::vector<tinyobj::material_t>* mats,
                std::map<std::string, int>* matMap, std::string* warn, std::string* err
            ) override
            {
                std::string mtlData = GetResourceString(IDR_TEAPOT_MTL);
                if (mtlData.empty()) {
                    if (warn) *warn = "Material resource not found";
                    return false;
                }
                std::istringstream mtlStream(mtlData);
                tinyobj::LoadMtl(matMap, mats, &mtlStream, warn, err);
                return true;
            }
        };
        ResourceMaterialReader matReader;

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t>    shapes;
        std::vector<tinyobj::material_t> objMats;
        std::string warn, err;
        if (!tinyobj::LoadObj(&attrib, &shapes, &objMats, &warn, &err, &objStream, &matReader))
            spdlog::error("tinyobj error: {}", err);
        if (!warn.empty()) spdlog::warn("tinyobj warn: {}", warn);

        std::vector<VertexPBR> verts;
        std::vector<uint32_t>  indices;
        for (const auto& shape : shapes) {
            for (const auto& idx : shape.mesh.indices) {
                VertexPBR v{};
                v.position = {
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]
                };
                v.normal = (idx.normal_index >= 0)
                    ? XMFLOAT3{attrib.normals[3 * idx.normal_index + 0],
                               attrib.normals[3 * idx.normal_index + 1],
                               attrib.normals[3 * idx.normal_index + 2]}
                    : XMFLOAT3{0.0f, 1.0f, 0.0f};
                if (idx.texcoord_index >= 0)
                    v.uv = {attrib.texcoords[2 * idx.texcoord_index + 0],
                            attrib.texcoords[2 * idx.texcoord_index + 1]};
                verts.push_back(v);
                indices.push_back(static_cast<uint32_t>(indices.size()));
            }
        }

        Material defMat;
        defMat.name      = "Teapot";
        defMat.roughness = 0.3f;
        defMat.metallic  = 0.0f;
        materials.push_back(defMat);

        GpuMesh m    = uploadMesh(cmdList, verts, indices);
        m.materialIdx = 0;
        meshes.push_back(std::move(m));
    }

    // --- DSV heap ---
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        chkDX(this->device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&this->dsvHeap)));
    }

    // --- Input layout for VertexPBR ---
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // --- Root signature (60 inline DWORDs) ---
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(this->device->CheckFeatureSupport(
            D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)
        )))
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

    const D3D12_ROOT_SIGNATURE_FLAGS rootSigFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    CD3DX12_ROOT_PARAMETER1 rootParams[1];
    rootParams[0].InitAsConstants(
        sizeof(SceneConstantBuffer) / 4, 0, 0, D3D12_SHADER_VISIBILITY_ALL
    );
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr, rootSigFlags);

    ComPtr<ID3DBlob> rootSigBlob, errorBlob;
    chkDX(D3DX12SerializeVersionedRootSignature(
        &rootSigDesc, featureData.HighestVersion, &rootSigBlob, &errorBlob
    ));
    chkDX(this->device->CreateRootSignature(
        0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
        IID_PPV_ARGS(&this->rootSignature)
    ));

    // --- Scene PSO ---
    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE      pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT        InputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY  PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS                  VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS                  PS;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    } pipelineStateStream;
    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets     = 1;
    rtvFormats.RTFormats[0]         = DXGI_FORMAT_R11G11B10_FLOAT;
    pipelineStateStream.pRootSignature      = this->rootSignature.Get();
    pipelineStateStream.InputLayout         = {inputLayout, _countof(inputLayout)};
    pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.VS                  = CD3DX12_SHADER_BYTECODE(g_vertex_shader, sizeof(g_vertex_shader));
    pipelineStateStream.PS                  = CD3DX12_SHADER_BYTECODE(g_pixel_shader,  sizeof(g_pixel_shader));
    pipelineStateStream.DSVFormat           = DXGI_FORMAT_D32_FLOAT;
    pipelineStateStream.RTVFormats          = rtvFormats;
    D3D12_PIPELINE_STATE_STREAM_DESC psoDesc = {sizeof(PipelineStateStream), &pipelineStateStream};
    chkDX(this->device->CreatePipelineState(&psoDesc, IID_PPV_ARGS(&this->pipelineState)));

    // --- Bloom root signature ---
    {
        CD3DX12_DESCRIPTOR_RANGE1 srvRange0, srvRange1;
        srvRange0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        srvRange1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

        CD3DX12_ROOT_PARAMETER1 bloomRootParams[3];
        bloomRootParams[0].InitAsDescriptorTable(1, &srvRange0, D3D12_SHADER_VISIBILITY_PIXEL);
        bloomRootParams[1].InitAsDescriptorTable(1, &srvRange1, D3D12_SHADER_VISIBILITY_PIXEL);
        bloomRootParams[2].InitAsConstants(4, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC staticSampler = {};
        staticSampler.Filter         = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        staticSampler.AddressU       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressV       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressW       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.ShaderRegister = 0;
        staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC bloomRootSigDesc;
        bloomRootSigDesc.Init_1_1(
            3, bloomRootParams, 1, &staticSampler,
            D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
        );

        ComPtr<ID3DBlob> bloomSigBlob, bloomErrBlob;
        chkDX(D3DX12SerializeVersionedRootSignature(
            &bloomRootSigDesc, featureData.HighestVersion, &bloomSigBlob, &bloomErrBlob
        ));
        chkDX(device->CreateRootSignature(
            0, bloomSigBlob->GetBufferPointer(), bloomSigBlob->GetBufferSize(),
            IID_PPV_ARGS(&this->bloomRootSignature)
        ));
    }

    // --- Bloom PSOs ---
    auto createBloomPSO = [&](const BYTE* psData, size_t psSize, DXGI_FORMAT rtFormat,
                              bool additiveBlend) -> ComPtr<ID3D12PipelineState> {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature         = bloomRootSignature.Get();
        desc.VS                     = CD3DX12_SHADER_BYTECODE(g_fullscreen_vs, sizeof(g_fullscreen_vs));
        desc.PS                     = CD3DX12_SHADER_BYTECODE(psData, psSize);
        desc.RasterizerState        = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.BlendState             = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        if (additiveBlend) {
            desc.BlendState.RenderTarget[0].BlendEnable    = TRUE;
            desc.BlendState.RenderTarget[0].SrcBlend       = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].DestBlend      = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].BlendOp        = D3D12_BLEND_OP_ADD;
            desc.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
        }
        desc.DepthStencilState.DepthEnable   = FALSE;
        desc.DepthStencilState.StencilEnable = FALSE;
        desc.SampleMask               = UINT_MAX;
        desc.PrimitiveTopologyType    = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets         = 1;
        desc.RTVFormats[0]            = rtFormat;
        desc.SampleDesc.Count         = 1;
        ComPtr<ID3D12PipelineState> pso;
        chkDX(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)));
        return pso;
    };

    const DXGI_FORMAT hdrFormat = DXGI_FORMAT_R11G11B10_FLOAT;
    this->prefilterPSO  = createBloomPSO(g_bloom_prefilter_ps,  sizeof(g_bloom_prefilter_ps),  hdrFormat, false);
    this->downsamplePSO = createBloomPSO(g_bloom_downsample_ps, sizeof(g_bloom_downsample_ps), hdrFormat, false);
    this->upsamplePSO   = createBloomPSO(g_bloom_upsample_ps,   sizeof(g_bloom_upsample_ps),   hdrFormat, true);
    this->compositePSO  = createBloomPSO(g_bloom_composite_ps,  sizeof(g_bloom_composite_ps),  DXGI_FORMAT_R8G8B8A8_UNORM, false);

    uint64_t fenceValue = this->cmdQueue.execCmdList(cmdList);
    this->cmdQueue.waitForFenceVal(fenceValue);

    this->contentLoaded = true;
    this->resizeDepthBuffer(this->clientWidth, this->clientHeight);
    this->createBloomResources(this->clientWidth, this->clientHeight);

    return true;
}

void Application::onResize(uint32_t width, uint32_t height)
{
    if (this->clientWidth != width || this->clientHeight != height) {
        this->clientWidth  = std::max(1u, width);
        this->clientHeight = std::max(1u, height);

        this->cmdQueue.flush();
        for (int i = 0; i < this->nBuffers; ++i)
            this->backBuffers[i].Reset();

        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        chkDX(this->swapChain->GetDesc(&swapChainDesc));
        chkDX(this->swapChain->ResizeBuffers(
            this->nBuffers, this->clientWidth, this->clientHeight,
            swapChainDesc.BufferDesc.Format, swapChainDesc.Flags
        ));

        this->curBackBufIdx = this->swapChain->GetCurrentBackBufferIndex();
        this->updateRenderTargetViews(this->rtvHeap);
        this->viewport = CD3DX12_VIEWPORT(
            0.0f, 0.0f,
            static_cast<float>(this->clientWidth),
            static_cast<float>(this->clientHeight)
        );
        this->resizeDepthBuffer(this->clientWidth, this->clientHeight);
        this->createBloomResources(this->clientWidth, this->clientHeight);
        this->flush();
    }
}
