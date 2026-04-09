module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <spdlog/spdlog.h>
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

module shader_hotreload;

bool ShaderCompiler::init(const char* dxcPath, const char* shaderDir)
{
    if (!dxcPath || !dxcPath[0] || !shaderDir || !shaderDir[0]) {
        return false;
    }

    if (!std::filesystem::exists(dxcPath)) {
        spdlog::warn("Shader hot reload: DXC not found at {}", dxcPath);
        return false;
    }
    if (!std::filesystem::is_directory(shaderDir)) {
        spdlog::warn("Shader hot reload: shader dir not found at {}", shaderDir);
        return false;
    }

    dxcPath_ = dxcPath;
    shaderDir_ = shaderDir;
    spdlog::info("Shader hot reload enabled (DXC: {})", dxcPath_);
    return true;
}

size_t ShaderCompiler::watch(const char* filename, const char* target)
{
    Watch w;
    w.path = std::filesystem::path(shaderDir_) / filename;
    w.target = target;

    std::error_code ec;
    auto wt = std::filesystem::last_write_time(w.path, ec);
    if (!ec) {
        w.lastWrite = wt;
    }

    watches_.push_back(std::move(w));
    return watches_.size() - 1;
}

bool ShaderCompiler::poll(float dt)
{
    if (dxcPath_.empty()) {
        return false;
    }

    // Check for completed async compilations every frame
    bool anyRecompiled = false;
    for (auto& w : watches_) {
        w.recompiled = false;
        if (w.process != nullptr) {
            if (collectResult(w)) {
                anyRecompiled = true;
            }
        }
    }

    // Check for file changes at reduced frequency
    pollTimer_ += dt;
    if (pollTimer_ >= 0.5f) {
        pollTimer_ = 0.f;
        for (auto& w : watches_) {
            if (w.process != nullptr) {
                continue;  // compilation already in flight
            }
            std::error_code ec;
            auto wt = std::filesystem::last_write_time(w.path, ec);
            if (ec) {
                continue;
            }
            if (wt != w.lastWrite) {
                w.lastWrite = wt;
                launchCompile(w);
            }
        }
    }

    return anyRecompiled;
}

const void* ShaderCompiler::data(size_t idx) const
{
    if (idx >= watches_.size() || watches_[idx].bytecode.empty()) {
        return nullptr;
    }
    return watches_[idx].bytecode.data();
}

size_t ShaderCompiler::size(size_t idx) const
{
    if (idx >= watches_.size()) {
        return 0;
    }
    return watches_[idx].bytecode.size();
}

bool ShaderCompiler::wasRecompiled(size_t idx) const
{
    if (idx >= watches_.size()) {
        return false;
    }
    return watches_[idx].recompiled;
}

bool ShaderCompiler::available() const
{
    return !dxcPath_.empty();
}

void ShaderCompiler::launchCompile(Watch& w)
{
    // Create temp file for output
    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    char tempFile[MAX_PATH];
    GetTempFileNameA(tempDir, "dxc", 0, tempFile);
    w.tempFile = tempFile;

    std::string cmd = "\"" + dxcPath_ + "\" -T " + w.target + " -E main -I \"" + shaderDir_ +
                      "\" -Fo \"" + tempFile + "\" \"" + w.path.string() + "\"";

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hReadPipe, hWritePipe;
    CreatePipe(&hReadPipe, &hWritePipe, &sa, 0);
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdError = hWritePipe;
    si.hStdOutput = hWritePipe;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(
        nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi
    );
    CloseHandle(hWritePipe);

    if (!ok) {
        CloseHandle(hReadPipe);
        DeleteFileA(tempFile);
        w.tempFile.clear();
        spdlog::error("Shader hot reload: failed to launch DXC for {}", w.path.filename().string());
        return;
    }

    CloseHandle(pi.hThread);
    w.process = pi.hProcess;
    w.readPipe = hReadPipe;
    spdlog::info("Compiling shader: {}", w.path.filename().string());
}

bool ShaderCompiler::collectResult(Watch& w)
{
    // Check if process has finished (non-blocking)
    DWORD waitResult = WaitForSingleObject(w.process, 0);
    if (waitResult == WAIT_TIMEOUT) {
        return false;  // still running
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(w.process, &exitCode);
    CloseHandle(w.process);
    w.process = nullptr;

    // Read error/warning output
    std::string errorOutput;
    char buf[4096];
    DWORD bytesRead;
    while (ReadFile(w.readPipe, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        errorOutput += buf;
    }
    CloseHandle(w.readPipe);
    w.readPipe = nullptr;

    if (exitCode != 0) {
        spdlog::error(
            "Shader compilation failed for {}:\n{}", w.path.filename().string(), errorOutput
        );
        DeleteFileA(w.tempFile.c_str());
        w.tempFile.clear();
        return false;
    }

    // Read compiled bytecode
    std::ifstream file(w.tempFile, std::ios::binary | std::ios::ate);
    if (!file) {
        spdlog::error("Shader hot reload: failed to read compiled output");
        DeleteFileA(w.tempFile.c_str());
        w.tempFile.clear();
        return false;
    }

    auto fileSize = file.tellg();
    file.seekg(0);
    w.bytecode.resize(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(w.bytecode.data()), fileSize);
    file.close();

    DeleteFileA(w.tempFile.c_str());
    w.tempFile.clear();
    spdlog::info("Hot-reloaded: {} ({} bytes)", w.path.filename().string(), w.bytecode.size());
    w.recompiled = true;
    return true;
}
