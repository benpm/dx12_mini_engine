module;

#include <d3d12.h>
#include <Windows.h>
#include <wrl.h>
#include <cstdint>
#include <string>

export module window;

import common;

using Microsoft::WRL::ComPtr;

export class Window
{
   public:
    ComPtr<ID3D12Device2> device;
    HWND hWnd = nullptr;
    RECT windowRect{};
    bool tearingSupported = false;
    uint32_t width = 0, height = 0;
    bool doExit = false;

    // Callbacks registered by Application (breaks circular dependency)
    void (*onPaintFn)(void*) = nullptr;
    void (*onResizeFn)(void*, uint32_t, uint32_t) = nullptr;
    void* callbackCtx = nullptr;
    bool isReady = false;
    bool inMessageLoop = false;

    void initialize(
        HINSTANCE hInstance,
        const std::string& title,
        uint32_t width,
        uint32_t height,
        int nCmdShow,
        bool useWarp = false
    );

    static Window* get()
    {
        static Window window{};
        return &window;
    }

   private:
    explicit Window() = default;
};
