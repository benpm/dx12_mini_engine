// Runtime-only entry point. Same as main.cpp but forces runtime.skipImGui so
// the editor UI is suppressed. ImGui code is still linked (it lives in
// engine_lib for now), but no panels render.
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

_Use_decl_annotations_ int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    ::SetUnhandledExceptionFilter(sehFilter);
    setupLogging();

    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        spdlog::warn("Failed to initialize COM");
        return -1;
    }

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    spdlog::info("game_app command line: {}", GetCommandLineA());
    std::string sceneFilePath;
    bool showHelp = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
            std::string arg(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, arg.data(), len, nullptr, nullptr);
            if (arg == "-h" || arg == "--help") {
                showHelp = true;
            } else if (!arg.empty() && arg[0] != '-') {
                sceneFilePath = arg;
            }
        }
        LocalFree(static_cast<HLOCAL>(argv));
    }

    if (showHelp) {
        ::MessageBoxA(
            nullptr,
            "DX12 Mini Engine — game runtime\n\nUsage: game_app.exe [scene-file]\n\n"
            "Runs without the editor UI. No menus, gizmo, or inspector.",
            "DX12 Mini Engine — game_app", MB_OK | MB_ICONINFORMATION
        );
        CoUninitialize();
        return 0;
    }

    ConfigData config{};
    mergeConfig("config.json", config);

    if (sceneFilePath.empty()) {
        sceneFilePath = config.defaultScenePath;
    }

    SceneFileData sceneData;
    bool hasSceneFile = false;
    if (!sceneFilePath.empty()) {
        hasSceneFile = loadSceneFile(sceneFilePath, sceneData);
    }
    if (!hasSceneFile) {
        sceneData = SceneFileData{};
        hasSceneFile = true;
    }

    // Runtime-only: always suppress editor UI.
    sceneData.runtime.skipImGui = true;

    bool useWarp = sceneData.runtime.useWarp;
    bool hideWindow = sceneData.runtime.hideWindow;
    int exitCode = 0;

    try {
        Window::get()->useWarp = useWarp;
        Window::get()->initialize(
            hInstance, "D3D12 Mini Engine", config.windowWidth, config.windowHeight, nCmdShow
        );
        Application app;
        app.applyConfig(config);
        app.applySceneData(sceneData);

        Window::get()->inMessageLoop = true;
        if (!hideWindow) {
            ::ShowWindow(Window::get()->hWnd, nCmdShow);
        }

        MSG msg = {};
        while (msg.message != WM_QUIT) {
            if (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                inputManager.HandleMessage(msg);
            } else {
                if (Window::get()->doExit) {
                    ::PostQuitMessage(0);
                    Window::get()->doExit = false;
                    break;
                }
                inputManager.Update();
                app.update();
                app.render();
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("Fatal exception: {}", e.what());
        exitCode = -1;
    } catch (...) {
        exitCode = -1;
    }

    CoUninitialize();
    return exitCode;
}
