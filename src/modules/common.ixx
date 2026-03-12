module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <Windows.h>

#include <DirectXMath.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch"
#endif
#include "d3dx12.h"
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

#include <shellapi.h>
#include <wrl.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <exception>
#include <string>
#include <spdlog/spdlog.h>

export module common;

export import math;

export using namespace Microsoft::WRL;
export using namespace DirectX;

export inline void chkDX(HRESULT hr)
{
    if (FAILED(hr)) {
        spdlog::error("chkDX failed with HRESULT: {:#010x}", static_cast<uint32_t>(hr));
        throw std::exception();
    }
}

export constexpr float pi  = 3.14159265358979323846f;
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