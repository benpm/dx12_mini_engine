module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <d3d12.h>
#include <dxgi1_6.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <windowsx.h>
#include <wrl.h>
#include <algorithm>
#include <cassert>

module window;

import input;

using Microsoft::WRL::ComPtr;

extern IMGUI_IMPL_API LRESULT
ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam)) {
        return true;
    }

    auto* win = Window::get();

    if (win->isReady) {
        switch (message) {
            case WM_PAINT:
                if (win->inMessageLoop && win->onPaintFn) {
                    inputManager.Update();
                    win->onPaintFn(win->callbackCtx);
                }
                ::ValidateRect(hwnd, nullptr);
                break;
            case WM_SYSCHAR:
                break;
            case WM_SIZE: {
                RECT clientRect = {};
                ::GetClientRect(win->hWnd, &clientRect);
                if (win->inMessageLoop && win->onResizeFn) {
                    win->onResizeFn(
                        win->callbackCtx, static_cast<uint32_t>(clientRect.right - clientRect.left),
                        static_cast<uint32_t>(clientRect.bottom - clientRect.top)
                    );
                }
            } break;
            case WM_DESTROY:
                win->doExit = true;
                break;
            default:
                return ::DefWindowProc(hwnd, message, wParam, lParam);
        }
    } else {
        return ::DefWindowProc(hwnd, message, wParam, lParam);
    }

    return 0;
}

static void regWinClass(HINSTANCE hInst, const wchar_t* windowClassName)
{
    WNDCLASSEXW windowClass = {};

    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &WndProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = hInst;
    windowClass.hIcon = ::LoadIcon(hInst, nullptr);
    windowClass.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    windowClass.lpszMenuName = nullptr;
    windowClass.lpszClassName = windowClassName;
    windowClass.hIconSm = ::LoadIcon(hInst, nullptr);

    [[maybe_unused]] static ATOM atom = ::RegisterClassExW(&windowClass);
    if (atom == 0) {
        throwLastWin32Error("RegisterClassExW");
    }
}

static void enableDebugging()
{
#if defined(_DEBUG)
    if (!::IsDebuggerPresent()) {
        spdlog::debug("No debugger attached — skipping D3D12 debug layer");
        return;
    }
    ComPtr<ID3D12Debug> debugInterface;
    chkDX(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
    debugInterface->EnableDebugLayer();
    spdlog::debug("Direct3D Debug Layer Enabled");
#endif
}

static HWND makeWindow(
    const wchar_t* windowClassName,
    HINSTANCE hInst,
    const wchar_t* windowTitle,
    uint32_t width,
    uint32_t height
)
{
    int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

    RECT windowRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    ::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;

    int windowX = std::max<int>(0, (screenWidth - windowWidth) / 2);
    int windowY = std::max<int>(0, (screenHeight - windowHeight) / 2);

    HWND hWnd = ::CreateWindowExW(
        0, windowClassName, windowTitle, WS_OVERLAPPEDWINDOW, windowX, windowY, windowWidth,
        windowHeight, nullptr, nullptr, hInst, nullptr
    );

    if (!hWnd) {
        throwLastWin32Error("CreateWindowExW");
    }

    return hWnd;
}

static ComPtr<IDXGIAdapter4> getAdapter(bool useWarp)
{
    ComPtr<IDXGIFactory4> dxgiFactory;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    if (::IsDebuggerPresent()) {
        createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    chkDX(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));
    ComPtr<IDXGIAdapter1> dxgiAdapter1;
    ComPtr<IDXGIAdapter4> dxgiAdapter4;

    if (useWarp) {
        chkDX(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
        chkDX(dxgiAdapter1.As(&dxgiAdapter4));
    } else {
        SIZE_T maxDedicatedVideoMemory = 0;
        for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND;
             ++i) {
            DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
            dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

            if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                SUCCEEDED(D3D12CreateDevice(
                    dxgiAdapter1.Get(), D3D_FEATURE_LEVEL_12_2, __uuidof(ID3D12Device), nullptr
                )) &&
                dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory) {
                maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
                chkDX(dxgiAdapter1.As(&dxgiAdapter4));
            }
        }
    }

    return dxgiAdapter4;
}

static ComPtr<ID3D12Device2> createDevice(ComPtr<IDXGIAdapter4> adapter)
{
    ComPtr<ID3D12Device2> d3d12Device2;
    // Try 12_2 first (required for mesh shaders etc.); fall back to 12_1 (e.g. WARP)
    if (FAILED(
            D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&d3d12Device2))
        )) {
        chkDX(
            D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&d3d12Device2))
        );
        spdlog::warn("D3D feature level 12_2 not available, using 12_1");
    }

    // Check DXR 1.1 support (non-fatal: WARP and some adapters don't support it)
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (SUCCEEDED(d3d12Device2->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)
        ))) {
        if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1) {
            spdlog::info("DXR Tier 1.1 support verified");
        } else {
            spdlog::warn("DXR Tier 1.1 not supported — raytracing features disabled");
        }
    } else {
        spdlog::warn("CheckFeatureSupport(OPTIONS5) failed — DXR status unknown");
    }

#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue> pInfoQueue;
    if (SUCCEEDED(d3d12Device2.As(&pInfoQueue))) {
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);

        D3D12_MESSAGE_SEVERITY Severities[] = { D3D12_MESSAGE_SEVERITY_INFO };

        D3D12_MESSAGE_ID DenyIds[] = {
            D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
            D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
            D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
        };

        D3D12_INFO_QUEUE_FILTER NewFilter = {};
        NewFilter.DenyList.NumSeverities = _countof(Severities);
        NewFilter.DenyList.pSeverityList = Severities;
        NewFilter.DenyList.NumIDs = _countof(DenyIds);
        NewFilter.DenyList.pIDList = DenyIds;

        chkDX(pInfoQueue->PushStorageFilter(&NewFilter));
    }
#endif

    return d3d12Device2;
}

static bool getTearingSupport()
{
    BOOL allowTearing = FALSE;

    ComPtr<IDXGIFactory4> factory4;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4)))) {
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory4.As(&factory5))) {
            if (FAILED(factory5->CheckFeatureSupport(
                    DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing)
                ))) {
                allowTearing = FALSE;
            }
        }
    }

    return allowTearing == TRUE;
}

void Window::initialize(
    HINSTANCE hInstance,
    const std::string& title,
    uint32_t w,
    uint32_t h,
    int nCmdShow,
    bool useWarp
)
{
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    enableDebugging();

    this->tearingSupported = getTearingSupport();

    const wchar_t* windowClassName = L"DX12WindowClass";
    regWinClass(hInstance, windowClassName);
    const std::wstring wTitle(title.begin(), title.end());
    this->hWnd = makeWindow(windowClassName, hInstance, wTitle.c_str(), w, h);
    GetWindowRect(this->hWnd, &this->windowRect);

    this->device = createDevice(getAdapter(useWarp));
    this->width = w;
    this->height = h;
}
