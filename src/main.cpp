#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <gainput/gainput.h>
#include <objbase.h>
#include <shellapi.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <dbghelp.h>
#include <chrono>
#include <ctime>
#include <cstdio>
#include "scene_data.h"

import application;
import config;
import logging;
import scene_file;
import window;

#ifndef SCENES_DIR
    #define SCENES_DIR "resources/scenes"
#endif

static void writeMiniDump(EXCEPTION_POINTERS* ep)
{
    // Write a minidump next to the executable with timestamped filename so
    // multiple crashes don't overwrite each other. WithDataSegs + WithThreadInfo
    // give a useful stack trace + global state at modest size.
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_s(&tm, &now);
    char path[MAX_PATH];
    std::snprintf(
        path, sizeof(path), "crash_%04d%02d%02d_%02d%02d%02d.dmp", tm.tm_year + 1900, tm.tm_mon + 1,
        tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec
    );

    HANDLE file = CreateFileA(
        path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = ep;
    info.ClientPointers = FALSE;
    MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithDataSegs | MiniDumpWithThreadInfo | MiniDumpWithProcessThreadData |
        MiniDumpWithHandleData
    );
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &info, nullptr, nullptr);
    CloseHandle(file);
    spdlog::error("Wrote minidump to {}", path);
}

static LONG WINAPI sehFilter(EXCEPTION_POINTERS* ep)
{
    spdlog::error(
        "Unhandled SEH exception 0x{:08x} at 0x{:016x}", ep->ExceptionRecord->ExceptionCode,
        reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress)
    );
    writeMiniDump(ep);
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
    bool showHelp = false;
    bool testMode = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
            std::string arg(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, arg.data(), len, nullptr, nullptr);
            if (arg == "-h" || arg == "--help") {
                showHelp = true;
            } else if (arg == "--dump-config") {
                dumpConfig = true;
            } else if (arg == "--test") {
                testMode = true;
            } else if (!arg.empty() && arg[0] != '-') {
                sceneFilePath = arg;
            }
        }
        LocalFree(static_cast<HLOCAL>(argv));
    }

    if (showHelp) {
        const char* helpText =
            "DX12 Mini Engine\n\n"
            "Usage: main.exe [options] [scene-file]\n\n"
            "Options:\n"
            "  -h, --help       Show this help message and exit\n"
            "  --dump-config    Write default config.json and exit\n"
            "  --test           Force run-screenshot-exit mode (WARP, hidden window,\n"
            "                   screenshot at frame 10, skip ImGui). Overrides any\n"
            "                   runtime block in the scene file.\n\n"
            "Arguments:\n"
            "  scene-file       Path to a JSON scene file (default: from "
            "config.json's defaultScenePath)\n";
        ::MessageBoxA(nullptr, helpText, "DX12 Mini Engine - Help", MB_OK | MB_ICONINFORMATION);
        CoUninitialize();
        return 0;
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

    // --test forces run-screenshot-exit behaviour regardless of what's in the scene file.
    if (testMode) {
        if (!hasSceneFile) {
            sceneData = SceneFileData{};
            hasSceneFile = true;
        }
        sceneData.runtime.useWarp = true;
        sceneData.runtime.hideWindow = true;
        // Honour a scene-defined screenshotFrame > 0 (lets showcase scenes
        // request a later snapshot once their startup script has had time to
        // settle). Default to 10 if the scene didn't specify one.
        if (sceneData.runtime.screenshotFrame <= 0) {
            sceneData.runtime.screenshotFrame = 10;
        }
        sceneData.runtime.exitAfterScreenshot = true;
        sceneData.runtime.skipImGui = true;
        spdlog::info(
            "--test: forcing WARP + headless + screenshot at frame {} + exit",
            sceneData.runtime.screenshotFrame
        );
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
