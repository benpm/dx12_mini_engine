#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <gainput/gainput.h>
#include <objbase.h>
#include <shellapi.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include "scene_data.h"

import application;
import input;
import logging;
import scene_file;
import window;

// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
_Use_decl_annotations_ int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    // Disable error popup
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    // Setup logging before doing anything else so we can capture any errors that happen during
    // initialization
    setupLogging();

    // Initialize COM for WIC (required for saving screenshots)
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        spdlog::warn("Failed to initialize COM");
        return -1;
    }

    // Parse CLI arguments: first non-flag argument is the scene file path
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    spdlog::info("Command line: {}", GetCommandLineA());
    std::string sceneFilePath;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            // Convert wide string to narrow
            int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
            std::string arg(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, arg.data(), len, nullptr, nullptr);
            if (!arg.empty() && arg[0] != '-') {
                sceneFilePath = arg;
                break;
            }
        }
        LocalFree(static_cast<HLOCAL>(argv));
    }

    // Load scene file (if provided) before window init to read runtime.useWarp
    SceneFileData sceneData;
    bool hasSceneFile = false;
    if (!sceneFilePath.empty()) {
        hasSceneFile = loadSceneFile(sceneFilePath, sceneData);
        if (!hasSceneFile) {
            spdlog::warn("Failed to load scene file '{}', using defaults", sceneFilePath);
        }
    }

    bool useWarp = hasSceneFile && sceneData.runtime.useWarp;
    bool hideWindow = hasSceneFile && sceneData.runtime.hideWindow;
    int exitCode = 0;

    try {
        spdlog::info("Initializing window...");
        Window::get()->initialize(hInstance, "D3D12 Experiment", 1280, 720, nCmdShow, useWarp);
        spdlog::info("Creating Application...");
        Application app;

        if (hasSceneFile) {
            app.applySceneData(sceneData);
            spdlog::info("Applied scene file: {}", sceneFilePath);
        }

        spdlog::info("Application created.");

        // Input map just to show example for closing window with escape
        app.inputMap.MapBool(Button::Exit, app.keyboardID, gainput::KeyEscape);

        ::ShowWindow(Window::get()->hWnd, hideWindow ? SW_HIDE : SW_SHOW);
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
        spdlog::warn("Fatal exception: {}", e.what());
        exitCode = -1;
    } catch (...) {
        spdlog::warn("Fatal unknown exception");
        exitCode = -1;
    }

    CoUninitialize();
    return exitCode;
}
