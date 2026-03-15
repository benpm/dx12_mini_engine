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
#include <gainput/gainput.h>
#include <ScreenGrab.h>
#include <wincodec.h>
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
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

module application;

import window;

using Microsoft::WRL::ComPtr;

// Convert HSL to linear RGB. hue in [0,360), sat and light in [0,1].
static vec4 hslToLinear(float hue, float sat, float light)
{
    float c = (1.0f - std::abs(2.0f * light - 1.0f)) * sat;
    float x = c * (1.0f - std::abs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    float m = light - c * 0.5f;
    float r, g, b;
    int seg = static_cast<int>(hue / 60.0f) % 6;
    switch (seg) {
        case 0:  r = c; g = x; b = 0; break;
        case 1:  r = x; g = c; b = 0; break;
        case 2:  r = 0; g = c; b = x; break;
        case 3:  r = 0; g = x; b = c; break;
        case 4:  r = x; g = 0; b = c; break;
        default: r = c; g = 0; b = x; break;
    }
    // sRGB → linear
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
    this->imguiLayer.init(
        hWnd, device.Get(), cmdQueue.queue.Get(), nBuffers, DXGI_FORMAT_R8G8B8A8_UNORM
    );
    this->isInitialized = true;
}

Application::~Application()
{
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

    lightTime += dt;
    lastFrameMs = dt * 1000.0f;
    recentFrameMs[recentFrameHead % 3] = lastFrameMs;
    ++recentFrameHead;
    if (!spawningStopped && recentFrameHead >= 3 &&
        recentFrameMs[0] > 10.0f && recentFrameMs[1] > 10.0f && recentFrameMs[2] > 10.0f) {
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
            bloom.reloadPipelines(device.Get(), bc(bloomFsVsIdx), bc(bloomPreIdx),
                                  bc(bloomDownIdx), bc(bloomUpIdx), bc(bloomCompIdx));
        }
    }

    // Spawn entities
    if (!scene.spawnableMeshRefs.empty()) {
        std::uniform_real_distribution<float> scaleDist(0.05f, 0.7f);
        std::uniform_real_distribution<float> angleDist(0.0f, 6.2832f);
        std::uniform_real_distribution<float> axisDist(-1.0f, 1.0f);
        std::uniform_int_distribution<size_t> meshDist(0, scene.spawnableMeshRefs.size() - 1);

        std::uniform_real_distribution<float> hueDist(0.0f, 360.0f);

        std::uniform_real_distribution<float> speedDist(0.1f, 0.5f);

        auto spawnOne = [&] {
            const vec3 axis =
                normalize(vec3(axisDist(scene.rng), axisDist(scene.rng), axisDist(scene.rng)));
            const vec3 pos =
                vec3(axisDist(scene.rng), axisDist(scene.rng), axisDist(scene.rng)) *
                this->cam.radius / 2.0f;
            const float s = scaleDist(scene.rng);
            const float angle = angleDist(scene.rng);
            const mat4 world =
                scale(s, s, s) * rotateAxis(axis, angle) *
                translate(pos.x, pos.y, pos.z);
            Transform tf;
            tf.world = world;
            MeshRef mesh = scene.spawnableMeshRefs[meshDist(scene.rng)];
            mesh.albedoOverride = hslToLinear(hueDist(scene.rng), 0.7f, 0.65f);

            float orbitRadius = std::sqrt(pos.x * pos.x + pos.z * pos.z);
            float orbitAngle = std::atan2(pos.z, pos.x);
            Animated anim{ speedDist(scene.rng), orbitRadius, orbitAngle, pos.y,
                           s, axis, angle, angleDist(scene.rng) };

            scene.ecsWorld.entity().set(tf).set(mesh).set(anim);
        };

        int current = scene.ecsWorld.count<MeshRef>();
        int capacity = static_cast<int>(Scene::maxDrawsPerFrame) - 1;

        if (testMode) {
            int toSpawn = std::min({ 100, capacity - current, 1000 - current });
            for (int i = 0; i < toSpawn; ++i) {
                spawnOne();
            }
        } else if (!spawningStopped && current < capacity) {
            // Spawn a batch each frame until the last 3 frames all exceeded 10ms
            constexpr int batchSize = 50;
            int toSpawn = std::min(batchSize, capacity - current);
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

    // Animate orbiting entities
    scene.ecsWorld.each([&](Transform& tf, Animated& anim) {
        anim.orbitAngle += anim.speed * dt;
        float pulse = 1.0f + 0.15f * std::sin(lightTime * 2.0f + anim.pulsePhase);
        float s = anim.initialScale * pulse;
        vec3 pos(anim.orbitRadius * std::cos(anim.orbitAngle), anim.orbitY,
                 anim.orbitRadius * std::sin(anim.orbitAngle));
        tf.world = scale(s, s, s) * rotateAxis(anim.rotAxis, anim.rotAngle) *
                   translate(pos.x, pos.y, pos.z);
    });
}

void Application::render()
{
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
        animLightPos[i] = {
            la.center.x + la.ampX * std::sin(la.freqX * lightTime),
            la.center.y + la.ampY * std::cos(la.freqY * lightTime),
            la.center.z + la.ampZ * std::sin(la.freqZ * lightTime + (float)i),
            1.0f
        };
        animLightColor[i] = {
            la.color.x * lightBrightness, la.color.y * lightBrightness,
            la.color.z * lightBrightness, 1.0f
        };
    }

    // Directional light shadow map viewProj
    mat4 lightViewProj{};
    vec4 dirLightDirVec{};  // toward light (negated from UI direction)
    vec4 dirLightColorVec{};
    if (shadowEnabled) {
        using namespace DirectX;
        // UI stores direction FROM light; negate to get direction TOWARD light for shader
        XMVECTOR fromLight = XMVector3Normalize(
            XMVectorSet(dirLightDir[0], dirLightDir[1], dirLightDir[2], 0.0f)
        );
        XMVECTOR toLight = XMVectorNegate(fromLight);
        XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(&dirLightDirVec),
                       XMVectorSetW(toLight, 0.0f));
        dirLightColorVec = {
            dirLightColor[0] * dirLightBrightness,
            dirLightColor[1] * dirLightBrightness,
            dirLightColor[2] * dirLightBrightness, 1.0f
        };

        // Place virtual light position far along the direction for LookAt
        XMVECTOR lightP = XMVectorScale(toLight, 25.0f);
        XMVECTOR target = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        float dotUp = fabsf(XMVectorGetByIndex(
            XMVector3Dot(XMVector3Normalize(XMVectorSubtract(target, lightP)),
                         up), 0));
        if (dotUp > 0.99f)
            up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        XMMATRIX lightView = XMMatrixLookAtLH(lightP, target, up);
        XMMATRIX lightProj = XMMatrixOrthographicLH(30.0f, 30.0f, 0.1f, 60.0f);
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
    uint32_t drawIdx = 0;
    SceneConstantBuffer* mapped = scene.drawDataMapped[curBackBufIdx];

    scene.ecsWorld.each([&](const Transform& tf, const MeshRef& mesh) {
        assert(drawIdx < Scene::maxDrawsPerFrame / 2);
        const Material& mat = scene.materials[mesh.materialIndex];

        // Scene draw data
        SceneConstantBuffer& scb = mapped[drawIdx];
        scb.model = tf.world * this->matModel;
        scb.viewProj = viewProj;
        scb.cameraPos = cameraPos;
        scb.ambientColor = ambientColor;
        for (int li = 0; li < SceneConstantBuffer::maxLights; ++li) {
            scb.lightPos[li]   = animLightPos[li];
            scb.lightColor[li] = animLightColor[li];
        }
        scb.albedo = mesh.albedoOverride.w > 0.0f ? mesh.albedoOverride : mat.albedo;
        scb.roughness = mat.roughness;
        scb.metallic = mat.metallic;
        scb.emissiveStrength = mat.emissiveStrength;
        scb._pad = 0.0f;
        scb.emissive = mat.emissive;
        scb.dirLightDir = dirLightDirVec;
        scb.dirLightColor = dirLightColorVec;
        scb.lightViewProj = lightViewProj;
        scb.shadowBias = shadowBias;
        scb.shadowMapTexelSize = 1.0f / static_cast<float>(shadowMapSize);
        scb._pad2[0] = scb._pad2[1] = 0.0f;

        drawCmds.push_back({ mesh.indexCount, mesh.indexOffset, mesh.vertexOffset });
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
    UINT presentFlags =
        (this->tearingSupported && !this->vsync) ? DXGI_PRESENT_ALLOW_TEARING : 0;
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

// ---------------------------------------------------------------------------
// ImGui render (app-specific UI — stays here)
// ---------------------------------------------------------------------------

void Application::renderImGui(ComPtr<ID3D12GraphicsCommandList2> cmdList)
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Bloom")) {
            ImGui::PushItemWidth(220.0f);
            ImGui::SliderFloat("Threshold", &bloomThreshold, 0.0f, 3.0f);
            ImGui::SliderFloat("Intensity", &bloomIntensity, 0.0f, 5.0f);
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tonemap")) {
            const char* tonemappers[] = { "ACES Filmic", "AgX", "AgX Punchy", "Gran Turismo",
                                          "PBR Neutral" };
            ImGui::PushItemWidth(180.0f);
            ImGui::Combo("##tonemap", &tonemapMode, tonemappers, IM_ARRAYSIZE(tonemappers));
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scene")) {
            ImGui::PushItemWidth(220.0f);
            ImGui::ColorEdit3("Background", bgColor);
            ImGui::Separator();
            ImGui::Text("Directional Light");
            ImGui::SliderFloat3("Direction", dirLightDir, -1.0f, 1.0f);
            ImGui::ColorEdit3("Dir Color", dirLightColor);
            ImGui::SliderFloat("Dir Brightness", &dirLightBrightness, 0.0f, 20.0f);
            ImGui::Separator();
            ImGui::Text("Point Lights");
            ImGui::SliderFloat("Point Brightness", &lightBrightness, 0.0f, 20.0f);
            ImGui::SliderFloat("Ambient Brightness", &ambientBrightness, 0.0f, 2.0f);
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Shadows")) {
            ImGui::PushItemWidth(220.0f);
            ImGui::Checkbox("Enabled", &shadowEnabled);
            ImGui::SliderFloat("Bias", &shadowBias, 0.0001f, 0.01f, "%.4f");
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Material")) {
            ImGui::PushItemWidth(220.0f);
            if (!scene.materials.empty()) {
                if (scene.materials.size() > 1) {
                    std::vector<const char*> names;
                    names.reserve(scene.materials.size());
                    for (const auto& m : scene.materials) {
                        names.push_back(m.name.c_str());
                    }
                    ImGui::Combo(
                        "##matsel", &scene.selectedMaterialIdx, names.data(), (int)names.size()
                    );
                    ImGui::Separator();
                }
                Material& mat = scene.materials[std::clamp(
                    scene.selectedMaterialIdx, 0, (int)scene.materials.size() - 1
                )];
                ImGui::ColorEdit4("Albedo", &mat.albedo.x);
                ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f);
                ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f);
                ImGui::ColorEdit3("Emissive", &mat.emissive.x);
                ImGui::SliderFloat("Emissive Strength", &mat.emissiveStrength, 0.0f, 20.0f);
            }
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scene File")) {
            ImGui::PushItemWidth(300.0f);
            ImGui::InputText("##path", gltfPathBuf, sizeof(gltfPathBuf));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::MenuItem("Load")) {
                if (gltfPathBuf[0] != '\0') {
                    pendingGltfPath = gltfPathBuf;
                }
            }
            if (ImGui::MenuItem("Reset to Teapot")) {
                pendingResetToTeapot = true;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    if (ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Frame: %.2f ms", this->lastFrameMs);
        ImGui::Text("Objects: %u", this->lastFrameObjectCount);
        ImGui::Text("Vertices: %u", this->lastFrameVertexCount);
        ImGui::End();
    }

    ImGui::Render();

    ID3D12DescriptorHeap* heaps[] = { imguiLayer.srvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList.Get());
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

// ---------------------------------------------------------------------------
// createScenePSO — (re)creates the scene pipeline state object
// ---------------------------------------------------------------------------

void Application::createScenePSO()
{
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    auto vsData = shaderCompiler.data(sceneVSIdx);
    auto psData = shaderCompiler.data(scenePSIdx);
    auto vs = vsData ? CD3DX12_SHADER_BYTECODE(vsData, shaderCompiler.size(sceneVSIdx))
                     : CD3DX12_SHADER_BYTECODE(g_vertex_shader, sizeof(g_vertex_shader));
    auto ps = psData ? CD3DX12_SHADER_BYTECODE(psData, shaderCompiler.size(scenePSIdx))
                     : CD3DX12_SHADER_BYTECODE(g_pixel_shader, sizeof(g_pixel_shader));

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
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R11G11B10_FLOAT;
    pipelineStateStream.pRootSignature = this->rootSignature.Get();
    pipelineStateStream.InputLayout = { inputLayout, _countof(inputLayout) };
    pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.VS = vs;
    pipelineStateStream.PS = ps;
    pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pipelineStateStream.RTVFormats = rtvFormats;
    D3D12_PIPELINE_STATE_STREAM_DESC psoDesc = { sizeof(PipelineStateStream),
                                                 &pipelineStateStream };
    chkDX(this->device->CreatePipelineState(&psoDesc, IID_PPV_ARGS(&this->pipelineState)));
}

// ---------------------------------------------------------------------------
// createShadowPSO — depth-only PSO for shadow pass
// ---------------------------------------------------------------------------

void Application::createShadowPSO()
{
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    auto vsData = shaderCompiler.data(sceneVSIdx);
    auto vs = vsData ? CD3DX12_SHADER_BYTECODE(vsData, shaderCompiler.size(sceneVSIdx))
                     : CD3DX12_SHADER_BYTECODE(g_vertex_shader, sizeof(g_vertex_shader));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = this->rootSignature.Get();
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.VS = vs;
    psoDesc.PS = {};  // No pixel shader — depth only
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;  // Reduce peter-panning
    psoDesc.RasterizerState.DepthBias = 1000;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
    psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 0;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc = { 1, 0 };

    chkDX(this->device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&this->shadowPSO)));
}

// ---------------------------------------------------------------------------
// loadContent — creates pipeline + uploads default teapot scene
// ---------------------------------------------------------------------------

bool Application::loadContent()
{
    spdlog::info("loadContent start");

    scene.createMegaBuffers(device.Get());
    scene.createDrawDataBuffers(device.Get());
    scene.loadTeapot(device.Get(), cmdQueue);

    // Load all GLB models from resources/models/
    for (const auto& entry : std::filesystem::directory_iterator(MODELS_DIR)) {
        if (entry.path().extension() == ".glb") {
            spdlog::info("Loading model: {}", entry.path().filename().string());
            scene.loadGltf(entry.path().string(), device.Get(), cmdQueue, true);
        }
    }

    // DSV heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        chkDX(this->device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&this->dsvHeap)));
    }

    // Root signature (SRV descriptor table + 1 root constant)
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

    CD3DX12_DESCRIPTOR_RANGE1 srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);  // t0: structured buffer

    CD3DX12_DESCRIPTOR_RANGE1 shadowSrvRange;
    shadowSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);  // t1: shadow map

    CD3DX12_ROOT_PARAMETER1 rootParams[3];
    rootParams[0].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[1].InitAsConstants(1, 0, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[2].InitAsDescriptorTable(1, &shadowSrvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    // Comparison sampler for shadow mapping (s0)
    D3D12_STATIC_SAMPLER_DESC shadowSampler = {};
    shadowSampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    shadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    shadowSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    shadowSampler.ShaderRegister = 0;
    shadowSampler.RegisterSpace = 0;
    shadowSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 1, &shadowSampler, rootSigFlags);

    ComPtr<ID3DBlob> rootSigBlob, errorBlob;
    chkDX(D3DX12SerializeVersionedRootSignature(
        &rootSigDesc, featureData.HighestVersion, &rootSigBlob, &errorBlob
    ));
    chkDX(this->device->CreateRootSignature(
        0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
        IID_PPV_ARGS(&this->rootSignature)
    ));

    createScenePSO();

    // Shadow map resources
    {
        // Shadow depth texture
        D3D12_CLEAR_VALUE shadowClearVal = {};
        shadowClearVal.Format = DXGI_FORMAT_D32_FLOAT;
        shadowClearVal.DepthStencil = { 1.0f, 0 };
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC shadowDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_TYPELESS, shadowMapSize, shadowMapSize, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
        );
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &shadowDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &shadowClearVal,
            IID_PPV_ARGS(&shadowMap)
        ));

        // Shadow DSV heap + view
        D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
        dsvDesc.NumDescriptors = 1;
        dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        chkDX(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&shadowDsvHeap)));

        D3D12_DEPTH_STENCIL_VIEW_DESC shadowDsvViewDesc = {};
        shadowDsvViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
        shadowDsvViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(
            shadowMap.Get(), &shadowDsvViewDesc,
            shadowDsvHeap->GetCPUDescriptorHandleForHeapStart()
        );

        // Shadow SRV in sceneSrvHeap at slot 3 (after 3 structured buffer SRVs)
        D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrvDesc = {};
        shadowSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        shadowSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        shadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        shadowSrvDesc.Texture2D.MipLevels = 1;
        CD3DX12_CPU_DESCRIPTOR_HANDLE shadowSrvHandle(
            scene.sceneSrvHeap->GetCPUDescriptorHandleForHeapStart(),
            static_cast<INT>(Scene::nBuffers), scene.sceneSrvDescSize
        );
        device->CreateShaderResourceView(shadowMap.Get(), &shadowSrvDesc, shadowSrvHandle);
    }

    createShadowPSO();

    // Init shader hot reload
    if (shaderCompiler.init(DXC_PATH, SHADER_SRC_DIR)) {
        sceneVSIdx = shaderCompiler.watch("vertex_shader.hlsl", "vs_6_0");
        scenePSIdx = shaderCompiler.watch("pixel_shader.hlsl", "ps_6_0");
        bloomFsVsIdx = shaderCompiler.watch("fullscreen_vs.hlsl", "vs_6_0");
        bloomPreIdx = shaderCompiler.watch("bloom_prefilter_ps.hlsl", "ps_6_0");
        bloomDownIdx = shaderCompiler.watch("bloom_downsample_ps.hlsl", "ps_6_0");
        bloomUpIdx = shaderCompiler.watch("bloom_upsample_ps.hlsl", "ps_6_0");
        bloomCompIdx = shaderCompiler.watch("bloom_composite_ps.hlsl", "ps_6_0");
    }

    // Initialize 8 animated lights with random hue colors and sine/cosine coefficients
    {
        std::mt19937 lightRng(42u);
        std::uniform_real_distribution<float> hDist(0.0f, 360.0f);
        std::uniform_real_distribution<float> cDist(-1.0f, 1.0f);
        std::uniform_real_distribution<float> freqDist(0.2f, 0.8f);
        std::uniform_real_distribution<float> ampDist(3.0f, 8.0f);
        const float orbitR = 8.0f;
        for (int i = 0; i < 8; ++i) {
            float angle = (float)i * (6.2831853f / 8.0f);
            lightAnims[i].center  = { orbitR * std::cos(angle), 3.0f + cDist(lightRng) * 2.0f,
                                      orbitR * std::sin(angle) };
            lightAnims[i].ampX   = ampDist(lightRng);
            lightAnims[i].ampY   = ampDist(lightRng) * 0.5f;
            lightAnims[i].ampZ   = ampDist(lightRng);
            lightAnims[i].freqX  = freqDist(lightRng);
            lightAnims[i].freqY  = freqDist(lightRng);
            lightAnims[i].freqZ  = freqDist(lightRng);
            lightAnims[i].color = hslToLinear(hDist(lightRng), 0.9f, 0.65f);
        }
    }

    this->contentLoaded = true;
    this->resizeDepthBuffer(this->clientWidth, this->clientHeight);
    bloom.createResources(device.Get(), clientWidth, clientHeight);

    return true;
}

void Application::onResize(uint32_t width, uint32_t height)
{
    if (this->clientWidth != width || this->clientHeight != height) {
        this->clientWidth = std::max(1u, width);
        this->clientHeight = std::max(1u, height);

        this->cmdQueue.flush();
        for (int i = 0; i < this->nBuffers; ++i) {
            this->backBuffers[i].Reset();
        }

        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        chkDX(this->swapChain->GetDesc(&swapChainDesc));
        chkDX(this->swapChain->ResizeBuffers(
            this->nBuffers, this->clientWidth, this->clientHeight, swapChainDesc.BufferDesc.Format,
            swapChainDesc.Flags
        ));

        this->curBackBufIdx = this->swapChain->GetCurrentBackBufferIndex();
        this->updateRenderTargetViews(this->rtvHeap);
        this->viewport = CD3DX12_VIEWPORT(
            0.0f, 0.0f, static_cast<float>(this->clientWidth),
            static_cast<float>(this->clientHeight)
        );
        this->resizeDepthBuffer(this->clientWidth, this->clientHeight);
        bloom.resize(device.Get(), clientWidth, clientHeight);
        this->flush();
    }
}
