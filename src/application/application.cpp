module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <d3d12.h>
#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <flecs.h>
#include <gainput/gainput.h>
#include <imgui.h>
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
#include "icons.h"
#include "profiling.h"
#include "resource.h"
#include "vertex_shader_cso.h"

module application;

#ifdef TRACY_ENABLE
TracyD3D12Ctx g_tracyD3d12Ctx = nullptr;
#else
void* g_tracyD3d12Ctx = nullptr;
#endif

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

Application::Application()
    : inputMap(inputManager, "input_map"),
      // gfx device must exist before renderGraph captures the native pointer.
      gfxDevice(([] {
          auto* w = Window::get();
          gfx::DeviceDesc dd{};
          dd.useWarp = w->useWarp;
#if defined(_DEBUG)
          dd.enableDebugLayer = ::IsDebuggerPresent() != 0;
#endif
          return gfx::createDevice(gfx::BackendKind::D3D12, dd);
      })()),
      renderGraph(*gfxDevice)
{
    spdlog::info("Application constructor start");

    auto* win = Window::get();
    this->hWnd = win->hWnd;
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

    this->viewport = gfx::Viewport{ 0.0f, 0.0f, static_cast<float>(this->clientWidth),
                                    static_cast<float>(this->clientHeight) };
    this->scissorRect = gfx::ScissorRect{ 0, 0, static_cast<int32_t>(this->clientWidth),
                                          static_cast<int32_t>(this->clientHeight) };

    spdlog::info("Creating CommandQueue (adopting gfx queue)");
    {
        auto* gfxQueueNative =
            static_cast<ID3D12CommandQueue*>(this->gfxDevice->graphicsQueue()->nativeHandle());
        this->cmdQueue = CommandQueue(
            ComPtr<ID3D12Device2>(static_cast<ID3D12Device2*>(gfxDevice->nativeHandle())),
            ComPtr<ID3D12CommandQueue>(gfxQueueNative), D3D12_COMMAND_LIST_TYPE_DIRECT
        );
    }

    spdlog::info("Creating SwapChain via gfx");
    {
        gfx::SwapChainDesc sd{};
        sd.nativeWindowHandle = this->hWnd;
        sd.width = this->clientWidth;
        sd.height = this->clientHeight;
        sd.bufferCount = this->nBuffers;
        sd.format = gfx::Format::RGBA8Unorm;
        sd.allowTearing = this->tearingSupported;
        this->gfxSwapChain = this->gfxDevice->createSwapChain(sd);
    }
    this->curBackBufIdx = this->gfxSwapChain->currentIndex();

    // Back-buffer handles come from the swap chain (RTVs created by the gfx backend).
    for (int i = 0; i < this->nBuffers; ++i) {
        this->backBuffers[i] = this->gfxSwapChain->backBufferAt(i);
    }

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
    this->inputMap.MapBool(Button::Exit, this->keyboardID, gainput::KeyEscape);

    hotkeys.setDefaults();

    this->loadContent();
    this->flush();

    // Initialize Lua scripting
#ifndef SCRIPTS_DIR
    #define SCRIPTS_DIR ""
#endif
    luaScripting.init(scene, SCRIPTS_DIR);
    std::string actionsPath = std::string(SCRIPTS_DIR) + "/actions.json";
    luaScripting.loadActionBindings(actionsPath);
    this->imguiLayer.init(
        hWnd, *gfxDevice, cmdQueue.queue.Get(), nBuffers, gfx::Format::RGBA8Unorm
    );

    // Fullscreen applied later via applyConfig (after message loop starts).

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

    luaScripting.shutdown();

    this->flush();
#ifdef TRACY_ENABLE
    if (g_tracyD3d12Ctx) {
        TracyD3D12Destroy(g_tracyD3d12Ctx);
        g_tracyD3d12Ctx = nullptr;
    }
#endif
    this->imguiLayer.shutdown();

    if (gfxDevice) {
        if (pipelineState.isValid()) {
            gfxDevice->destroy(pipelineState);
        }
        if (scenePsoVS.isValid()) {
            gfxDevice->destroy(scenePsoVS);
        }
        if (scenePsoPS.isValid()) {
            gfxDevice->destroy(scenePsoPS);
        }
        if (gridPSO.isValid()) {
            gfxDevice->destroy(gridPSO);
        }
        if (gridVS.isValid()) {
            gfxDevice->destroy(gridVS);
        }
        if (gridPS.isValid()) {
            gfxDevice->destroy(gridPS);
        }
        if (gbufferPSO.isValid()) {
            gfxDevice->destroy(gbufferPSO);
        }
        if (gbufferVS.isValid()) {
            gfxDevice->destroy(gbufferVS);
        }
        if (gbufferPS.isValid()) {
            gfxDevice->destroy(gbufferPS);
        }
        if (depthBuffer.isValid()) {
            gfxDevice->destroy(depthBuffer);
        }
        if (cubemapTexture.isValid()) {
            gfxDevice->destroy(cubemapTexture);
        }
        if (cubemapDepth.isValid()) {
            gfxDevice->destroy(cubemapDepth);
        }
    }
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

void Application::applyConfig(const ConfigData& cfg)
{
    if (cfg.startFullscreen) {
        pendingFullscreenChange = true;
        pendingFullscreenValue = true;
    }
    vsync = cfg.vsync;
    tonemapMode = cfg.tonemapMode;
    bloomThreshold = cfg.bloomThreshold;
    bloomIntensity = cfg.bloomIntensity;
    ssao.enabled = cfg.ssaoEnabled;
    ssao.radius = cfg.ssaoRadius;
    ssao.bias = cfg.ssaoBias;
    ssao.kernelSize = cfg.ssaoKernelSize;
    shadow.enabled = cfg.shadowsEnabled;
    cubemapEnabled = cfg.cubemapEnabled;
    cubemapResolution = cfg.cubemapResolution;
    showGrid = cfg.showGrid;
    gridMajorSize = cfg.gridMajorSize;
    gridSubdivisions = cfg.gridSubdivisions;
    showMetrics = cfg.showMetrics;
    showLightBillboards = cfg.showLightBillboards;
    animateEntities = cfg.animateEntities;
    lightAnimationSpeed = cfg.lightAnimationSpeed;
    autoStopSpawning = cfg.autoStopSpawning;
    spawnStopFrameMs = cfg.spawnStopFrameMs;
    spawnBatchSize = cfg.spawnBatchSize;

    // Apply icon config
    iconConfig = cfg.icons;
    rebuildIconCache();

    // Apply hotkey bindings from config
    hotkeys.setDefaults();
    for (const auto& [actionName, keyNames] : cfg.hotkeys) {
        for (int i = 0; i < static_cast<int>(EditorAction::Count); ++i) {
            auto action = static_cast<EditorAction>(i);
            if (actionName == editorActionName(action)) {
                hotkeys.bindings[action].clear();
                for (const auto& kn : keyNames) {
                    hotkeys.bindings[action].push_back(keyFromName(kn));
                }
                break;
            }
        }
    }
}

ConfigData Application::extractConfig() const
{
    ConfigData cfg;
    cfg.windowWidth = clientWidth;
    cfg.windowHeight = clientHeight;
    cfg.vsync = vsync;
    cfg.tonemapMode = tonemapMode;
    cfg.bloomThreshold = bloomThreshold;
    cfg.bloomIntensity = bloomIntensity;
    cfg.ssaoEnabled = ssao.enabled;
    cfg.ssaoRadius = ssao.radius;
    cfg.ssaoBias = ssao.bias;
    cfg.ssaoKernelSize = ssao.kernelSize;
    cfg.shadowsEnabled = shadow.enabled;
    cfg.cubemapEnabled = cubemapEnabled;
    cfg.cubemapResolution = cubemapResolution;
    cfg.showGrid = showGrid;
    cfg.gridMajorSize = gridMajorSize;
    cfg.gridSubdivisions = gridSubdivisions;
    cfg.showMetrics = showMetrics;
    cfg.showLightBillboards = showLightBillboards;
    cfg.animateEntities = animateEntities;
    cfg.lightAnimationSpeed = lightAnimationSpeed;
    cfg.autoStopSpawning = autoStopSpawning;
    cfg.spawnStopFrameMs = spawnStopFrameMs;
    cfg.spawnBatchSize = spawnBatchSize;

    // Extract hotkey bindings to config
    cfg.hotkeys.clear();
    for (const auto& [action, keys] : hotkeys.bindings) {
        std::vector<std::string> keyNames;
        for (Key k : keys) {
            keyNames.push_back(keyName(k));
        }
        cfg.hotkeys[editorActionName(action)] = std::move(keyNames);
    }
    cfg.icons = iconConfig;
    return cfg;
}

// ---------------------------------------------------------------------------
// Icon helpers
// ---------------------------------------------------------------------------

void Application::rebuildIconCache()
{
    iconCache.clear();
    for (const auto& [key, name] : iconConfig) {
        iconCache[key] = iconStr(name);
    }
}

std::string Application::iconLabel(const char* key, const char* label) const
{
    auto it = iconCache.find(key);
    if (it != iconCache.end() && !it->second.empty()) {
        return it->second + label;
    }
    return label;
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

    if (depthBuffer.isValid()) {
        gfxDevice->destroy(depthBuffer);
    }
    gfx::TextureDesc td{};
    td.width = width;
    td.height = height;
    td.format = gfx::Format::R32G8X24Typeless;
    td.viewFormat = gfx::Format::D32FloatS8X24Uint;
    // SSAO reads depth as an SRV. We just need DepthStencil here — gfx no
    // longer adds DENY_SHADER_RESOURCE automatically, and the typeless
    // resource format means gfx skips auto-SRV creation; the engine
    // creates its own typed SRV inside SsaoRenderer.
    td.usage = gfx::TextureUsage::DepthStencil;
    td.initialState = gfx::ResourceState::DepthWrite;
    td.useClearValue = true;
    td.clearDepth = 1.0f;
    td.clearStencil = 0;
    td.debugName = "depth_buffer";
    depthBuffer = gfxDevice->createTexture(td);
    // DSV is created automatically by the gfx backend (TextureUsage::DepthStencil).
}

// ---------------------------------------------------------------------------
// Update / render
// ---------------------------------------------------------------------------

void Application::update()
{
    PROFILE_ZONE();

    // Store previous transforms for motion vectors
    scene.ecsWorld.query<const Transform, PrevTransform>().each(
        [](const Transform& tf, PrevTransform& ptf) { ptf.world = tf.world; }
    );
    scene.ecsWorld.query<const InstanceGroup, PrevInstanceGroup>().each(
        [](const InstanceGroup& ig, PrevInstanceGroup& pig) { pig.transforms = ig.transforms; }
    );

    scene.ecsWorld.defer_begin();
    // Ensure new entities have previous transforms
    scene.ecsWorld.query<const Transform>().each([](flecs::entity e, const Transform& tf) {
        if (!e.has<PrevTransform>()) {
            e.set<PrevTransform>({ tf.world });
        }
    });
    scene.ecsWorld.query<const InstanceGroup>().each([](flecs::entity e, const InstanceGroup& ig) {
        if (!e.has<PrevInstanceGroup>()) {
            e.set<PrevInstanceGroup>({ ig.transforms });
        }
    });
    scene.ecsWorld.defer_end();

    if (pendingFullscreenChange) {
        const bool targetFullscreen = pendingFullscreenValue;
        pendingFullscreenChange = false;
        setFullscreen(targetFullscreen);
    }

    // Process deferred scene loads (set from ImGui, safe here before render commands)
    if (pendingResetToTeapot) {
        pendingResetToTeapot = false;
        scene.clearScene(cmdQueue);
        scene.loadTeapot(*gfxDevice, cmdQueue);
        gizmo.init(scene, *gfxDevice, cmdQueue);
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
        if (!scene.loadGltf(path, *gfxDevice, cmdQueue)) {
            spdlog::error("Failed to load GLB: {}", path);
        } else {
            gizmo.init(scene, *gfxDevice, cmdQueue);
        }
    }

    if (!pendingSceneLoad.empty()) {
        std::string path = std::move(pendingSceneLoad);
        pendingSceneLoad.clear();
        SceneFileData data;
        if (loadSceneFile(path, data)) {
            scene.clearScene(cmdQueue);
            scene.loadTeapot(*gfxDevice, cmdQueue);
            for (const auto& entry : std::filesystem::directory_iterator(MODELS_DIR)) {
                if (entry.path().extension() == ".glb") {
                    scene.loadGltf(entry.path().string(), *gfxDevice, cmdQueue, true);
                }
            }
            applySceneData(data);
            if (!runtimeConfig.singleTeapotMode) {
                gizmo.init(scene, *gfxDevice, cmdQueue);
            }
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

    // Process deferred ECS mutations from UI
    if (pendingCreateEntity && !scene.spawnableMeshRefs.empty()) {
        int mi = std::clamp(createMeshIdx, 0, (int)scene.spawnableMeshRefs.size() - 1);
        MeshRef mesh = scene.spawnableMeshRefs[mi];
        mesh.materialIndex = std::clamp(createMatIdx, 0, (int)scene.materials.size() - 1);
        Transform tf;
        tf.world = scale(createScale) * translate(createPos);
        auto e = scene.ecsWorld.entity().set(tf).set(mesh).add<Pickable>();
        if (createAnimated) {
            Animated anim{};
            anim.speed = createAnimSpeed;
            anim.orbitRadius = createAnimRadius;
            anim.orbitY = createPos.y;
            anim.initialScale = createScale;
            anim.pulsePhase = static_cast<float>(scene.rng() % 1000) / 1000.0f * 6.28f;
            e.set(anim);
        }
        selectedEntity = e;
    }
    pendingCreateEntity = false;

    if (pendingAddAnimated && selectedEntity.is_alive()) {
        selectedEntity.set(*pendingAddAnimated);
    }
    pendingAddAnimated.reset();

    if (pendingAddPickable && selectedEntity.is_alive()) {
        selectedEntity.add<Pickable>();
    }
    pendingAddPickable = false;

    if (pendingDeleteSelected && selectedEntity.is_alive()) {
        if (selectedEntity.has<Scripted>()) {
            luaScripting.detachScript(selectedEntity);
        }
        selectedEntity.destruct();
        selectedEntity = flecs::entity{};
    }
    pendingDeleteSelected = false;

    static std::chrono::high_resolution_clock clock;
    static auto t0 = clock.now();
    auto t1 = clock.now();
    auto deltaTime = t1 - t0;
    t0 = t1;
    const float dt = static_cast<float>(static_cast<double>(deltaTime.count()) * 1e-9);

    lightTime += dt * std::max(0.0f, lightAnimationSpeed);
    lastFrameMs = dt * 1000.0f;
    fpsHistory[fpsHistoryHead % fpsHistorySize] = 1.0f / std::max(dt, 1e-6f);
    ++fpsHistoryHead;
    recentFrameMs[recentFrameHead % 3] = lastFrameMs;
    ++recentFrameHead;
    if (autoStopSpawning && !spawningStopped && recentFrameHead >= 3 &&
        recentFrameMs[0] > spawnStopFrameMs && recentFrameMs[1] > spawnStopFrameMs &&
        recentFrameMs[2] > spawnStopFrameMs) {
        spawningStopped = true;
    }

    // Lua scripting update + hot reload poll (every ~1s)
    luaScripting.selectedEntityId = selectedEntity.is_alive() ? selectedEntity.id() : 0;
    luaScripting.updateScriptedEntities(dt, lightTime, frameCount);
    static float luaHotReloadTimer = 0.0f;
    luaHotReloadTimer += dt;
    if (luaHotReloadTimer >= 1.0f) {
        luaHotReloadTimer = 0.0f;
        luaScripting.pollHotReload();
    }
    // If a script destroyed the selected entity, clear selection
    if (selectedEntity.is_alive() == false) {
        selectedEntity = flecs::entity{};
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
        bool gridChanged =
            shaderCompiler.wasRecompiled(gridVSIdx) || shaderCompiler.wasRecompiled(gridPSIdx);

        // Wrap each PSO recreation in try/catch — on failure, keep the current PSO
        if (sceneChanged) {
            try {
                createScenePSO();
                createGBufferPSO();
                auto vsData = shaderCompiler.data(sceneVSIdx);
                gfx::ShaderBytecode vs =
                    vsData ? gfx::ShaderBytecode{ vsData, shaderCompiler.size(sceneVSIdx) }
                           : gfx::ShaderBytecode{};
                shadow.reloadPSO(*gfxDevice, rootSignature.Get(), vs);
            } catch (const std::exception& e) {
                spdlog::error("Hot reload PSO failed (scene): {}", e.what());
            }
        }
        if (outlineChanged) {
            try {
                auto vsData = shaderCompiler.data(outlineVSIdx);
                auto psData = shaderCompiler.data(outlinePSIdx);
                gfx::ShaderBytecode vs =
                    vsData ? gfx::ShaderBytecode{ vsData, shaderCompiler.size(outlineVSIdx) }
                           : gfx::ShaderBytecode{};
                gfx::ShaderBytecode ps =
                    psData ? gfx::ShaderBytecode{ psData, shaderCompiler.size(outlinePSIdx) }
                           : gfx::ShaderBytecode{};
                outline.reloadPSO(*gfxDevice, rootSignature.Get(), vs, ps);
            } catch (const std::exception& e) {
                spdlog::error("Hot reload PSO failed (outline): {}", e.what());
            }
        }
        if (gridChanged) {
            try {
                createGridPSO();
            } catch (const std::exception& e) {
                spdlog::error("Hot reload PSO failed (grid): {}", e.what());
            }
        }
        if (bloomChanged) {
            try {
                auto bc = [&](size_t idx) -> gfx::ShaderBytecode {
                    auto d = shaderCompiler.data(idx);
                    return d ? gfx::ShaderBytecode{ d, shaderCompiler.size(idx) }
                             : gfx::ShaderBytecode{};
                };
                bloom.reloadPipelines(
                    *gfxDevice, bc(bloomFsVsIdx), bc(bloomPreIdx), bc(bloomDownIdx), bc(bloomUpIdx),
                    bc(bloomCompIdx)
                );
            } catch (const std::exception& e) {
                spdlog::error("Hot reload PSO failed (bloom): {}", e.what());
            }
        }
    }

    // Spawn entities
    if (!runtimeConfig.singleTeapotMode && !scene.spawnableMeshRefs.empty()) {
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

    // Create material test grids (once, on first frame with meshes loaded)
    if (false) {
        constexpr int G = 5;
        constexpr float spacing = 2.4f;
        constexpr float objScale = 0.45f;
        constexpr float rotSpeed = 0.3f;
        // Grid centers spread along X axis
        const float gridCentersX[3] = { -10.0f, 0.0f, 10.0f };

        int numGrids = std::min(3, static_cast<int>(scene.spawnableMeshRefs.size()));
        // Always create all 3 grids even with 1 mesh (reuse mesh 0)
        numGrids = 3;

        // Emissive material for grid 3: orange glow
        int emissiveMatIdx = static_cast<int>(scene.materials.size());
        {
            Material emMat;
            emMat.name = "GridEmissive";
            emMat.albedo = { 0.6f, 0.2f, 0.05f, 1.0f };
            emMat.roughness = 0.5f;
            emMat.metallic = 0.0f;
            emMat.emissiveStrength = 0.0f;  // overridden per-instance
            emMat.emissive = { 1.0f, 0.45f, 0.1f, 0.0f };
            scene.materials.push_back(emMat);
        }

        struct GridDef
        {
            int meshIdx;
            vec4 albedo;
            int matIdx;
            bool varyEmissive;  // false = roughness×metallic, true = roughness×emissive
        };
        int diffMatIdx = scene.presetIdx[static_cast<int>(MaterialPreset::Diffuse)];
        if (diffMatIdx < 0) {
            diffMatIdx = 0;
        }

        GridDef defs[3] = {
            { 0, { 0.9f, 0.85f, 0.8f, 1.0f }, diffMatIdx, false },
            { std::min(1, static_cast<int>(scene.spawnableMeshRefs.size()) - 1),
              { 0.4f, 0.6f, 1.0f, 1.0f },
              diffMatIdx,
              false },
            { std::min(2, static_cast<int>(scene.spawnableMeshRefs.size()) - 1),
              { 0.6f, 0.2f, 0.05f, 1.0f },
              emissiveMatIdx,
              true },
        };

        for (int g = 0; g < numGrids; ++g) {
            const GridDef& def = defs[g];

            InstanceGroup group;
            group.mesh = scene.spawnableMeshRefs[def.meshIdx];
            group.mesh.materialIndex = def.matIdx;

            InstanceAnimation anim;
            anim.rotationSpeed = rotSpeed;

            for (int gx = 0; gx < G; ++gx) {
                for (int gz = 0; gz < G; ++gz) {
                    float px = gridCentersX[g] + (gx - G / 2) * spacing;
                    float pz = (gz - G / 2) * spacing;
                    float roughness = static_cast<float>(gx) / (G - 1);
                    float varParam = static_cast<float>(gz) / (G - 1);  // metallic or emissive

                    group.albedoOverrides.push_back(def.albedo);
                    group.roughnessOverrides.push_back(roughness);
                    if (def.varyEmissive) {
                        group.metallicOverrides.push_back(0.0f);
                        group.emissiveStrengthOverrides.push_back(varParam * 2.0f);
                    } else {
                        group.metallicOverrides.push_back(varParam);
                        group.emissiveStrengthOverrides.push_back(0.0f);
                    }

                    anim.positions.push_back({ px, 0.0f, pz });
                    anim.scales.push_back(objScale);
                    group.transforms.push_back(
                        scale(objScale, objScale, objScale) * translate(px, 0.0f, pz)
                    );
                }
            }

            Transform tf;
            tf.world = mat4{};
            scene.ecsWorld.entity().set(tf).set(std::move(group)).set(std::move(anim));
        }
    }

    const float w = static_cast<float>(this->clientWidth);

    this->mouseDelta = { this->inputMap.GetFloatDelta(Button::AxisDeltaX),
                         this->inputMap.GetFloatDelta(Button::AxisDeltaY) };
    this->mousePos = { this->inputMap.GetFloat(Button::AxisX),
                       this->inputMap.GetFloat(Button::AxisY) };

    // --- Entity picking from ID buffer ---
    const bool imguiCaptureMouse = ImGui::GetIO().WantCaptureMouse;
    if (!imguiCaptureMouse) {
        hoveredEntity = flecs::entity{};
        uint32_t pickVal = picker.pickedIndex;
        // ID shader writes drawIndex+1, so 0 means "no entity"
        if (pickVal > 0) {
            uint32_t pickIdx = pickVal - 1;
            if (pickIdx < scene.drawIndexToEntity.size()) {
                hoveredEntity = scene.drawIndexToEntity[pickIdx];
            }
        }

        bool leftDown = this->inputMap.GetBool(Button::LeftClick);

        // Update gizmo (drag detection + entity translation)
        gizmo.update(
            scene, selectedEntity, cam, viewport, mousePos, leftDown, leftClickActive,
            picker.pickedIndex
        );

        if (leftDown && !leftClickActive) {
            clickStartPos = mousePos;
            leftClickActive = true;
        }
        if (!leftDown && leftClickActive) {
            leftClickActive = false;
            float dx = mousePos.x - clickStartPos.x;
            float dy = mousePos.y - clickStartPos.y;
            // Suppress selection when clicking on gizmo arrow or dragging gizmo
            if (dx * dx + dy * dy < 9.0f && !gizmo.isGizmoEntity(hoveredEntity)) {
                selectedEntity = hoveredEntity;
            }
        }
    }

    this->matModel = mat4{};
    if (!imguiCaptureMouse && !gizmo.isDragging() && this->inputMap.GetBool(Button::LeftClick)) {
        this->cam.pitch += (this->mouseDelta.y / w) * 180_deg;
        this->cam.yaw -= (this->mouseDelta.x / w) * 360_deg;
        this->cam.pitch = std::clamp(this->cam.pitch, -89.9_deg, 89.9_deg);
    }
    if (!imguiCaptureMouse && this->inputMap.GetBool(Button::RightClick)) {
        this->cam.radius += (this->mouseDelta.y / w) * this->cam.radius;
    }
    this->cam.aspectRatio = w / static_cast<float>(this->clientHeight);
    if (!imguiCaptureMouse && this->inputMap.GetBoolWasDown(Button::ScrollUp)) {
        this->cam.radius *= 0.8f;
    }
    if (!imguiCaptureMouse && this->inputMap.GetBoolWasDown(Button::ScrollDown)) {
        this->cam.radius *= 1.25f;
    }
    this->cam.radius = std::clamp(this->cam.radius, 0.1f, 1000.0f);

    // Hotkey actions (only when ImGui isn't capturing keyboard)
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        if (hotkeys.wasPressed(EditorAction::ToggleFullscreen, prevKeyStates)) {
            pendingFullscreenValue = !fullscreen;
            pendingFullscreenChange = true;
        }
        if (hotkeys.wasPressed(EditorAction::DeleteEntity, prevKeyStates)) {
            if (selectedEntity.is_alive()) {
                pendingDeleteSelected = true;
            }
        }
        if (hotkeys.wasPressed(EditorAction::Deselect, prevKeyStates)) {
            if (selectedEntity.is_alive()) {
                selectedEntity = flecs::entity{};
            } else {
                int result = ::MessageBoxW(
                    hWnd, L"Are you sure you want to exit?", L"Confirm Exit",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2
                );
                if (result == IDYES) {
                    Window::get()->doExit = true;
                }
            }
        }
    }
    hotkeys.updateKeyStates(prevKeyStates);

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

        // Animate instanced groups: spin each instance around its own Y axis
        scene.instanceAnimQuery.each([&](InstanceGroup& group, InstanceAnimation& ia) {
            ia.currentAngle += ia.rotationSpeed * dt;
            mat4 rot = rotateAxis(vec3(0.f, 1.f, 0.f), ia.currentAngle);
            for (size_t i = 0; i < group.transforms.size(); ++i) {
                float s = ia.scales[i];
                vec3 p = ia.positions[i];
                group.transforms[i] = scale(s, s, s) * rot * translate(p.x, p.y, p.z);
            }
        });
    }

    scene.updateLightBuffer(*gfxDevice, cmdQueue);
}

// ---------------------------------------------------------------------------
// Fullscreen
// ---------------------------------------------------------------------------

void Application::setFullscreen(bool val)
{
    if (this->fullscreen != val) {
        this->isResizing = true;
        this->fullscreen = val;
        if (this->fullscreen) {
            if (::GetWindowRect(this->hWnd, &this->windowRect) == 0) {
                throwLastWin32Error("GetWindowRect");
            }

            UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                                                       WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
            ::SetLastError(0);
            [[maybe_unused]] LONG prevStyle =
                ::SetWindowLongW(this->hWnd, GWL_STYLE, static_cast<LONG>(windowStyle));
            if (prevStyle == 0 && ::GetLastError() != 0) {
                throwLastWin32Error("SetWindowLongW(enter fullscreen)");
            }

            HMONITOR hMonitor = ::MonitorFromWindow(this->hWnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFOEX monitorInfo = {};
            monitorInfo.cbSize = sizeof(MONITORINFOEX);
            if (::GetMonitorInfo(hMonitor, &monitorInfo) == 0) {
                throwLastWin32Error("GetMonitorInfoW");
            }

            if (::SetWindowPos(
                    this->hWnd, HWND_TOP, monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
                    monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                    monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                    SWP_FRAMECHANGED | SWP_NOACTIVATE
                ) == 0) {
                throwLastWin32Error("SetWindowPos(enter fullscreen)");
            }
            ::ShowWindow(this->hWnd, SW_MAXIMIZE);
        } else {
            ::SetLastError(0);
            [[maybe_unused]] LONG prevStyle =
                ::SetWindowLongW(this->hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
            if (prevStyle == 0 && ::GetLastError() != 0) {
                throwLastWin32Error("SetWindowLongW(exit fullscreen)");
            }

            if (::SetWindowPos(
                    this->hWnd, HWND_NOTOPMOST, this->windowRect.left, this->windowRect.top,
                    this->windowRect.right - this->windowRect.left,
                    this->windowRect.bottom - this->windowRect.top,
                    SWP_FRAMECHANGED | SWP_NOACTIVATE
                ) == 0) {
                throwLastWin32Error("SetWindowPos(exit fullscreen)");
            }
            ::ShowWindow(this->hWnd, SW_NORMAL);
        }

        this->isResizing = false;

        RECT clientRect = {};
        if (::GetClientRect(this->hWnd, &clientRect) == 0) {
            throwLastWin32Error("GetClientRect");
        }

        this->onResize(
            static_cast<uint32_t>(std::max<LONG>(1, clientRect.right - clientRect.left)),
            static_cast<uint32_t>(std::max<LONG>(1, clientRect.bottom - clientRect.top))
        );
    }
}

void Application::flush()
{
    this->cmdQueue.flush();
}

// createScenePSO, createShadowPSO, createCubemapResources, loadContent, onResize
// are in application_setup.cpp
