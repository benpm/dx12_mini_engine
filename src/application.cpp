module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <d3d12.h>
#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <flecs.h>
#include <gainput/gainput.h>
#include <ScreenGrab.h>
#include <spdlog/spdlog.h>
#include <wincodec.h>
#include <Windows.h>
#include <wrl.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>
#include "d3dx12_clean.h"
#include "resource.h"

module application;

using Microsoft::WRL::ComPtr;
using namespace DirectX;

static vec4 hslToLinear(float hue, float sat, float light)
{
    float c = (1.0f - std::abs(2.0f * light - 1.0f)) * sat;
    float x = c * (1.0f - std::abs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    float m = light - c * 0.5f;
    float r, g, b;
    int seg = static_cast<int>(hue / 60.0f) % 6;
    switch (seg) {
        case 0:
            r = c;
            g = x;
            b = 0;
            break;
        case 1:
            r = x;
            g = c;
            b = 0;
            break;
        case 2:
            r = 0;
            g = c;
            b = x;
            break;
        case 3:
            r = 0;
            g = x;
            b = c;
            break;
        case 4:
            r = x;
            g = 0;
            b = c;
            break;
        default:
            r = c;
            g = 0;
            b = x;
            break;
    }
    auto lin = [](float v) { return std::pow(v, 2.2f); };
    return { lin(r + m), lin(g + m), lin(b + m), 1.0f };
}

#ifndef DXC_PATH
    #define DXC_PATH ""
#endif
#ifndef SHADER_SRC_DIR
    #define SHADER_SRC_DIR ""
#endif

// ---------------------------------------------------------------------------
// Application constructor / destructor
// ---------------------------------------------------------------------------

Application::Application() : inputMap(inputManager, "input_map")
{
    spdlog::info("Application constructor start");

    // Populate from Window singleton (replaces old registerApp)
    auto* win = Window::get();
    this->hWnd = win->hWnd;
    this->device = win->device;
    this->tearingSupported = win->tearingSupported;
    this->clientWidth = win->width;
    this->clientHeight = win->height;

    // Register callbacks so WndProc can drive the render loop without importing application
    win->callbackCtx = this;
    win->onPaintFn = [](void* ctx) {
        auto* a = static_cast<Application*>(ctx);
        a->update();
        a->render();
    };
    win->onResizeFn = [](void* ctx, uint32_t w, uint32_t h) {
        static_cast<Application*>(ctx)->onResize(w, h);
    };

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
    this->imguiLayer.init(
        hWnd, device.Get(), cmdQueue.queue.Get(), nBuffers, DXGI_FORMAT_R8G8B8A8_UNORM
    );
    this->isInitialized = true;
    Window::get()->isReady = true;
}

Application::~Application()
{
    auto* win = Window::get();
    win->isReady = false;
    win->onPaintFn = nullptr;
    win->onResizeFn = nullptr;
    win->callbackCtx = nullptr;

    this->flush();
    this->imguiLayer.shutdown();
}

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------

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
    cmdList->ClearDepthStencilView(
        dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth, 0, 0, nullptr
    );
}

// ---------------------------------------------------------------------------
// Depth buffer
// ---------------------------------------------------------------------------

void Application::resizeDepthBuffer(uint32_t width, uint32_t height)
{
    assert(this->contentLoaded);
    this->flush();
    width = std::max(1u, width);
    height = std::max(1u, height);

    D3D12_CLEAR_VALUE optimizedClearValue = {};
    optimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    optimizedClearValue.DepthStencil = { 1.0f, 0 };
    const CD3DX12_HEAP_PROPERTIES pHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    const CD3DX12_RESOURCE_DESC pDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32G8X24_TYPELESS, width, height, 1, 0, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
    );
    chkDX(device->CreateCommittedResource(
        &pHeapProperties, D3D12_HEAP_FLAG_NONE, &pDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &optimizedClearValue, IID_PPV_ARGS(&this->depthBuffer)
    ));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
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
    swapChainDesc.Flags = this->tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

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
        rtvHandle.Offset(static_cast<INT>(rtvDescriptorSize));
    }
}

// ---------------------------------------------------------------------------
// Update / render
// ---------------------------------------------------------------------------

void Application::update()
{
    // Process deferred scene loads (set from ImGui, safe here before render commands)
    if (pendingResetToTeapot) {
        pendingResetToTeapot = false;
        scene.clearScene(cmdQueue);
        scene.loadTeapot(device.Get(), cmdQueue);
        spawningStopped = false;
        recentFrameHead = 0;
        recentFrameMs[0] = recentFrameMs[1] = recentFrameMs[2] = 0.0f;
    }
    if (!pendingGltfPath.empty()) {
        std::string path = std::move(pendingGltfPath);
        pendingGltfPath.clear();
        spawningStopped = false;
        recentFrameHead = 0;
        recentFrameMs[0] = recentFrameMs[1] = recentFrameMs[2] = 0.0f;
        if (!scene.loadGltf(path, device.Get(), cmdQueue)) {
            spdlog::error("Failed to load GLB: {}", path);
        }
    }

    // Deferred scene file load/save
    if (!pendingSceneLoad.empty()) {
        std::string path = std::move(pendingSceneLoad);
        pendingSceneLoad.clear();
        SceneFileData data;
        if (loadSceneFile(path, data)) {
            scene.clearScene(cmdQueue);
            scene.loadTeapot(device.Get(), cmdQueue);
            for (const auto& entry : std::filesystem::directory_iterator(MODELS_DIR)) {
                if (entry.path().extension() == ".glb") {
                    scene.loadGltf(entry.path().string(), device.Get(), cmdQueue, true);
                }
            }
            applySceneData(data);
            spawningStopped = data.spawning.stopped;
            recentFrameHead = 0;
            recentFrameMs[0] = recentFrameMs[1] = recentFrameMs[2] = 0.0f;
        }
    }
    if (!pendingSceneSave.empty()) {
        std::string path = std::move(pendingSceneSave);
        pendingSceneSave.clear();
        auto data = extractSceneData();
        saveSceneFile(path, data);
    }

    static std::chrono::high_resolution_clock clock;
    static auto t0 = clock.now();

    auto t1 = clock.now();
    auto deltaTime = t1 - t0;
    t0 = t1;
    const float dt = static_cast<float>(static_cast<double>(deltaTime.count()) * 1e-9);

    lightTime += dt * std::max(0.0f, lightAnimationSpeed);
    lastFrameMs = dt * 1000.0f;
    recentFrameMs[recentFrameHead % 3] = lastFrameMs;
    ++recentFrameHead;
    if (autoStopSpawning && !spawningStopped && recentFrameHead >= 3 &&
        recentFrameMs[0] > spawnStopFrameMs && recentFrameMs[1] > spawnStopFrameMs &&
        recentFrameMs[2] > spawnStopFrameMs) {
        spawningStopped = true;
    }

    // Shader hot reload
    if (shaderCompiler.poll(dt)) {
        flush();
        bool sceneChanged =
            shaderCompiler.wasRecompiled(sceneVSIdx) || shaderCompiler.wasRecompiled(scenePSIdx);
        bool outlineChanged = shaderCompiler.wasRecompiled(outlineVSIdx) ||
                              shaderCompiler.wasRecompiled(outlinePSIdx);
        bool bloomChanged = shaderCompiler.wasRecompiled(bloomFsVsIdx) ||
                            shaderCompiler.wasRecompiled(bloomPreIdx) ||
                            shaderCompiler.wasRecompiled(bloomDownIdx) ||
                            shaderCompiler.wasRecompiled(bloomUpIdx) ||
                            shaderCompiler.wasRecompiled(bloomCompIdx);
        if (sceneChanged) {
            createScenePSO();
            createShadowPSO();
        }
        if (outlineChanged) {
            createOutlinePSO();
        }
        if (bloomChanged) {
            auto bc = [&](size_t idx) -> D3D12_SHADER_BYTECODE {
                auto d = shaderCompiler.data(idx);
                return d ? D3D12_SHADER_BYTECODE{ d, shaderCompiler.size(idx) }
                         : D3D12_SHADER_BYTECODE{};
            };
            bloom.reloadPipelines(
                device.Get(), bc(bloomFsVsIdx), bc(bloomPreIdx), bc(bloomDownIdx), bc(bloomUpIdx),
                bc(bloomCompIdx)
            );
        }
    }

    // Spawn entities
    if (!scene.spawnableMeshRefs.empty()) {
        std::uniform_real_distribution<float> scaleDist(0.05f, 0.35f);
        std::uniform_real_distribution<float> angleDist(0.0f, 6.2832f);
        std::uniform_real_distribution<float> axisDist(-1.0f, 1.0f);
        std::uniform_int_distribution<size_t> meshDist(0, scene.spawnableMeshRefs.size() - 1);

        std::uniform_real_distribution<float> hueDist(0.0f, 360.0f);

        std::uniform_real_distribution<float> speedDist(0.1f, 0.5f);
        std::uniform_int_distribution<int> presetDist(
            0, static_cast<int>(MaterialPreset::Count) - 1
        );

        auto spawnOne = [&] {
            const vec3 axis =
                normalize(vec3(axisDist(scene.rng), axisDist(scene.rng), axisDist(scene.rng)));
            const vec3 pos = vec3(axisDist(scene.rng), axisDist(scene.rng), axisDist(scene.rng)) *
                             this->cam.radius / 2.0f;
            const float s = scaleDist(scene.rng);
            const float angle = angleDist(scene.rng);
            const mat4 world =
                scale(s, s, s) * rotateAxis(axis, angle) * translate(pos.x, pos.y, pos.z);
            Transform tf;
            tf.world = world;
            MeshRef mesh = scene.spawnableMeshRefs[meshDist(scene.rng)];

            // Assign random preset material
            auto preset = static_cast<MaterialPreset>(presetDist(scene.rng));
            int matIdx = scene.presetIdx[static_cast<int>(preset)];
            if (matIdx >= 0) {
                mesh.materialIndex = matIdx;
            }

            vec4 color = hslToLinear(hueDist(scene.rng), 0.7f, 0.65f);
            if (preset == MaterialPreset::Mirror) {
                mesh.albedoOverride = {};  // use material's black albedo
            } else if (preset == MaterialPreset::Metal) {
                mesh.albedoOverride = { color.x * 0.25f, color.y * 0.25f, color.z * 0.25f, 1.0f };
            } else {
                mesh.albedoOverride = color;
            }

            float orbitRadius = std::sqrt(pos.x * pos.x + pos.z * pos.z);
            float orbitAngle = std::atan2(pos.z, pos.x);
            Animated anim{ speedDist(scene.rng), orbitRadius, orbitAngle, pos.y, s, axis, angle,
                           angleDist(scene.rng) };

            scene.ecsWorld.entity().set(tf).set(mesh).set(anim).add<Pickable>();
        };

        int current = scene.ecsWorld.count<MeshRef>();
        int capacity = static_cast<int>(Scene::maxDrawsPerFrame) - 1;

        if (runtimeConfig.spawnPerFrame > 0) {
            int toSpawn = std::min(runtimeConfig.spawnPerFrame, capacity - current);
            for (int i = 0; i < toSpawn; ++i) {
                spawnOne();
            }
        } else if (!spawningStopped && current < capacity) {
            // Spawn a batch each frame until the last 3 frames all exceeded 10ms
            int toSpawn = std::min(std::max(1, spawnBatchSize), capacity - current);
            for (int i = 0; i < toSpawn; ++i) {
                spawnOne();
            }
        }
    }

    const float w = static_cast<float>(this->clientWidth);

    this->mouseDelta = { this->inputMap.GetFloatDelta(Button::AxisDeltaX),
                         this->inputMap.GetFloatDelta(Button::AxisDeltaY) };
    this->mousePos = { this->inputMap.GetFloat(Button::AxisX),
                       this->inputMap.GetFloat(Button::AxisY) };

    // --- Entity picking from ID buffer ---
    {
        hoveredEntity = flecs::entity{};
        uint32_t pickIdx = picker.pickedIndex;
        if (pickIdx != ObjectPicker::invalidID && pickIdx < drawIndexToEntity.size()) {
            hoveredEntity = drawIndexToEntity[pickIdx];
        }

        bool leftDown = this->inputMap.GetBool(Button::LeftClick);
        if (leftDown && !leftClickActive) {
            clickStartPos = mousePos;
            leftClickActive = true;
        }
        if (!leftDown && leftClickActive) {
            leftClickActive = false;
            float dx = mousePos.x - clickStartPos.x;
            float dy = mousePos.y - clickStartPos.y;
            if (dx * dx + dy * dy < 9.0f) {  // < 3px drag = click
                selectedEntity = hoveredEntity;
            }
        }
    }

    this->matModel = mat4{};
    if (this->inputMap.GetBool(Button::LeftClick)) {
        this->cam.pitch += (this->mouseDelta.y / w) * 180_deg;
        this->cam.yaw -= (this->mouseDelta.x / w) * 360_deg;
        this->cam.pitch = std::clamp(this->cam.pitch, -89.9_deg, 89.9_deg);
    }
    if (this->inputMap.GetBool(Button::RightClick)) {
        this->cam.radius += (this->mouseDelta.y / w) * this->cam.radius;
    }
    this->cam.aspectRatio = w / static_cast<float>(this->clientHeight);
    if (this->inputMap.GetBoolWasDown(Button::ScrollUp)) {
        this->cam.radius *= 0.8f;
    }
    if (this->inputMap.GetBoolWasDown(Button::ScrollDown)) {
        this->cam.radius *= 1.25f;
    }
    this->cam.radius = std::clamp(this->cam.radius, 0.1f, 1000.0f);

    // Animate orbiting entities
    if (animateEntities) {
        scene.animQuery.each([&](Transform& tf, Animated& anim) {
            anim.orbitAngle += anim.speed * dt;
            float pulse = 1.0f + 0.15f * std::sin(lightTime * 2.0f + anim.pulsePhase);
            float s = anim.initialScale * pulse;
            vec3 pos(
                anim.orbitRadius * std::cos(anim.orbitAngle), anim.orbitY,
                anim.orbitRadius * std::sin(anim.orbitAngle)
            );
            tf.world = scale(s, s, s) * rotateAxis(anim.rotAxis, anim.rotAngle) *
                       translate(pos.x, pos.y, pos.z);
        });
    }
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
            UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                                                       WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
            ::SetWindowLongW(this->hWnd, GWL_STYLE, static_cast<LONG>(windowStyle));
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

// createScenePSO, createShadowPSO, createCubemapResources, loadContent, onResize
// are in application_setup.cpp
