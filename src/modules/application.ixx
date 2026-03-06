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

export module application;

export import camera;
export import command_queue;
export import input;

export struct VertexPosNormalColor
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT3 color;
};

export struct SceneConstantBuffer
{
    XMMATRIX model;
    XMMATRIX viewProj;
    XMFLOAT4 cameraPos;
    XMFLOAT4 lightPos;
    XMFLOAT4 lightColor;
    XMFLOAT4 ambientColor;
};

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
    XMFLOAT2 mousePos;
    XMFLOAT2 mouseDelta;
    ComPtr<ID3D12Device2> device;
    CommandQueue cmdQueue;
    ComPtr<IDXGISwapChain4> swapChain;
    ComPtr<ID3D12Resource> backBuffers[nBuffers];
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    UINT rtvDescSize;
    UINT curBackBufIdx;

    ComPtr<ID3D12Resource> vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_INDEX_BUFFER_VIEW indexBufferView;
    ComPtr<ID3D12Resource> depthBuffer;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pipelineState;
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
    float fov = 45.0f;
    XMMATRIX matModel;
    OrbitCamera cam;
    bool contentLoaded = false;
    uint32_t numIndices = 0;

    // Bloom / post-processing
    static constexpr uint32_t bloomMipCount = 5;
    ComPtr<ID3D12Resource> hdrRenderTarget;
    ComPtr<ID3D12Resource> bloomMips[bloomMipCount];
    ComPtr<ID3D12DescriptorHeap> bloomRtvHeap;  // 1 HDR RT + bloomMipCount RTVs
    ComPtr<ID3D12DescriptorHeap> srvHeap;       // shader-visible: 1 HDR + bloomMipCount SRVs
    UINT srvDescSize = 0;
    ComPtr<ID3D12RootSignature> bloomRootSignature;
    ComPtr<ID3D12PipelineState> prefilterPSO;
    ComPtr<ID3D12PipelineState> downsamplePSO;
    ComPtr<ID3D12PipelineState> upsamplePSO;
    ComPtr<ID3D12PipelineState> compositePSO;
    float bloomThreshold = 0.7f;
    float bloomIntensity = 1.0f;

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
    void updateBufferResource(
        ComPtr<ID3D12GraphicsCommandList2> cmdList,
        ID3D12Resource** pDestinationResource,
        ID3D12Resource** pIntermediateResource,
        size_t numElements,
        size_t elementSize,
        const void* bufferData = nullptr,
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE
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
    void onResize(uint32_t width, uint32_t height);
    void createBloomResources(uint32_t width, uint32_t height);
    void renderBloom(ComPtr<ID3D12GraphicsCommandList2> cmdList);
};
