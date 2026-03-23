module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <Windows.h>
#include <exception>
#include <spdlog/spdlog.h>

export module common;

export import math;

export inline void chkDX(HRESULT hr)
{
    if (FAILED(hr)) {
        spdlog::error("chkDX failed with HRESULT: {:#010x}", static_cast<uint32_t>(hr));
        throw std::exception();
    }
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