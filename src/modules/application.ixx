module;

#include <Windows.h>
#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch"
#endif
#include "d3dx12.h"
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
#include <gainput/gainput.h>
#include <unordered_set>
#include <string>

export module application;

export import camera;
export import command_queue;
export import input;
export import scene;
export import bloom;
export import imgui_layer;
export import shader_hotreload;

export class Application
{
   public:
    constexpr static uint8_t nBuffers = 3u;
    bool useWarp = false;
    uint32_t clientWidth = 1280;
    uint32_t clientHeight = 720;
    bool isInitialized = false;
    HWND hWnd;
    RECT windowRect;
    std::unordered_set<Key> pressedKeys;
    std::unordered_set<MouseButton> pressedMouseButtons;
    vec2 mousePos;
    vec2 mouseDelta;
    Microsoft::WRL::ComPtr<ID3D12Device2> device;
    CommandQueue cmdQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain;
    Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers[nBuffers];
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    UINT rtvDescSize;
    UINT curBackBufIdx;

    // Subsystems
    Scene scene;
    BloomRenderer bloom;
    ImGuiLayer imguiLayer;

    // Depth buffer + scene PSO
    Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
    float fov = 45.0f;
    mat4 matModel;
    OrbitCamera cam;
    bool contentLoaded = false;

    // Shadow mapping
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowMap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shadowDsvHeap;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPSO;
    bool shadowEnabled = true;
    float shadowBias = 0.002f;
    static constexpr uint32_t shadowMapSize = 2048;

    // Bloom / post-processing UI params
    float bloomThreshold = 1.7f;
    float bloomIntensity = 0.1f;
    int tonemapMode = 1;

    // GUI-controlled scene parameters
    float bgColor[3] = { 0.0f, 0.0f, 0.0f };
    float lightBrightness = 0.4f;
    float ambientBrightness = 0.01f;
    // Directional light
    float dirLightDir[3] = { 0.5f, -0.8f, 0.3f };  // direction FROM light (negated in shader)
    float dirLightBrightness = 3.0f;
    float dirLightColor[3] = { 1.0f, 0.95f, 0.85f };  // warm white
    char gltfPathBuf[512] = "";
    bool pendingResetToTeapot{ false };
    std::string pendingGltfPath;

    uint64_t frameFenceValues[nBuffers] = {};

    // Animated lights
    struct LightAnim
    {
        vec3 center;
        float ampX, ampY, ampZ;
        float freqX, freqY, freqZ;
        vec4 color;  // rgb = hdr color (pre-multiplied brightness)
    };
    LightAnim lightAnims[8] = {};
    float lightTime = 0.0f;

    bool vsync = true;
    bool tearingSupported = false;
    bool fullscreen = false;
    bool testMode = false;
    int frameCount = 0;
    uint32_t lastFrameObjectCount = 0;
    uint32_t lastFrameVertexCount = 0;
    float lastFrameMs = 0.0f;
    float recentFrameMs[3] = {};
    int recentFrameHead = 0;
    bool spawningStopped = false;

    gainput::InputMap inputMap;
    gainput::DeviceId keyboardID, mouseID, rawMouseID;

    // Shader hot reload
    ShaderCompiler shaderCompiler;
    size_t sceneVSIdx = 0;
    size_t scenePSIdx = 0;
    size_t bloomFsVsIdx = 0;
    size_t bloomPreIdx = 0;
    size_t bloomDownIdx = 0;
    size_t bloomUpIdx = 0;
    size_t bloomCompIdx = 0;

    Application();
    ~Application();

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
    Microsoft::WRL::ComPtr<IDXGISwapChain4> createSwapChain();
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>
    createDescHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors);
    void updateRenderTargetViews(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap);
    void update();
    void render();
    void setFullscreen(bool val);
    void flush();
    bool loadContent();
    void onResize(uint32_t width, uint32_t height);
    void createScenePSO();
    void createShadowPSO();
    void renderImGui(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> cmdList);
};
