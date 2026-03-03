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

export using namespace Microsoft::WRL;
export using namespace DirectX;

export inline void chkDX(HRESULT hr)
{
    if (FAILED(hr)) {
        spdlog::error("chkDX failed with HRESULT: {:#010x}", static_cast<uint32_t>(hr));
        throw std::exception();
    }
}

export constexpr float pi = XM_PI;
export constexpr float pi2 = XM_PIDIV2;
export constexpr float pi4 = XM_PIDIV4;
export constexpr float tau = XM_2PI;

export inline constexpr float operator""_deg(long double degrees)
{
    return static_cast<float>(degrees) * pi / 180.0f;
}
export inline constexpr float operator""_deg(unsigned long long degrees)
{
    return static_cast<float>(degrees) * pi / 180.0f;
}