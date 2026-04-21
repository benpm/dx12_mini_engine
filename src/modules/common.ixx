module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <Windows.h>
#include <cctype>
#include <cstdint>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

export module common;

export import math;

namespace detail
{
    inline std::string trimTrailingWhitespace(std::string msg)
    {
        while (!msg.empty() && std::isspace(static_cast<unsigned char>(msg.back())) != 0) {
            msg.pop_back();
        }
        return msg;
    }

    inline std::string getSystemMessage(uint32_t code)
    {
        LPSTR messageBuffer = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                            FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD length = ::FormatMessageA(
            flags, nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPSTR>(&messageBuffer), 0, nullptr
        );

        std::string message;
        if (length != 0 && messageBuffer != nullptr) {
            message.assign(messageBuffer, length);
            ::LocalFree(messageBuffer);
            message = trimTrailingWhitespace(message);
        }
        return message;
    }
}

export inline std::string formatHRESULT(HRESULT hr)
{
    const auto code = static_cast<uint32_t>(hr);
    const std::string message = detail::getSystemMessage(code);
    if (message.empty()) {
        return fmt::format("{:#010x} (no system message)", code);
    }
    return fmt::format("{:#010x} ({})", code, message);
}

export inline std::string formatWin32Error(DWORD errorCode)
{
    const std::string message = detail::getSystemMessage(errorCode);
    if (message.empty()) {
        return fmt::format("{} (0x{:08x}) (no system message)", errorCode, errorCode);
    }
    return fmt::format("{} (0x{:08x}) ({})", errorCode, errorCode, message);
}

export inline void chkDX(
    HRESULT hr,
    std::string_view context = "DirectX call",
    std::source_location location = std::source_location::current()
)
{
    if (FAILED(hr)) {
        auto msg = fmt::format(
            "{} failed with HRESULT {} at {}:{} in {}", context, formatHRESULT(hr),
            location.file_name(), location.line(), location.function_name()
        );
        spdlog::error("{}", msg);
        throw std::runtime_error(msg);
    }
}

export [[noreturn]] inline void throwLastWin32Error(
    std::string_view context,
    std::source_location location = std::source_location::current()
)
{
    const DWORD error = ::GetLastError();
    throw std::runtime_error(
        fmt::format(
            "{} failed with Win32 error {} at {}:{} in {}", context, formatWin32Error(error),
            location.file_name(), location.line(), location.function_name()
        )
    );
}

export constexpr float pi = 3.14159265358979323846f;
export constexpr float pi2 = 1.57079632679489661923f;
export constexpr float pi4 = 0.78539816339744830962f;
export constexpr float tau = 6.28318530717958647692f;

export inline constexpr float operator""_deg(long double degrees)
{
    return static_cast<float>(degrees) * pi / 180.0f;
}
export inline constexpr float operator""_deg(unsigned long long degrees)
{
    return static_cast<float>(degrees) * pi / 180.0f;
}