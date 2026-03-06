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

static std::string GetResourceString(int resourceId)
{
    HRSRC hRes = FindResource(nullptr, MAKEINTRESOURCE(resourceId), RT_RCDATA);
    if (!hRes) {
        return "";
    }
    HGLOBAL hMem = LoadResource(nullptr, hRes);
    if (!hMem) {
        return "";
    }
    DWORD size = SizeofResource(nullptr, hRes);
    void* data = LockResource(hMem);
    if (!data) {
        return "";
    }
    return std::string(static_cast<const char*>(data), size);
}

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
    this->mouseID = inputManager.CreateDevice<gainput::InputDeviceMouse>();
    this->rawMouseID = inputManager.CreateDevice<gainput::InputDeviceMouse>(
        gainput::InputDevice::AutoIndex, gainput::InputDevice::DV_RAW
    );
    inputManager.SetDisplaySize(1, 1);
    this->inputMap.MapBool(Button::MoveForward, this->keyboardID, gainput::KeyW);
    this->inputMap.MapBool(Button::MoveForward, this->keyboardID, gainput::KeyUp);
    this->inputMap.MapBool(Button::MoveBackward, this->keyboardID, gainput::KeyS);
    this->inputMap.MapBool(Button::MoveBackward, this->keyboardID, gainput::KeyDown);
    this->inputMap.MapBool(Button::MoveLeft, this->keyboardID, gainput::KeyA);
    this->inputMap.MapBool(Button::MoveLeft, this->keyboardID, gainput::KeyLeft);
    this->inputMap.MapBool(Button::MoveRight, this->keyboardID, gainput::KeyD);
    this->inputMap.MapBool(Button::MoveRight, this->keyboardID, gainput::KeyRight);
    this->inputMap.MapBool(Button::LeftClick, this->mouseID, gainput::MouseButtonLeft);
    this->inputMap.MapBool(Button::RightClick, this->mouseID, gainput::MouseButtonRight);
    this->inputMap.MapFloat(Button::AxisX, this->mouseID, gainput::MouseAxisX);
    this->inputMap.MapFloat(Button::AxisY, this->mouseID, gainput::MouseAxisY);
    this->inputMap.MapFloat(Button::AxisDeltaX, this->mouseID, gainput::MouseAxisX);
    this->inputMap.MapFloat(Button::AxisDeltaY, this->mouseID, gainput::MouseAxisY);
    this->inputMap.MapBool(Button::ScrollUp, this->mouseID, gainput::MouseButtonWheelUp);
    this->inputMap.MapBool(Button::ScrollDown, this->mouseID, gainput::MouseButtonWheelDown);

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

void Application::transitionResource(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    ComPtr<ID3D12Resource> resource,
    D3D12_RESOURCE_STATES beforeState,
    D3D12_RESOURCE_STATES afterState
)
{
    CD3DX12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), beforeState, afterState);
    cmdList->ResourceBarrier(1, &barrier);
}

void Application::clearRTV(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv,
    FLOAT clearColor[4]
)
{
    cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
}

void Application::clearDepth(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv,
    FLOAT depth
)
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
    D3D12_RESOURCE_FLAGS flags
)
{
    const size_t bufSize = numElements * elementSize;
    {  // Create a committed resource for the GPU resource in a default heap
        const CD3DX12_HEAP_PROPERTIES pHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC pDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSize, flags);
        chkDX(device->CreateCommittedResource(
            &pHeapProperties, D3D12_HEAP_FLAG_NONE, &pDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(pDestinationResource)
        ));
    }

    if (bufferData) {
        // Create a committed resource for the upload
        const CD3DX12_HEAP_PROPERTIES pHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC pDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSize);
        chkDX(device->CreateCommittedResource(
            &pHeapProperties, D3D12_HEAP_FLAG_NONE, &pDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(pIntermediateResource)
        ));

        D3D12_SUBRESOURCE_DATA subresourceData = {};
        subresourceData.pData = bufferData;
        subresourceData.RowPitch = bufSize;
        subresourceData.SlicePitch = subresourceData.RowPitch;

        ::UpdateSubresources(
            cmdList.Get(), *pDestinationResource, *pIntermediateResource, 0, 0, 1, &subresourceData
        );
    }
}

void Application::resizeDepthBuffer(uint32_t width, uint32_t height)
{
    assert(this->contentLoaded);

    this->flush();
    width = std::max(1u, width);
    height = std::max(1u, height);

    D3D12_CLEAR_VALUE optimizedClearValue = {};
    optimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    optimizedClearValue.DepthStencil = { 1.0f, 0 };
    const CD3DX12_HEAP_PROPERTIES pHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    const CD3DX12_RESOURCE_DESC pDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D32_FLOAT, width, height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
    );
    chkDX(device->CreateCommittedResource(
        &pHeapProperties, D3D12_HEAP_FLAG_NONE, &pDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &optimizedClearValue, IID_PPV_ARGS(&this->depthBuffer)
    ));

    // Update depth-stencil view
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    device->CreateDepthStencilView(
        this->depthBuffer.Get(), &dsvDesc, this->dsvHeap->GetCPUDescriptorHandleForHeapStart()
    );
}

void Application::createBloomResources(uint32_t width, uint32_t height)
{
    width = std::max(1u, width);
    height = std::max(1u, height);

    const DXGI_FORMAT hdrFormat = DXGI_FORMAT_R11G11B10_FLOAT;

    // Create HDR scene render target
    {
        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = hdrFormat;
        clearVal.Color[0] = 0.4f;
        clearVal.Color[1] = 0.6f;
        clearVal.Color[2] = 0.9f;
        clearVal.Color[3] = 1.0f;
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            hdrFormat, width, height, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
        );
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clearVal,
            IID_PPV_ARGS(&this->hdrRenderTarget)
        ));
    }

    // Create individual bloom mip textures
    uint32_t mipW = std::max(1u, width / 2);
    uint32_t mipH = std::max(1u, height / 2);
    for (uint32_t i = 0; i < bloomMipCount; ++i) {
        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = hdrFormat;
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            hdrFormat, mipW, mipH, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
        );
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clearVal,
            IID_PPV_ARGS(&this->bloomMips[i])
        ));
        mipW = std::max(1u, mipW / 2);
        mipH = std::max(1u, mipH / 2);
    }

    // Create bloom RTV heap: 1 HDR RT + bloomMipCount mip RTVs
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 1 + bloomMipCount;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        chkDX(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&this->bloomRtvHeap)));
    }

    // Create shader-visible SRV heap: 1 HDR scene + bloomMipCount bloom mip SRVs
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 1 + bloomMipCount;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        chkDX(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&this->srvHeap)));
    }
    this->srvDescSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    UINT rtvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(bloomRtvHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(srvHeap->GetCPUDescriptorHandleForHeapStart());

    // HDR RT: RTV at index 0, SRV at index 0
    device->CreateRenderTargetView(hdrRenderTarget.Get(), nullptr, rtvHandle);
    rtvHandle.Offset(rtvInc);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = hdrFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(hdrRenderTarget.Get(), &srvDesc, srvHandle);
    srvHandle.Offset(srvDescSize);

    // Bloom mips: RTVs at index 1..5, SRVs at index 1..5
    for (uint32_t i = 0; i < bloomMipCount; ++i) {
        device->CreateRenderTargetView(bloomMips[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(rtvInc);

        device->CreateShaderResourceView(bloomMips[i].Get(), &srvDesc, srvHandle);
        srvHandle.Offset(srvDescSize);
    }
}

// Create swap chain which describes the sequence of buffers used for rendering
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
    swapChainDesc.Width = this->clientWidth;
    swapChainDesc.Height = this->clientHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc = { 1, 0 };
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = this->nBuffers;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    // It is recommended to always allow tearing if tearing support is
    // available.
    swapChainDesc.Flags = this->tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    ComPtr<IDXGISwapChain1> swapChain1;
    chkDX(dxgiFactory4->CreateSwapChainForHwnd(
        this->cmdQueue.queue.Get(), this->hWnd, &swapChainDesc, nullptr, nullptr, &swapChain1
    ));

    // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
    // will be handled manually.
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
    desc.Type = type;

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

void Application::update()
{
    [[maybe_unused]] static double elapsedSeconds = 0.0;
    static std::chrono::high_resolution_clock clock;
    static auto t0 = clock.now();

    // Timing
    auto t1 = clock.now();
    auto deltaTime = t1 - t0;
    t0 = t1;
    [[maybe_unused]] const double dt = deltaTime.count() * 1e-9;
    elapsedSeconds += dt;

    const float w = static_cast<float>(this->clientWidth);
    const float h = static_cast<float>(this->clientHeight);

    // Handle input
    this->mouseDelta = { this->inputMap.GetFloatDelta(Button::AxisDeltaX),
                         this->inputMap.GetFloatDelta(Button::AxisDeltaY) };
    this->mousePos = { this->inputMap.GetFloat(Button::AxisX),
                       this->inputMap.GetFloat(Button::AxisY) };

    // Camera controls
    this->matModel = XMMatrixIdentity();
    if (this->inputMap.GetBool(Button::LeftClick)) {
        this->cam.pitch += (this->mouseDelta.y / w) * 180_deg;
        this->cam.yaw -= (this->mouseDelta.x / w) * 360_deg;
        this->cam.pitch = std::clamp(this->cam.pitch, -89.9_deg, 89.9_deg);
    }
    if (this->inputMap.GetBool(Button::RightClick)) {
        this->cam.radius += this->mouseDelta.y / w;
    }
    this->cam.aspectRatio = w / h;
    if (this->inputMap.GetBoolWasDown(Button::ScrollUp)) {
        this->cam.radius *= 1.25f;
    }
    if (this->inputMap.GetBoolWasDown(Button::ScrollDown)) {
        this->cam.radius *= 0.8f;
    }
}

void Application::render()
{
    auto backBuffer = this->backBuffers[this->curBackBufIdx];
    auto cmdList = this->cmdQueue.getCmdList();

    // --- Scene pass: render to HDR render target ---
    {
        FLOAT clearColor[] = { bgColor[0], bgColor[1], bgColor[2], 1.0f };
        CD3DX12_CPU_DESCRIPTOR_HANDLE hdrRtv(bloomRtvHeap->GetCPUDescriptorHandleForHeapStart());
        this->clearRTV(cmdList, hdrRtv, clearColor);
        auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
        this->clearDepth(cmdList, dsv);

        cmdList->SetPipelineState(this->pipelineState.Get());
        cmdList->SetGraphicsRootSignature(this->rootSignature.Get());

        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->IASetVertexBuffers(0, 1, &this->vertexBufferView);
        cmdList->IASetIndexBuffer(&this->indexBufferView);
        cmdList->RSSetViewports(1, &this->viewport);
        cmdList->RSSetScissorRects(1, &this->scissorRect);

        cmdList->OMSetRenderTargets(1, &hdrRtv, true, &dsv);

        SceneConstantBuffer scb = {};
        scb.model = this->matModel;
        scb.viewProj = this->cam.view() * this->cam.proj();

        float camX = this->cam.radius * cos(this->cam.pitch) * cos(this->cam.yaw);
        float camY = this->cam.radius * sin(this->cam.pitch);
        float camZ = this->cam.radius * cos(this->cam.pitch) * sin(this->cam.yaw);
        scb.cameraPos = XMFLOAT4(camX, camY, camZ, 1.0f);

        scb.lightPos = XMFLOAT4(10.0f, 15.0f, -10.0f, 1.0f);
        scb.lightColor = XMFLOAT4(lightBrightness, lightBrightness, lightBrightness, 1.0f);
        scb.ambientColor = XMFLOAT4(
            bgColor[0] * ambientBrightness, bgColor[1] * ambientBrightness,
            bgColor[2] * ambientBrightness, 1.0f
        );

        cmdList->SetGraphicsRoot32BitConstants(0, sizeof(SceneConstantBuffer) / 4, &scb, 0);
        cmdList->DrawIndexedInstanced(this->numIndices, 1, 0, 0, 0);
    }

    // --- Bloom post-process (prefilter → downsample → upsample → composite to swap chain) ---
    this->renderBloom(cmdList);

    // --- ImGui overlay (renders to swap chain back buffer, which is already in RENDER_TARGET) ---
    this->renderImGui(cmdList);

    // --- Present ---
    {
        this->transitionResource(
            cmdList, backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT
        );

        // Transition bloom resources back to RENDER_TARGET for next frame
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

        UINT syncInterval = this->vsync ? 1 : 0;
        UINT presentFlags = this->tearingSupported && !this->vsync ? DXGI_PRESENT_ALLOW_TEARING : 0;
        chkDX(this->swapChain->Present(syncInterval, presentFlags));
        this->curBackBufIdx = this->swapChain->GetCurrentBackBufferIndex();

        this->cmdQueue.waitForFenceVal(this->frameFenceValues[this->curBackBufIdx]);

        if (this->testMode) {
            this->frameCount++;
            if (this->frameCount == 10) {
                spdlog::info("Saving screenshot and exiting...");
                HRESULT hr = DirectX::SaveWICTextureToFile(
                    this->cmdQueue.queue.Get(), backBuffer.Get(), GUID_ContainerFormatPng,
                    L"screenshot.png", D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT
                );
                if (FAILED(hr)) {
                    spdlog::error(
                        "Failed to save screenshot! HRESULT: {:#010x}", static_cast<uint32_t>(hr)
                    );
                }
                Window::get()->doExit = true;
            }
        }
    }
}

void Application::renderBloom(ComPtr<ID3D12GraphicsCommandList2> cmdList)
{
    ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootSignature(bloomRootSignature.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    UINT rtvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto bloomRtvBase = bloomRtvHeap->GetCPUDescriptorHandleForHeapStart();
    auto srvGpuBase = srvHeap->GetGPUDescriptorHandleForHeapStart();

    auto getRtv = [&](uint32_t idx) -> D3D12_CPU_DESCRIPTOR_HANDLE {
        return CD3DX12_CPU_DESCRIPTOR_HANDLE(bloomRtvBase, idx, rtvInc);
    };
    auto getSrvGpu = [&](uint32_t idx) -> D3D12_GPU_DESCRIPTOR_HANDLE {
        return CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuBase, idx, srvDescSize);
    };

    // Compute mip dimensions
    uint32_t mipW[bloomMipCount], mipH[bloomMipCount];
    mipW[0] = std::max(1u, clientWidth / 2);
    mipH[0] = std::max(1u, clientHeight / 2);
    for (uint32_t i = 1; i < bloomMipCount; ++i) {
        mipW[i] = std::max(1u, mipW[i - 1] / 2);
        mipH[i] = std::max(1u, mipH[i - 1] / 2);
    }

    struct BloomCB
    {
        float texelSizeX, texelSizeY;
        float param0, param1;
    };

    // --- Prefilter: HDR scene → bloom mip 0 ---
    transitionResource(
        cmdList, hdrRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    cmdList->SetPipelineState(prefilterPSO.Get());
    cmdList->SetGraphicsRootDescriptorTable(0, getSrvGpu(0));  // HDR scene
    auto mip0Rtv = getRtv(1);  // bloom mip 0
    cmdList->OMSetRenderTargets(1, &mip0Rtv, false, nullptr);
    CD3DX12_VIEWPORT vp(0.0f, 0.0f, (float)mipW[0], (float)mipH[0]);
    D3D12_RECT sr = { 0, 0, (LONG)mipW[0], (LONG)mipH[0] };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sr);
    BloomCB cb = { 1.0f / clientWidth, 1.0f / clientHeight, bloomThreshold, 0.5f };
    cmdList->SetGraphicsRoot32BitConstants(2, 4, &cb, 0);
    cmdList->DrawInstanced(3, 1, 0, 0);

    // --- Downsample chain: mip N → mip N+1 ---
    cmdList->SetPipelineState(downsamplePSO.Get());
    for (uint32_t i = 0; i < bloomMipCount - 1; ++i) {
        transitionResource(
            cmdList, bloomMips[i], D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );

        cmdList->SetGraphicsRootDescriptorTable(0, getSrvGpu(1 + i));  // bloom mip i
        auto rtv = getRtv(2 + i);  // bloom mip i+1
        cmdList->OMSetRenderTargets(1, &rtv, false, nullptr);
        vp = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)mipW[i + 1], (float)mipH[i + 1]);
        sr = { 0, 0, (LONG)mipW[i + 1], (LONG)mipH[i + 1] };
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sr);
        cb = { 1.0f / mipW[i], 1.0f / mipH[i], 0, 0 };
        cmdList->SetGraphicsRoot32BitConstants(2, 4, &cb, 0);
        cmdList->DrawInstanced(3, 1, 0, 0);
    }

    // --- Upsample chain: mip N+1 → mip N (additive) ---
    cmdList->SetPipelineState(upsamplePSO.Get());
    for (int i = bloomMipCount - 2; i >= 0; --i) {
        // Source: mip i+1 (needs to be SRV)
        transitionResource(
            cmdList, bloomMips[i + 1], D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        // Destination: mip i (needs to be RT — it still has downsample data, additive blend accumulates)
        transitionResource(
            cmdList, bloomMips[i], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );

        cmdList->SetGraphicsRootDescriptorTable(0, getSrvGpu(2 + i));  // bloom mip i+1
        auto rtv = getRtv(1 + i);  // bloom mip i
        cmdList->OMSetRenderTargets(1, &rtv, false, nullptr);
        vp = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)mipW[i], (float)mipH[i]);
        sr = { 0, 0, (LONG)mipW[i], (LONG)mipH[i] };
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sr);
        cb = { 1.0f / mipW[i + 1], 1.0f / mipH[i + 1], 1.0f, 0 };
        cmdList->SetGraphicsRoot32BitConstants(2, 4, &cb, 0);
        cmdList->DrawInstanced(3, 1, 0, 0);
    }

    // --- Composite: HDR scene + bloom mip 0 → swap chain ---
    // HDR RT already in PIXEL_SHADER_RESOURCE from prefilter step
    transitionResource(
        cmdList, bloomMips[0], D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    auto backBuffer = backBuffers[curBackBufIdx];
    transitionResource(
        cmdList, backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
    );

    cmdList->SetPipelineState(compositePSO.Get());
    cmdList->SetGraphicsRootDescriptorTable(0, getSrvGpu(0));  // HDR scene
    cmdList->SetGraphicsRootDescriptorTable(1, getSrvGpu(1));  // bloom mip 0
    CD3DX12_CPU_DESCRIPTOR_HANDLE backBufRtv(
        rtvHeap->GetCPUDescriptorHandleForHeapStart(), curBackBufIdx, rtvDescSize
    );
    cmdList->OMSetRenderTargets(1, &backBufRtv, false, nullptr);
    vp = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)clientWidth, (float)clientHeight);
    sr = { 0, 0, (LONG)clientWidth, (LONG)clientHeight };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sr);
    struct { float a, b, c; uint32_t d; } compositeCB = {
        0, 0, bloomIntensity, (uint32_t)tonemapMode
    };
    cmdList->SetGraphicsRoot32BitConstants(2, 4, &compositeCB, 0);
    cmdList->DrawInstanced(3, 1, 0, 0);
}

void Application::initImGui()
{
    // Create SRV descriptor heap for ImGui
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 16;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        chkDX(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&imguiSrvHeap)));
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hWnd);

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = device.Get();
    initInfo.CommandQueue = cmdQueue.queue.Get();
    initInfo.NumFramesInFlight = nBuffers;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = imguiSrvHeap.Get();
    initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info,
                                       D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                                       D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
        // Simple bump allocator — ImGui only allocates a few descriptors (font texture)
        auto* app = static_cast<Application*>(info->UserData);
        UINT inc =
            app->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        *outCpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            app->imguiSrvHeap->GetCPUDescriptorHandleForHeapStart(), app->imguiSrvNextIndex, inc
        );
        *outGpu = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            app->imguiSrvHeap->GetGPUDescriptorHandleForHeapStart(), app->imguiSrvNextIndex, inc
        );
        app->imguiSrvNextIndex++;
    };
    initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE,
                                      D3D12_GPU_DESCRIPTOR_HANDLE) {
        // No-op: descriptors freed when heap is destroyed
    };
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

    if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Threshold", &bloomThreshold, 0.0f, 3.0f);
        ImGui::SliderFloat("Intensity", &bloomIntensity, 0.0f, 5.0f);
    }

    if (ImGui::CollapsingHeader("Tonemapping", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* tonemappers[] = { "ACES Filmic", "AgX", "AgX Punchy", "Gran Turismo", "PBR Neutral" };
        ImGui::Combo("Tonemapper", &tonemapMode, tonemappers, IM_ARRAYSIZE(tonemappers));
    }

    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Background", bgColor);
        ImGui::SliderFloat("Light Brightness", &lightBrightness, 0.0f, 20.0f);
        ImGui::SliderFloat("Ambient Brightness", &ambientBrightness, 0.0f, 2.0f);
    }

    ImGui::End();
    ImGui::Render();

    ID3D12DescriptorHeap* heaps[] = { imguiSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList.Get());
}

void Application::setFullscreen(bool val)
{
    if (this->fullscreen != val) {
        this->fullscreen = val;

        if (this->fullscreen) {
            // Store the current window dimensions so they can be restored
            //  when switching out of fullscreen state.
            ::GetWindowRect(this->hWnd, &this->windowRect);
            // Set the window style to a borderless window so the client area
            // fills
            //  the entire screen.
            UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                                                       WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

            ::SetWindowLongW(this->hWnd, GWL_STYLE, windowStyle);
            // Query the name of the nearest display device for the window.
            //  This is required to set the fullscreen dimensions of the window
            //  when using a multi-monitor setup.
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
            // Restore all the window decorators.
            ::SetWindowLong(this->hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);

            ::SetWindowPos(
                this->hWnd, HWND_NOTOPMOST, this->windowRect.left, this->windowRect.top,
                this->windowRect.right - this->windowRect.left,
                this->windowRect.bottom - this->windowRect.top, SWP_FRAMECHANGED | SWP_NOACTIVATE
            );

            ::ShowWindow(this->hWnd, SW_NORMAL);
        }
    }
}

void Application::flush()
{
    this->cmdQueue.flush();
}

bool Application::loadContent()
{
    spdlog::info("loadContent start");
    auto cmdList = this->cmdQueue.getCmdList();

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
            const std::string& matId,
            std::vector<tinyobj::material_t>* materials,
            std::map<std::string, int>* matMap,
            std::string* warn,
            std::string* err
        ) override
        {
            std::string mtlData = GetResourceString(IDR_TEAPOT_MTL);
            if (mtlData.empty()) {
                if (warn) {
                    *warn = "Material resource not found";
                }
                return false;
            }
            std::istringstream mtlStream(mtlData);
            tinyobj::LoadMtl(matMap, materials, &mtlStream, warn, err);
            return true;
        }
    };
    ResourceMaterialReader matReader;

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, &objStream, &matReader)) {
        spdlog::error("Failed to load obj: {}", err);
        return false;
    }
    if (!warn.empty()) {
        spdlog::warn("tinyobjloader warn: {}", warn);
    }

    std::vector<VertexPosNormalColor> vertices;
    std::vector<uint32_t> indices;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            VertexPosNormalColor vertex{};
            vertex.position = { attrib.vertices[3 * index.vertex_index + 0],
                                attrib.vertices[3 * index.vertex_index + 1],
                                attrib.vertices[3 * index.vertex_index + 2] };
            if (index.normal_index >= 0) {
                vertex.normal = { attrib.normals[3 * index.normal_index + 0],
                                  attrib.normals[3 * index.normal_index + 1],
                                  attrib.normals[3 * index.normal_index + 2] };
            } else {
                vertex.normal = { 0.0f, 1.0f, 0.0f };
            }
            // Set some default color
            vertex.color = { 0.8f, 0.8f, 0.8f };
            vertices.push_back(vertex);
            indices.push_back(static_cast<uint32_t>(indices.size()));
        }
    }
    this->numIndices = static_cast<uint32_t>(indices.size());

    spdlog::info("Uploading vertex buffer");
    // Upload vertex buffer data
    ComPtr<ID3D12Resource> intermediateVertexBuffer;
    this->updateBufferResource(
        cmdList, &this->vertexBuffer, &intermediateVertexBuffer, vertices.size(),
        sizeof(VertexPosNormalColor), vertices.data()
    );

    // Create the vertex buffer view
    this->vertexBufferView.BufferLocation = this->vertexBuffer->GetGPUVirtualAddress();
    this->vertexBufferView.SizeInBytes =
        static_cast<UINT>(vertices.size() * sizeof(VertexPosNormalColor));
    this->vertexBufferView.StrideInBytes = sizeof(VertexPosNormalColor);

    spdlog::info("Uploading index buffer");
    // Upload index buffer data
    ComPtr<ID3D12Resource> intermediateIndexBuffer;
    this->updateBufferResource(
        cmdList, &this->indexBuffer, &intermediateIndexBuffer, indices.size(), sizeof(uint32_t),
        indices.data()
    );

    // Create the index buffer view
    this->indexBufferView.BufferLocation = this->indexBuffer->GetGPUVirtualAddress();
    this->indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    this->indexBufferView.SizeInBytes = static_cast<UINT>(indices.size() * sizeof(uint32_t));

    spdlog::info("Creating dsvHeap");
    // Create the descriptor heap for the depth-stencil view
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    chkDX(this->device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&this->dsvHeap)));

    spdlog::info("Creating vertex input layout");
    // is structured
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // Specify a root signature
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(this->device->CheckFeatureSupport(
            D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)
        ))) {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }
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

    // Serialize and create the root signature
    ComPtr<ID3DBlob> rootSigBlob, errorBlob;
    chkDX(D3DX12SerializeVersionedRootSignature(
        &rootSigDesc, featureData.HighestVersion, &rootSigBlob, &errorBlob
    ));
    chkDX(this->device->CreateRootSignature(
        0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
        IID_PPV_ARGS(&this->rootSignature)
    ));

    // Create the pipeline state object
    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS PS;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    } pipelineStateStream;
    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R11G11B10_FLOAT;  // HDR render target
    pipelineStateStream.pRootSignature = this->rootSignature.Get();
    pipelineStateStream.InputLayout = { inputLayout, _countof(inputLayout) };
    pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(g_vertex_shader, sizeof(g_vertex_shader));
    pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(g_pixel_shader, sizeof(g_pixel_shader));
    pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pipelineStateStream.RTVFormats = rtvFormats;
    D3D12_PIPELINE_STATE_STREAM_DESC psoDesc = { sizeof(PipelineStateStream),
                                                 &pipelineStateStream };
    chkDX(this->device->CreatePipelineState(&psoDesc, IID_PPV_ARGS(&this->pipelineState)));

    // --- Bloom root signature ---
    {
        CD3DX12_DESCRIPTOR_RANGE1 srvRange0;
        srvRange0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);  // t0
        CD3DX12_DESCRIPTOR_RANGE1 srvRange1;
        srvRange1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);  // t1

        CD3DX12_ROOT_PARAMETER1 bloomRootParams[3];
        bloomRootParams[0].InitAsDescriptorTable(1, &srvRange0, D3D12_SHADER_VISIBILITY_PIXEL);
        bloomRootParams[1].InitAsDescriptorTable(1, &srvRange1, D3D12_SHADER_VISIBILITY_PIXEL);
        bloomRootParams[2].InitAsConstants(4, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);  // b0: 4 floats

        D3D12_STATIC_SAMPLER_DESC staticSampler = {};
        staticSampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
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
        desc.pRootSignature = bloomRootSignature.Get();
        desc.VS = CD3DX12_SHADER_BYTECODE(g_fullscreen_vs, sizeof(g_fullscreen_vs));
        desc.PS = CD3DX12_SHADER_BYTECODE(psData, psSize);
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        if (additiveBlend) {
            desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
            desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        }
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.StencilEnable = FALSE;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = rtFormat;
        desc.SampleDesc.Count = 1;

        ComPtr<ID3D12PipelineState> pso;
        chkDX(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)));
        return pso;
    };

    const DXGI_FORMAT hdrFormat = DXGI_FORMAT_R11G11B10_FLOAT;
    this->prefilterPSO =
        createBloomPSO(g_bloom_prefilter_ps, sizeof(g_bloom_prefilter_ps), hdrFormat, false);
    this->downsamplePSO =
        createBloomPSO(g_bloom_downsample_ps, sizeof(g_bloom_downsample_ps), hdrFormat, false);
    this->upsamplePSO =
        createBloomPSO(g_bloom_upsample_ps, sizeof(g_bloom_upsample_ps), hdrFormat, true);
    this->compositePSO = createBloomPSO(
        g_bloom_composite_ps, sizeof(g_bloom_composite_ps), DXGI_FORMAT_R8G8B8A8_UNORM, false
    );

    // Execute the created command list on the GPU
    uint64_t fenceValue = this->cmdQueue.execCmdList(cmdList);
    this->cmdQueue.waitForFenceVal(fenceValue);

    // Resize / create the depth buffer
    this->contentLoaded = true;
    this->resizeDepthBuffer(this->clientWidth, this->clientHeight);

    // Create HDR render target and bloom mip chain
    this->createBloomResources(this->clientWidth, this->clientHeight);

    return this->contentLoaded;
}

void Application::onResize(uint32_t width, uint32_t height)
{
    if (this->clientWidth != width || this->clientHeight != height) {
        // Don't allow 0 size swap chain back buffers.
        this->clientWidth = std::max(1u, width);
        this->clientHeight = std::max(1u, height);

        // Flush the GPU queue to make sure the swap chain's back buffers
        //  are not being referenced by an in-flight command list.
        this->cmdQueue.flush();
        for (int i = 0; i < this->nBuffers; ++i) {
            // Any references to the back buffers must be released
            //  before the swap chain can be resized.
            this->backBuffers[i].Reset();
        }
        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        chkDX(this->swapChain->GetDesc(&swapChainDesc));
        chkDX(this->swapChain->ResizeBuffers(
            this->nBuffers, this->clientWidth, this->clientHeight, swapChainDesc.BufferDesc.Format,
            swapChainDesc.Flags
        ));
        spdlog::debug(
            "resized buffers in swap chain to ({},{})", this->clientWidth, this->clientHeight
        );

        this->curBackBufIdx = this->swapChain->GetCurrentBackBufferIndex();

        this->updateRenderTargetViews(this->rtvHeap);

        this->viewport = CD3DX12_VIEWPORT(
            0.0f, 0.0f, static_cast<float>(this->clientWidth),
            static_cast<float>(this->clientHeight)
        );
        this->resizeDepthBuffer(this->clientWidth, this->clientHeight);
        this->createBloomResources(this->clientWidth, this->clientHeight);

        this->flush();
    }
}
