module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

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

    pollTimer_ += dt;
    if (pollTimer_ < 0.5f) {
        return false;
    }
    pollTimer_ = 0.f;

    bool anyRecompiled = false;
    for (auto& w : watches_) {
        w.recompiled = false;
        std::error_code ec;
        auto wt = std::filesystem::last_write_time(w.path, ec);
        if (ec) {
            continue;
        }
        if (wt != w.lastWrite) {
            w.lastWrite = wt;
            if (compile(w)) {
                w.recompiled = true;
                anyRecompiled = true;
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

bool ShaderCompiler::compile(Watch& w)
{
    // Create temp file for output
    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    char tempFile[MAX_PATH];
    GetTempFileNameA(tempDir, "dxc", 0, tempFile);

    // Build command line
    std::string cmd = "\"" + dxcPath_ + "\" -T " + w.target + " -E main -Fo \"" + tempFile +
                      "\" \"" + w.path.string() + "\"";

    // Create pipe for error output
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
        spdlog::error("Shader hot reload: failed to launch DXC");
        return false;
    }

    WaitForSingleObject(pi.hProcess, 10000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Read DXC error/warning output
    std::string errorOutput;
    char buf[4096];
    DWORD bytesRead;
    while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        errorOutput += buf;
    }
    CloseHandle(hReadPipe);

    if (exitCode != 0) {
        spdlog::error(
            "Shader compilation failed for {}:\n{}", w.path.filename().string(), errorOutput
        );
        DeleteFileA(tempFile);
        return false;
    }

    // Read compiled bytecode
    std::ifstream file(tempFile, std::ios::binary | std::ios::ate);
    if (!file) {
        spdlog::error("Shader hot reload: failed to read compiled output");
        DeleteFileA(tempFile);
        return false;
    }

    auto fileSize = file.tellg();
    file.seekg(0);
    w.bytecode.resize(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(w.bytecode.data()), fileSize);
    file.close();

    DeleteFileA(tempFile);
    spdlog::info("Hot-reloaded: {} ({} bytes)", w.path.filename().string(), w.bytecode.size());
    return true;
}
