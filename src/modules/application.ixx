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
#include <flecs.h>
#include <random>
#include <unordered_set>
#include <vector>
#include <string>

export module application;

export import camera;
export import command_queue;
export import ecs_components;
export import input;

// ---------------------------------------------------------------------------
// GPU vertex layout (PBR)
// ---------------------------------------------------------------------------
export struct VertexPBR
{
    vec3 position;
    vec3 normal;
    vec2 uv;
};

// ---------------------------------------------------------------------------
// Per-draw data (stored in a StructuredBuffer, indexed by drawIndex)
// ---------------------------------------------------------------------------
export struct SceneConstantBuffer
{
    mat4 model;
    mat4 viewProj;
    vec4 cameraPos;
    vec4 lightPos;
    vec4 lightColor;
    vec4 ambientColor;
    // PBR material
    vec4 albedo;
    float roughness;
    float metallic;
    float emissiveStrength;
    float _pad;
    vec4 emissive;
};

// ---------------------------------------------------------------------------
// CPU-side material
// ---------------------------------------------------------------------------
export struct Material
{
    vec4 albedo{ 0.8f, 0.8f, 0.8f, 1.0f };
    float roughness{ 0.4f };
    float metallic{ 0.0f };
    float emissiveStrength{ 0.0f };
    float _pad{ 0.0f };
    vec4 emissive{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::string name;
};

// ---------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------
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
    ComPtr<ID3D12Device2> device;
    CommandQueue cmdQueue;
    ComPtr<IDXGISwapChain4> swapChain;
    ComPtr<ID3D12Resource> backBuffers[nBuffers];
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    UINT rtvDescSize;
    UINT curBackBufIdx;

    // Scene data
    std::vector<Material> materials;
    int selectedMaterialIdx{ 0 };

    // Spawn system
    std::vector<MeshRef> spawnableMeshRefs;  // template refs for currently loaded meshes
    float spawnAccumulator{ 0.0f };
    float spawnInterval{ 0.1f };  // seconds between spawns
    std::mt19937 rng{ std::random_device{}() };

    // ECS
    flecs::world ecsWorld;

    // Mega vertex/index buffers
    ComPtr<ID3D12Resource> megaVB;
    ComPtr<ID3D12Resource> megaIB;
    D3D12_VERTEX_BUFFER_VIEW megaVBV{};
    D3D12_INDEX_BUFFER_VIEW megaIBV{};
    uint32_t megaVBCapacity{ 0 };
    uint32_t megaVBUsed{ 0 };
    uint32_t megaIBCapacity{ 0 };
    uint32_t megaIBUsed{ 0 };

    // Per-draw structured buffer (triple-buffered upload heaps)
    static constexpr uint32_t maxDrawsPerFrame = 4096;
    ComPtr<ID3D12Resource> drawDataBuffer[nBuffers];
    SceneConstantBuffer* drawDataMapped[nBuffers]{};
    ComPtr<ID3D12DescriptorHeap> sceneSrvHeap;
    UINT sceneSrvDescSize{ 0 };

    ComPtr<ID3D12Resource> depthBuffer;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pipelineState;
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
    float fov = 45.0f;
    mat4 matModel;
    OrbitCamera cam;
    bool contentLoaded = false;

    // Bloom / post-processing
    static constexpr uint32_t bloomMipCount = 5;
    ComPtr<ID3D12Resource> hdrRenderTarget;
    ComPtr<ID3D12Resource> bloomMips[bloomMipCount];
    ComPtr<ID3D12DescriptorHeap> bloomRtvHeap;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    UINT srvDescSize = 0;
    ComPtr<ID3D12RootSignature> bloomRootSignature;
    ComPtr<ID3D12PipelineState> prefilterPSO;
    ComPtr<ID3D12PipelineState> downsamplePSO;
    ComPtr<ID3D12PipelineState> upsamplePSO;
    ComPtr<ID3D12PipelineState> compositePSO;
    float bloomThreshold = 0.7f;
    float bloomIntensity = 1.0f;
    int tonemapMode = 1;

    // ImGui
    ComPtr<ID3D12DescriptorHeap> imguiSrvHeap;
    UINT imguiSrvNextIndex = 0;

    // GUI-controlled scene parameters
    float bgColor[3] = { 0.0f, 0.0f, 0.0f };
    float lightBrightness = 10.0f;
    float ambientBrightness = 0.15f;
    char gltfPathBuf[512] = "";
    // Deferred scene load (set from ImGui, executed at start of next update)
    bool pendingResetToTeapot{ false };
    std::string pendingGltfPath;

    uint64_t frameFenceValues[nBuffers] = {};

    bool vsync = true;
    bool tearingSupported = false;
    bool fullscreen = false;
    bool testMode = false;
    int frameCount = 0;

    gainput::InputMap inputMap;
    gainput::DeviceId keyboardID, mouseID, rawMouseID;

    Application();
    ~Application();

    void transitionResource(
        ComPtr<ID3D12GraphicsCommandList2> cmdList,
        ComPtr<ID3D12Resource> resource,
        D3D12_RESOURCE_STATES beforeState,
        D3D12_RESOURCE_STATES afterState
    );
    void clearRTV(
        ComPtr<ID3D12GraphicsCommandList2> cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        FLOAT clearColor[4]
    );
    void clearDepth(
        ComPtr<ID3D12GraphicsCommandList2> cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv,
        FLOAT depth = 1.0f
    );
    void resizeDepthBuffer(uint32_t width, uint32_t height);
    ComPtr<IDXGISwapChain4> createSwapChain();
    ComPtr<ID3D12DescriptorHeap>
    createDescHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors);
    void updateRenderTargetViews(ComPtr<ID3D12DescriptorHeap> descriptorHeap);
    void update();
    void render();
    void setFullscreen(bool val);
    void flush();
    bool loadContent();
    bool loadGltf(const std::string& path);
    void clearScene();
    void onResize(uint32_t width, uint32_t height);
    void createBloomResources(uint32_t width, uint32_t height);
    void renderBloom(ComPtr<ID3D12GraphicsCommandList2> cmdList);
    void initImGui();
    void shutdownImGui();
    void renderImGui(ComPtr<ID3D12GraphicsCommandList2> cmdList);

    void createMegaBuffers();
    void createDrawDataBuffers();
    MeshRef appendToMegaBuffers(
        ComPtr<ID3D12GraphicsCommandList2> cmdList,
        const std::vector<VertexPBR>& vertices,
        const std::vector<uint32_t>& indices,
        int materialIdx,
        std::vector<ComPtr<ID3D12Resource>>& temps
    );
};
