#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <Windows.h>
#include <gainput/gainput.h>
#include <shellapi.h>
#include <objbase.h>
#include <spdlog/spdlog.h>

import application;
import input;
import logging;
import window;

// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
_Use_decl_annotations_ int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    setupLogging();

    // Initialize COM for WIC (required for saving screenshots)
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        spdlog::error("Failed to initialize COM");
        return -1;
    }

    // Parse CLI arguments
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    spdlog::info("Command line: {}", GetCommandLineA());
    bool useWarp = false;
    bool testMode = false;
    if (argv) {
        for (int i = 0; i < argc; ++i) {
            if (wcscmp(argv[i], L"--test") == 0) {
                testMode = true;
                useWarp = true;  // Use WARP in test mode for headless environments
                spdlog::info("Running in test mode");
            }
        }
        LocalFree(argv);
    }

    try {
        spdlog::info("Initializing window...");
        Window::get()->initialize(hInstance, "D3D12 Experiment", 1280, 720, nCmdShow, useWarp);
        spdlog::info("Creating Application...");
        Application app;
        app.testMode = testMode;
        spdlog::info("Application created.");

        // Input map just to show example for closing window with escape
        app.inputMap.MapBool(Button::Exit, app.keyboardID, gainput::KeyEscape);

        ::ShowWindow(Window::get()->hWnd, SW_SHOW);
        ::UpdateWindow(Window::get()->hWnd);

        MSG msg = {};
        while (msg.message != WM_QUIT) {
            if (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
                if (msg.message == WM_QUIT) {
                    break;
                }
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                inputManager.HandleMessage(msg);
            } else {
                if (Window::get()->doExit) {
                    spdlog::debug("exit requested");
                    ::PostQuitMessage(0);
                    Window::get()->doExit = false;
                    break;
                }

                inputManager.Update();
                app.update();
                app.render();

                if (app.inputMap.GetBoolWasDown(Button::Exit)) {
                    Window::get()->doExit = true;
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Exception caught: {}", e.what());
    }

    CoUninitialize();
    return 0;
}
