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
import config;
import logging;
import scene_file;
import window;

#ifndef SCENES_DIR
    #define SCENES_DIR "resources/scenes"
#endif

static LONG WINAPI sehFilter(EXCEPTION_POINTERS* ep)
{
    spdlog::error(
        "Unhandled SEH exception 0x{:08x} at 0x{:016x}", ep->ExceptionRecord->ExceptionCode,
        reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress)
    );
    spdlog::default_logger()->flush();
    return EXCEPTION_EXECUTE_HANDLER;
}

// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
_Use_decl_annotations_ int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    // Disable error popup
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    ::SetUnhandledExceptionFilter(sehFilter);

    // Setup logging before doing anything else so we can capture any errors that happen during
    // initialization
    setupLogging();

    // Initialize COM for WIC (required for saving screenshots)
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        spdlog::warn("Failed to initialize COM");
        return -1;
    }

    // Parse CLI arguments
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    spdlog::info("Command line: {}", GetCommandLineA());
    std::string sceneFilePath;
    bool dumpConfig = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
            std::string arg(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, arg.data(), len, nullptr, nullptr);
            if (arg == "--dump-config") {
                dumpConfig = true;
            } else if (!arg.empty() && arg[0] != '-') {
                sceneFilePath = arg;
            }
        }
        LocalFree(static_cast<HLOCAL>(argv));
    }

    // Handle --dump-config: write defaults and exit
    if (dumpConfig) {
        ConfigData defaults{};
        if (saveConfig("config.json", defaults)) {
            spdlog::info("Dumped default config to config.json");
        }
        CoUninitialize();
        return 0;
    }

    // Load / merge config
    ConfigData config{};
    mergeConfig("config.json", config);

    if (sceneFilePath.empty()) {
        sceneFilePath = config.defaultScenePath;
        spdlog::info("No scene path provided, using config default '{}'", sceneFilePath);
    }

    // Load scene file before window init to read runtime flags
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
        Window::get()->useWarp = useWarp;
        Window::get()->initialize(
            hInstance, "D3D12 Experiment", config.windowWidth, config.windowHeight, nCmdShow
        );
        spdlog::info("Creating Application...");
        Application app;
        app.applyConfig(config);

        if (hasSceneFile) {
            app.applySceneData(sceneData);
            spdlog::info("Applied scene file: {}", sceneFilePath);
        }

        spdlog::info("Application created.");

        // Enable WM_SIZE/WM_PAINT callbacks before ShowWindow so the resize
        // triggered by ShowWindow is handled correctly by onResize().
        Window::get()->inMessageLoop = true;
        if (!hideWindow) {
            ::ShowWindow(Window::get()->hWnd, nCmdShow);
        }

        MSG msg = {};
        spdlog::info("Entering main loop...");
        while (msg.message != WM_QUIT) {
            if (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                inputManager.HandleMessage(msg);
            } else {
                if (Window::get()->doExit) {
                    spdlog::info("Exit requested from Window");
                    ::PostQuitMessage(0);
                    Window::get()->doExit = false;
                    break;
                }

                inputManager.Update();
                app.update();
                app.render();
            }
        }
        spdlog::info("Main loop exited");
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
