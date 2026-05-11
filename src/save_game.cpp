module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <Windows.h>
#include <shlobj.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <filesystem>
#include <string>

module save_game;

#include "audio_capi.h"

extern "C" int engine_save_slot_path(const char* slotName, char* outBuf, int outBufSize)
{
    if (!slotName || !outBuf || outBufSize <= 0) {
        return 0;
    }
    auto path = SaveGame::slotPath(slotName);
    if (path.empty() || static_cast<int>(path.size()) >= outBufSize) {
        outBuf[0] = '\0';
        return 0;
    }
    std::memcpy(outBuf, path.data(), path.size());
    outBuf[path.size()] = '\0';
    return 1;
}

namespace
{
    std::string knownFolder(REFKNOWNFOLDERID id)
    {
        PWSTR path = nullptr;
        if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &path))) {
            return {};
        }
        std::wstring w(path);
        CoTaskMemFree(path);
        std::string out(w.size(), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(),
            static_cast<int>(out.size()), nullptr, nullptr
        );
        return out;
    }
}  // namespace

namespace SaveGame
{
    std::string savesDir()
    {
        std::string base = knownFolder(FOLDERID_LocalAppData);
        if (base.empty()) {
            spdlog::warn("SaveGame: SHGetKnownFolderPath(LocalAppData) failed");
            return {};
        }
        std::filesystem::path dir = std::filesystem::path(base) / "dx12_mini_engine" / "saves";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            spdlog::warn("SaveGame: failed to create '{}': {}", dir.string(), ec.message());
            return {};
        }
        return dir.string();
    }

    std::string slotPath(const std::string& slotName)
    {
        auto dir = savesDir();
        if (dir.empty()) {
            return {};
        }
        std::filesystem::path p = std::filesystem::path(dir) / (slotName + ".json");
        return p.string();
    }
}  // namespace SaveGame
