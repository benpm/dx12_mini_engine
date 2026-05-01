module;

#include <Windows.h>
#include <cstdint>
#include <string>

export module window;

import common;

export class Window
{
   public:
    HWND hWnd = nullptr;
    RECT windowRect{};
    bool tearingSupported = false;
    bool useWarp = false;  // GPU adapter preference; consumed by Application's gfx device init.
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
        int nCmdShow
    );

    static Window* get()
    {
        static Window window{};
        return &window;
    }

   private:
    explicit Window() = default;
};
