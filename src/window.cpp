module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <dxgi1_5.h>
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
    int /*nCmdShow*/
)
{
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    this->tearingSupported = getTearingSupport();

    const wchar_t* windowClassName = L"DX12WindowClass";
    regWinClass(hInstance, windowClassName);
    const std::wstring wTitle(title.begin(), title.end());
    this->hWnd = makeWindow(windowClassName, hInstance, wTitle.c_str(), w, h);
    GetWindowRect(this->hWnd, &this->windowRect);

    this->width = w;
    this->height = h;
}
