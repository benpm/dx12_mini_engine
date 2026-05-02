module;

#include <d3d12.h>
#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <flecs.h>
#include <gainput/gainput.h>
#include <Windows.h>
#include <wrl.h>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "d3dx12_clean.h"
#include "icons.h"

export module application;

export import window;
export import camera;
export import command_queue;
export import config;
export import input;
export import scene;
export import bloom;
export import billboard;
export import imgui_layer;
export import shader_hotreload;
export import object_picking;
export import terrain;
export import scene_file;
export import ssao;
export import shadow;
export import outline;
export import gizmo;
export import render_graph;
export import lua_scripting;
export import gbuffer;
export import gfx;

// Global application data and state
export namespace app_slots
{
    // Root signature slot assignments

    inline constexpr UINT rootPerFrameCB = 0;
    inline constexpr UINT rootPerPassCB = 1;
    inline constexpr UINT rootDrawIndex = 2;
    inline constexpr UINT rootOutlineParams = 3;
    inline constexpr UINT rootPerObjectSrv = 4;
    inline constexpr UINT rootShadowSrv = 5;
    inline constexpr UINT rootCubemapSrv = 6;
    inline constexpr UINT rootSsaoSrv = 7;

    // Note: rootPerObjectSrv is a descriptor table with one entry per back buffer, so the actual
    // SRV for drawIndex N is at rootPerObjectSrv + (N * srvSlotPerObjectBase)

    inline constexpr uint32_t srvSlotPerObjectBase = 0;
    inline constexpr uint32_t srvSlotShadow = Scene::nBuffers;
    inline constexpr uint32_t srvSlotCubemap = Scene::nBuffers + 1;
    inline constexpr uint32_t srvSlotSsao = Scene::nBuffers + 2;
}  // namespace app_slots

export class Application
{
   public:
    Application();
    ~Application();

    void update();
    void render();

    RuntimeData runtimeConfig;
    OrbitCamera cam;
    gainput::InputMap inputMap;
    gainput::DeviceId keyboardID;

    // Scene file serialization
    SceneFileData extractSceneData() const;
    void applySceneData(const SceneFileData& data);

    // Config
    void applyConfig(const ConfigData& cfg);
    ConfigData extractConfig() const;

   private:
    constexpr static uint8_t nBuffers = 3u;
    bool useWarp = false;
    uint32_t clientWidth = 1280;
    uint32_t clientHeight = 720;
    bool isInitialized = false;
    HWND hWnd;
    RECT windowRect{};
    std::unordered_set<MouseButton> pressedMouseButtons;
    vec2 mousePos;
    vec2 mouseDelta;
    // gfx abstraction owns device + swap chain (P1). Legacy ComPtr fields below
    // are obtained via the gfx native handles and remain in place during
    // P2-P13 subsystem migration so existing subsystem signatures keep working.
    std::unique_ptr<gfx::IDevice> gfxDevice;
    std::unique_ptr<gfx::ISwapChain> gfxSwapChain;

    Microsoft::WRL::ComPtr<ID3D12Device2> device;
    CommandQueue cmdQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain;
    Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers[nBuffers];
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    UINT rtvDescSize;
    UINT curBackBufIdx;

    rg::RenderGraph renderGraph;

    // Subsystems
    Scene scene;
    GBuffer gbuffer;
    BloomRenderer bloom;
    BillboardRenderer billboards;
    ImGuiLayer imguiLayer;
    ObjectPicker picker;
    SsaoRenderer ssao;
    ShadowRenderer shadow;
    OutlineRenderer outline;
    GizmoState gizmo;
    // Entity picking/selection
    flecs::entity hoveredEntity;
    flecs::entity selectedEntity;
    vec2 clickStartPos;
    bool leftClickActive = false;

    // Depth buffer + scene PSOs
    gfx::TextureHandle depthBuffer{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    // Scene + GBuffer PSOs migrated to gfx; the engine still owns the root sig
    // (descriptor-table layout) and passes it via nativeRootSignatureOverride
    // until P2 lands the bindless rewrite.
    gfx::PipelineHandle pipelineState{};
    gfx::ShaderHandle scenePsoVS{};
    gfx::ShaderHandle scenePsoPS{};
    gfx::PipelineHandle gbufferPSO{};
    gfx::ShaderHandle gbufferVS{};
    gfx::ShaderHandle gbufferPS{};

    // Infinite grid
    Microsoft::WRL::ComPtr<ID3D12RootSignature> gridRootSig;
    gfx::PipelineHandle gridPSO{};
    gfx::ShaderHandle gridVS{};
    gfx::ShaderHandle gridPS{};
    bool showGrid = true;
    float gridMajorSize = 10.0f;
    int gridSubdivisions = 10;
    void createGridPSO();
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
    float fov = 55.0f;
    mat4 matModel;
    mat4 prevViewProj;
    bool contentLoaded = false;

    // Cubemap reflections
    gfx::TextureHandle cubemapTexture{};
    gfx::TextureHandle cubemapDepth{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cubemapRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cubemapDsvHeap;
    UINT cubemapRtvDescSize = 0;
    UINT cubemapDsvDescSize = 0;
    uint32_t cubemapResolution = 128;
    bool cubemapEnabled = true;
    float cubemapNearPlane = 0.1f;
    float cubemapFarPlane = 100.0f;
    void createCubemapResources();

    // Bloom / post-processing UI params
    float bloomThreshold = 1.7f;
    float bloomIntensity = 0.1f;
    int tonemapMode = 2;

    // GUI-controlled scene parameters
    vec3 bgColor;
    float lightBrightness = 0.1f;
    float ambientBrightness = 0.01f;
    // Directional light
    vec3 dirLightDir{ 0.5f, -0.8f, 0.3f };  // direction FROM light (negated in shader)
    float dirLightBrightness = 3.0f;
    vec3 dirLightColor{ 1.0f, 0.95f, 0.85f };  // warm white
    // Ocean fog
    float fogStartY = -4.0f;
    float fogDensity = 0.4f;
    vec3 fogColor{ 0.1f, 0.35f, 0.45f };  // ocean teal
    char gltfPathBuf[512] = "";
    char scenePathBuf[512] = "";
    char sceneTitleBuf[256] = "";
    char sceneDescBuf[512] = "";
    std::string sceneTitle;
    std::string sceneDescription;
    bool pendingResetToTeapot{ false };
    std::string pendingGltfPath;
    std::string pendingSceneLoad;
    std::string pendingSceneSave;

    uint64_t frameFenceValues[nBuffers] = {};

    float lightTime = 0.0f;

    bool vsync = true;
    bool tearingSupported = false;
    bool fullscreen = false;
    bool pendingFullscreenChange = false;
    bool pendingFullscreenValue = false;
    bool isResizing = false;
    int frameCount = 0;
    uint32_t lastFrameObjectCount = 0;
    uint32_t lastFrameVertexCount = 0;
    uint32_t lastFrameDrawCalls = 0;
    float lastFrameMs = 0.0f;
    float recentFrameMs[3] = {};
    // FPS history for chart (5 seconds at ~60fps = 300 samples)
    static constexpr int fpsHistorySize = 300;
    float fpsHistory[fpsHistorySize] = {};
    int fpsHistoryHead = 0;
    bool showMetrics = true;
    int recentFrameHead = 0;
    bool spawningStopped = false;
    bool autoStopSpawning = true;
    float spawnStopFrameMs = 10.0f;
    int spawnBatchSize = 50;
    bool animateEntities = true;
    float lightAnimationSpeed = 1.0f;
    bool showLightBillboards = true;

    // Create Entity panel state
    int createMeshIdx = 0;
    int createMatIdx = 0;
    vec3 createPos;
    float createScale = 1.0f;
    bool createAnimated = false;
    float createAnimSpeed = 1.0f;
    float createAnimRadius = 5.0f;
    bool pendingCreateEntity = false;

    // Deferred ECS mutations (set from UI, applied in update() before queries)
    std::optional<Animated> pendingAddAnimated;
    bool pendingAddPickable = false;
    bool pendingDeleteSelected = false;

    // Hotkeys
    HotkeyBindings hotkeys;
    std::unordered_map<UINT, bool> prevKeyStates;

    // Icons (config-driven, maps UI element key → icon name)
    std::map<std::string, std::string> iconConfig;
    // Cached icon UTF-8 prefix strings (built from iconConfig)
    std::unordered_map<std::string, std::string> iconCache;
    void rebuildIconCache();
    std::string iconLabel(const char* key, const char* label) const;

    // Lua scripting
    LuaScripting luaScripting;

    gainput::DeviceId mouseID, rawMouseID;

    // Shader hot reload
    ShaderCompiler shaderCompiler;
    size_t sceneVSIdx = 0;
    size_t scenePSIdx = 0;
    size_t outlineVSIdx = 0;
    size_t outlinePSIdx = 0;
    size_t bloomFsVsIdx = 0;
    size_t bloomPreIdx = 0;
    size_t bloomDownIdx = 0;
    size_t bloomUpIdx = 0;
    size_t bloomCompIdx = 0;
    size_t gridVSIdx = 0;
    size_t gridPSIdx = 0;

    void transitionResource(
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> cmdList,
        Microsoft::WRL::ComPtr<ID3D12Resource> resource,
        D3D12_RESOURCE_STATES beforeState,
        D3D12_RESOURCE_STATES afterState
    );
    void clearRTV(
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        FLOAT clearColor[4]
    );
    void clearDepth(
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv,
        FLOAT depth = 1.0f
    );
    void resizeDepthBuffer(uint32_t width, uint32_t height);
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>
    createDescHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors);
    void updateRenderTargetViews(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap);
    void setFullscreen(bool val);
    void flush();
    bool loadContent();
    void onResize(uint32_t width, uint32_t height);
    void createScenePSO();
    void createGBufferPSO();
    void renderImGui(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> cmdList);
};
