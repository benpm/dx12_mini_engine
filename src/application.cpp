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
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <random>
#include <string>
#include <filesystem>
#include <vector>
#include <flecs.h>
#include <gainput/gainput.h>
#include <ScreenGrab.h>
#include <wincodec.h>
#include <spdlog/spdlog.h>
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
    cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
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

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
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
        bool bloomChanged = shaderCompiler.wasRecompiled(bloomFsVsIdx) ||
                            shaderCompiler.wasRecompiled(bloomPreIdx) ||
                            shaderCompiler.wasRecompiled(bloomDownIdx) ||
                            shaderCompiler.wasRecompiled(bloomUpIdx) ||
                            shaderCompiler.wasRecompiled(bloomCompIdx);
        if (sceneChanged) {
            createScenePSO();
            createShadowPSO();
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

        if (testMode) {
            int toSpawn = 6;
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
        scene.ecsWorld.each([&](Transform& tf, Animated& anim) {
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

void Application::render()
{
    // Read back picked entity from previous frame
    picker.readPickResult();

    auto backBuffer = this->backBuffers[this->curBackBufIdx];
    auto cmdList = this->cmdQueue.getCmdList();

    // --- Compute per-frame scene data ---
    mat4 viewProj = this->cam.view() * this->cam.proj();
    float camX = this->cam.radius * cos(this->cam.pitch) * cos(this->cam.yaw);
    float camY = this->cam.radius * sin(this->cam.pitch);
    float camZ = this->cam.radius * cos(this->cam.pitch) * sin(this->cam.yaw);
    vec4 cameraPos(camX, camY, camZ, 1.0f);
    vec4 ambientColor(
        bgColor[0] * ambientBrightness, bgColor[1] * ambientBrightness,
        bgColor[2] * ambientBrightness, 1.0f
    );

    // Compute animated light positions for this frame
    vec4 animLightPos[SceneConstantBuffer::maxLights];
    vec4 animLightColor[SceneConstantBuffer::maxLights];
    for (int i = 0; i < SceneConstantBuffer::maxLights; ++i) {
        const LightAnim& la = lightAnims[i];
        animLightPos[i] = { la.center.x + la.ampX * std::sin(la.freqX * lightTime),
                            la.center.y + la.ampY * std::cos(la.freqY * lightTime),
                            la.center.z + la.ampZ * std::sin(la.freqZ * lightTime + (float)i),
                            1.0f };
        animLightColor[i] = { la.color.x * lightBrightness, la.color.y * lightBrightness,
                              la.color.z * lightBrightness, 1.0f };
    }
    billboards.updateInstances(
        animLightPos, animLightColor, static_cast<uint32_t>(SceneConstantBuffer::maxLights)
    );

    // Directional light shadow map viewProj
    mat4 lightViewProj{};
    vec4 dirLightDirVec{};  // toward light (negated from UI direction)
    vec4 dirLightColorVec{};
    if (shadowEnabled) {
        using namespace DirectX;
        // UI stores direction FROM light; negate to get direction TOWARD light for shader
        XMVECTOR fromLight =
            XMVector3Normalize(XMVectorSet(dirLightDir[0], dirLightDir[1], dirLightDir[2], 0.0f));
        XMVECTOR toLight = XMVectorNegate(fromLight);
        XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(&dirLightDirVec), XMVectorSetW(toLight, 0.0f));
        dirLightColorVec = { dirLightColor[0] * dirLightBrightness,
                             dirLightColor[1] * dirLightBrightness,
                             dirLightColor[2] * dirLightBrightness, 1.0f };

        // Place virtual light position far along the direction for LookAt
        XMVECTOR lightP = XMVectorScale(toLight, shadowLightDistance);
        XMVECTOR target = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        float dotUp = fabsf(XMVectorGetByIndex(
            XMVector3Dot(XMVector3Normalize(XMVectorSubtract(target, lightP)), up), 0
        ));
        if (dotUp > 0.99f) {
            up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        }
        XMMATRIX lightView = XMMatrixLookAtLH(lightP, target, up);
        XMMATRIX lightProj = XMMatrixOrthographicLH(
            shadowOrthoSize, shadowOrthoSize, std::max(0.001f, shadowNearPlane),
            std::max(shadowNearPlane + 0.001f, shadowFarPlane)
        );
        XMMATRIX lvp = XMMatrixMultiply(lightView, lightProj);
        XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&lightViewProj), lvp);
    }

    // --- Fill structured buffer (scene + shadow draw data) ---
    struct DrawCmd
    {
        uint32_t indexCount;
        uint32_t indexOffset;
        uint32_t vertexOffset;
    };
    std::vector<DrawCmd> drawCmds;
    drawIndexToEntity.clear();
    uint32_t drawIdx = 0;
    SceneConstantBuffer* mapped = scene.drawDataMapped[curBackBufIdx];
    bool anyReflective = false;
    vec3 reflectivePos{};

    scene.ecsWorld.each([&](flecs::entity e, const Transform& tf, const MeshRef& mesh) {
        assert(drawIdx < Scene::maxDrawsPerFrame / 3);
        const Material& mat = scene.materials[mesh.materialIndex];

        SceneConstantBuffer& scb = mapped[drawIdx];
        scb.model = tf.world * this->matModel;
        scb.viewProj = viewProj;
        scb.cameraPos = cameraPos;
        scb.ambientColor = ambientColor;
        for (int li = 0; li < SceneConstantBuffer::maxLights; ++li) {
            scb.lightPos[li] = animLightPos[li];
            scb.lightColor[li] = animLightColor[li];
        }
        scb.albedo = mesh.albedoOverride.w > 0.0f ? mesh.albedoOverride : mat.albedo;
        scb.roughness = mat.roughness;
        scb.metallic = mat.metallic;
        scb.emissiveStrength = mat.emissiveStrength;
        scb.reflective = mat.reflective ? 1.0f : 0.0f;
        scb.emissive = mat.emissive;
        scb.dirLightDir = dirLightDirVec;
        scb.dirLightColor = dirLightColorVec;
        scb.lightViewProj = lightViewProj;
        scb.shadowBias = shadowBias;
        scb.shadowMapTexelSize = 1.0f / static_cast<float>(shadowMapSize);
        scb._pad2[0] = scb._pad2[1] = 0.0f;

        if (mat.reflective && !anyReflective) {
            anyReflective = true;
            // Extract translation from world matrix (row 3 in row-major XMFLOAT4X4)
            const auto& m = scb.model;
            reflectivePos = vec3(m._41, m._42, m._43);
        }

        // Highlight hovered/selected entities with emissive tint
        if (e == selectedEntity) {
            scb.emissive = vec4(1.0f, 0.6f, 0.0f, 1.0f);
            scb.emissiveStrength = 0.8f;
        } else if (e == hoveredEntity) {
            scb.emissive = vec4(1.0f, 0.5f, 0.0f, 1.0f);
            scb.emissiveStrength = 0.3f;
        }

        drawCmds.push_back({ mesh.indexCount, mesh.indexOffset, mesh.vertexOffset });
        drawIndexToEntity.push_back(e);
        drawIdx++;
    });

    uint32_t entityCount = drawIdx;

    // Shadow draw data (same model transforms, light viewProj instead of camera viewProj)
    if (shadowEnabled) {
        for (uint32_t i = 0; i < entityCount; ++i) {
            SceneConstantBuffer& shadow = mapped[entityCount + i];
            shadow.model = mapped[i].model;
            shadow.viewProj = lightViewProj;
        }
    }

    this->lastFrameObjectCount = entityCount;

    // --- Shadow pass ---
    if (shadowEnabled) {
        this->transitionResource(
            cmdList, shadowMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE
        );
        auto shadowDsv = shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
        this->clearDepth(cmdList, shadowDsv);

        cmdList->SetPipelineState(this->shadowPSO.Get());
        cmdList->SetGraphicsRootSignature(this->rootSignature.Get());
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        D3D12_VIEWPORT shadowVP = { 0.0f, 0.0f, (float)shadowMapSize, (float)shadowMapSize,
                                    0.0f, 1.0f };
        D3D12_RECT shadowScissor = { 0, 0, (LONG)shadowMapSize, (LONG)shadowMapSize };
        cmdList->RSSetViewports(1, &shadowVP);
        cmdList->RSSetScissorRects(1, &shadowScissor);
        cmdList->OMSetRenderTargets(0, nullptr, false, &shadowDsv);

        cmdList->IASetVertexBuffers(0, 1, &scene.megaVBV);
        cmdList->IASetIndexBuffer(&scene.megaIBV);

        ID3D12DescriptorHeap* sceneHeaps[] = { scene.sceneSrvHeap.Get() };
        cmdList->SetDescriptorHeaps(1, sceneHeaps);
        CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(
            scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
        );
        cmdList->SetGraphicsRootDescriptorTable(0, srvGpuHandle);

        for (uint32_t i = 0; i < entityCount; ++i) {
            cmdList->SetGraphicsRoot32BitConstant(1, entityCount + i, 0);
            cmdList->DrawIndexedInstanced(
                drawCmds[i].indexCount, 1, drawCmds[i].indexOffset,
                static_cast<INT>(drawCmds[i].vertexOffset), 0
            );
        }

        this->transitionResource(
            cmdList, shadowMap, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
    }

    // --- Cubemap pass: render environment for reflective objects ---
    if (cubemapEnabled && anyReflective) {
        // Build 6 cubemap face view-projection matrices (LH, 90° FOV)
        XMVECTOR eyePos = XMVectorSet(reflectivePos.x, reflectivePos.y, reflectivePos.z, 1.0f);
        XMMATRIX cubeProj = XMMatrixPerspectiveFovLH(
            XM_PIDIV2, 1.0f, std::max(0.001f, cubemapNearPlane),
            std::max(cubemapNearPlane + 0.001f, cubemapFarPlane)
        );
        struct CubeFace
        {
            XMVECTOR dir;
            XMVECTOR up;
        };
        CubeFace cubeFaces[6] = {
            { XMVectorSet(1, 0, 0, 0), XMVectorSet(0, 1, 0, 0) },   // +X
            { XMVectorSet(-1, 0, 0, 0), XMVectorSet(0, 1, 0, 0) },  // -X
            { XMVectorSet(0, 1, 0, 0), XMVectorSet(0, 0, -1, 0) },  // +Y
            { XMVectorSet(0, -1, 0, 0), XMVectorSet(0, 0, 1, 0) },  // -Y
            { XMVectorSet(0, 0, 1, 0), XMVectorSet(0, 1, 0, 0) },   // +Z
            { XMVectorSet(0, 0, -1, 0), XMVectorSet(0, 1, 0, 0) },  // -Z
        };

        // Write cubemap draw data: non-reflective entities only, 6 copies with different viewProj
        // Placed at offset 2*entityCount in the structured buffer
        uint32_t cubemapBaseIdx = 2 * entityCount;
        std::vector<uint32_t> nonReflectiveIndices;
        for (uint32_t i = 0; i < entityCount; ++i) {
            if (mapped[i].reflective < 0.5f) {
                nonReflectiveIndices.push_back(i);
            }
        }
        uint32_t nonReflCount = static_cast<uint32_t>(nonReflectiveIndices.size());

        static bool warnedMissingCubemapSources = false;
        if (nonReflCount == 0) {
            if (!warnedMissingCubemapSources) {
                spdlog::warn(
                    "Cubemap pass skipped: no non-reflective entities available to render."
                );
                warnedMissingCubemapSources = true;
            }
        } else {
            warnedMissingCubemapSources = false;
        }

        if (nonReflCount > 0) {
            this->transitionResource(
                cmdList, cubemapTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_RENDER_TARGET
            );

            for (uint32_t face = 0; face < 6; ++face) {
                XMMATRIX faceView = XMMatrixLookAtLH(
                    eyePos, XMVectorAdd(eyePos, cubeFaces[face].dir), cubeFaces[face].up
                );
                mat4 faceVP(XMMatrixMultiply(faceView, cubeProj));
                uint32_t faceOffset = cubemapBaseIdx + face * nonReflCount;
                for (uint32_t j = 0; j < nonReflCount; ++j) {
                    uint32_t srcIdx = nonReflectiveIndices[j];
                    SceneConstantBuffer& dst = mapped[faceOffset + j];
                    dst = mapped[srcIdx];
                    dst.viewProj = faceVP;
                    dst.reflective = 0.0f;
                }
            }

            // Render 6 cubemap faces
            D3D12_VIEWPORT cubeVP = {
                0, 0, (float)cubemapResolution, (float)cubemapResolution, 0, 1
            };
            D3D12_RECT cubeScissor = { 0, 0, (LONG)cubemapResolution, (LONG)cubemapResolution };
            UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            UINT dsvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

            cmdList->SetPipelineState(this->pipelineState.Get());
            cmdList->SetGraphicsRootSignature(this->rootSignature.Get());
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->IASetVertexBuffers(0, 1, &scene.megaVBV);
            cmdList->IASetIndexBuffer(&scene.megaIBV);

            ID3D12DescriptorHeap* sceneHeaps[] = { scene.sceneSrvHeap.Get() };
            cmdList->SetDescriptorHeaps(1, sceneHeaps);
            CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(
                scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
            );
            cmdList->SetGraphicsRootDescriptorTable(0, srvGpuHandle);
            // Bind shadow map for cubemap pass too
            CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrv(
                scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                static_cast<INT>(Scene::nBuffers), scene.sceneSrvDescSize
            );
            cmdList->SetGraphicsRootDescriptorTable(2, shadowSrv);
            // Bind cubemap SRV (will be black/uninitialized but reflective=0 prevents sampling)
            CD3DX12_GPU_DESCRIPTOR_HANDLE cubeSrv(
                scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                static_cast<INT>(Scene::nBuffers + 1), scene.sceneSrvDescSize
            );
            cmdList->SetGraphicsRootDescriptorTable(3, cubeSrv);

            for (uint32_t face = 0; face < 6; ++face) {
                CD3DX12_CPU_DESCRIPTOR_HANDLE faceRtv(
                    cubemapRtvHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(face),
                    rtvSize
                );
                CD3DX12_CPU_DESCRIPTOR_HANDLE faceDsv(
                    cubemapDsvHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(face),
                    dsvSize
                );
                FLOAT clearColor[] = { bgColor[0], bgColor[1], bgColor[2], 1.0f };
                this->clearRTV(cmdList, faceRtv, clearColor);
                this->clearDepth(cmdList, faceDsv);
                cmdList->RSSetViewports(1, &cubeVP);
                cmdList->RSSetScissorRects(1, &cubeScissor);
                cmdList->OMSetRenderTargets(1, &faceRtv, true, &faceDsv);

                uint32_t faceOffset = cubemapBaseIdx + face * nonReflCount;
                for (uint32_t j = 0; j < nonReflCount; ++j) {
                    uint32_t drawDataIdx = faceOffset + j;
                    uint32_t srcIdx = nonReflectiveIndices[j];
                    cmdList->SetGraphicsRoot32BitConstant(1, drawDataIdx, 0);
                    cmdList->DrawIndexedInstanced(
                        drawCmds[srcIdx].indexCount, 1, drawCmds[srcIdx].indexOffset,
                        static_cast<INT>(drawCmds[srcIdx].vertexOffset), 0
                    );
                }
            }

            this->transitionResource(
                cmdList, cubemapTexture, D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
            );
        }
    }

    // --- Scene pass: render to HDR render target ---
    {
        FLOAT clearColor[] = { bgColor[0], bgColor[1], bgColor[2], 1.0f };
        CD3DX12_CPU_DESCRIPTOR_HANDLE hdrRtv(
            bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart()
        );
        this->clearRTV(cmdList, hdrRtv, clearColor);
        auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
        this->clearDepth(cmdList, dsv);

        cmdList->SetPipelineState(this->pipelineState.Get());
        cmdList->SetGraphicsRootSignature(this->rootSignature.Get());
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->RSSetViewports(1, &this->viewport);
        cmdList->RSSetScissorRects(1, &this->scissorRect);
        cmdList->OMSetRenderTargets(1, &hdrRtv, true, &dsv);

        cmdList->IASetVertexBuffers(0, 1, &scene.megaVBV);
        cmdList->IASetIndexBuffer(&scene.megaIBV);

        ID3D12DescriptorHeap* sceneHeaps[] = { scene.sceneSrvHeap.Get() };
        cmdList->SetDescriptorHeaps(1, sceneHeaps);
        CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(
            scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
        );
        cmdList->SetGraphicsRootDescriptorTable(0, srvGpuHandle);

        // Bind shadow map SRV (descriptor index 3 in sceneSrvHeap)
        CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrvHandle(
            scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(Scene::nBuffers), scene.sceneSrvDescSize
        );
        cmdList->SetGraphicsRootDescriptorTable(2, shadowSrvHandle);

        // Bind cubemap SRV (descriptor index nBuffers+1 in sceneSrvHeap)
        CD3DX12_GPU_DESCRIPTOR_HANDLE cubemapSrvHandle(
            scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(Scene::nBuffers + 1), scene.sceneSrvDescSize
        );
        cmdList->SetGraphicsRootDescriptorTable(3, cubemapSrvHandle);

        uint32_t currentVertexCount = 0;
        for (uint32_t i = 0; i < entityCount; ++i) {
            currentVertexCount += drawCmds[i].indexCount;
            cmdList->SetGraphicsRoot32BitConstant(1, i, 0);
            cmdList->DrawIndexedInstanced(
                drawCmds[i].indexCount, 1, drawCmds[i].indexOffset,
                static_cast<INT>(drawCmds[i].vertexOffset), 0
            );
        }

        this->lastFrameVertexCount = currentVertexCount;
    }

    // --- Object ID pass (for picking) ---
    {
        auto idRtv = picker.getRTV();
        auto idDsv = picker.getDSV();
        // Clear ID RT to invalidID
        FLOAT clearColor[] = { static_cast<float>(ObjectPicker::invalidID), 0.0f, 0.0f, 0.0f };
        cmdList->ClearRenderTargetView(idRtv, clearColor, 0, nullptr);
        cmdList->ClearDepthStencilView(idDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        cmdList->SetPipelineState(picker.pso.Get());
        cmdList->SetGraphicsRootSignature(this->rootSignature.Get());
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->RSSetViewports(1, &this->viewport);
        cmdList->RSSetScissorRects(1, &this->scissorRect);
        cmdList->OMSetRenderTargets(1, &idRtv, true, &idDsv);

        cmdList->IASetVertexBuffers(0, 1, &scene.megaVBV);
        cmdList->IASetIndexBuffer(&scene.megaIBV);

        ID3D12DescriptorHeap* sceneHeaps[] = { scene.sceneSrvHeap.Get() };
        cmdList->SetDescriptorHeaps(1, sceneHeaps);
        CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(
            scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
        );
        cmdList->SetGraphicsRootDescriptorTable(0, srvGpuHandle);

        for (uint32_t i = 0; i < entityCount; ++i) {
            cmdList->SetGraphicsRoot32BitConstant(1, i, 0);
            cmdList->DrawIndexedInstanced(
                drawCmds[i].indexCount, 1, drawCmds[i].indexOffset,
                static_cast<INT>(drawCmds[i].vertexOffset), 0
            );
        }

        // Copy pixel under mouse cursor to readback buffer
        uint32_t mx = static_cast<uint32_t>(mousePos.x);
        uint32_t my = static_cast<uint32_t>(mousePos.y);
        picker.copyPickedPixel(cmdList, mx, my);
    }

    // --- Light billboards pass (batched instancing) ---
    if (showLightBillboards) {
        auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
        CD3DX12_CPU_DESCRIPTOR_HANDLE hdrRtv(
            bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart()
        );
        cmdList->RSSetViewports(1, &this->viewport);
        cmdList->RSSetScissorRects(1, &this->scissorRect);
        cmdList->OMSetRenderTargets(1, &hdrRtv, true, &dsv);
        billboards.render(cmdList, viewProj, vec3(cameraPos.x, cameraPos.y, cameraPos.z));
    }

    // --- Bloom + composite ---
    CD3DX12_CPU_DESCRIPTOR_HANDLE backBufRtv(
        rtvHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(curBackBufIdx), rtvDescSize
    );
    bloom.render(
        cmdList, backBuffer, backBufRtv, clientWidth, clientHeight, bloomThreshold, bloomIntensity,
        tonemapMode
    );

    if (!this->testMode) {
        this->renderImGui(cmdList);
    }

    this->transitionResource(
        cmdList, backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT
    );

    this->frameFenceValues[this->curBackBufIdx] = this->cmdQueue.execCmdList(cmdList);

    UINT syncInterval = (this->vsync && !this->testMode) ? 1 : 0;
    UINT presentFlags = (this->tearingSupported && !this->vsync) ? DXGI_PRESENT_ALLOW_TEARING : 0;
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

// renderImGui is in application_ui.cpp

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
