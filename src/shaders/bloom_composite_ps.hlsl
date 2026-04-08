Texture2D<float4> sceneTexture : register(t0);
Texture2D<float4> bloomTexture : register(t1);
SamplerState linearClamp : register(s0);

cbuffer BloomConstants : register(b0)
{
    float2 texelSize;
    float bloomIntensity;
    uint tonemapMode;
    float3 camForward;
    float3 camRight;
    float3 camUp;
    float3 sunDir;
    float aspectRatio;
    float tanHalfFov;
    float time;
};

// --- ACES Filmic (Narkowicz 2015) ---
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// --- AgX (Troy Sobotka) ---
static const float3x3 AgXInsetMatrix = float3x3(
    0.856627153315983f,
    0.137318972929847f,
    0.11189821299995f,
    0.0951212405381588f,
    0.761241990602591f,
    0.0767994186031903f,
    0.0482516061458583f,
    0.101439036467562f,
    0.811302368396859f
);
static const float3x3 AgXOutsetMatrix = float3x3(
    1.1271005818144368f,
    -0.1413297634984383f,
    -0.14132976349843826f,
    -0.11060664309660323f,
    1.157823702216272f,
    -0.11060664309660294f,
    -0.016493938717834573f,
    -0.016493938717834257f,
    1.2519364065950405f
);

float3 AgXCore(float3 color)
{
    color = mul(AgXInsetMatrix, color);
    color = max(color, 1e-10f);
    color = clamp(log2(color), -12.47393f, 4.026069f);
    color = (color + 12.47393f) / (4.026069f + 12.47393f);
    color = clamp(color, 0.0f, 1.0f);

    // 6th order polynomial approximation of AgX sigmoid.
    // Coefficients from Godot PR #101406 — monotonic on [0,1], f(0)=0, f(1)≈1.
    float3 x2 = color * color;
    float3 x4 = x2 * x2;
    color = 27.069f * x4 * x2 - 74.778f * x4 * color + 70.359f * x4 - 25.682f * x2 * color +
            4.0111f * x2 + 0.021f * color;
    return color;
}

float3 AgXTonemap(float3 color)
{
    color = AgXCore(color);
    color = mul(AgXOutsetMatrix, color);
    return saturate(color);
}

float3 AgXPunchy(float3 color)
{
    color = AgXCore(color);
    // CDL: contrast boost + saturation
    color = pow(color, 1.35f);
    float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    color = luma + 1.4f * (color - luma);

    color = mul(AgXOutsetMatrix, color);
    return saturate(color);
}

// --- Gran Turismo / Uchimura ---
float UchimuraCurve(float x, float P, float a, float m, float l, float c, float b)
{
    float l0 = ((P - m) * l) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    float w0 = 1.0f - smoothstep(0.0f, m, x);
    float w2 = step(m + l0, x);
    float w1 = 1.0f - w0 - w2;

    float T = m * pow(x / m, c) + b;
    float S = P - (P - S1) * exp(CP * (x - S0));
    float L = m + a * (x - m);

    return T * w0 + L * w1 + S * w2;
}

float3 UchimuraTonemap(float3 x)
{
    const float P = 1.0f;   // max brightness
    const float a = 1.0f;   // contrast
    const float m = 0.22f;  // linear section start
    const float l = 0.4f;   // linear section length
    const float c = 1.33f;  // toe curvature
    const float b = 0.0f;   // pedestal
    return float3(
        UchimuraCurve(x.r, P, a, m, l, c, b), UchimuraCurve(x.g, P, a, m, l, c, b),
        UchimuraCurve(x.b, P, a, m, l, c, b)
    );
}

// --- Khronos PBR Neutral ---
float3 PBRNeutralTonemap(float3 color)
{
    const float startCompression = 0.8f - 0.04f;
    const float desaturation = 0.15f;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) {
        return color;
    }

    float d = 1.0f - startCompression;
    float newPeak = 1.0f - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
    return lerp(color, newPeak * float3(1, 1, 1), g);
}

#include "sky.hlsli"

float4 main(float2 uv : TEXCOORD) : SV_Target
{
    float3 scene = sceneTexture.Sample(linearClamp, uv).rgb;
    float3 bloom = bloomTexture.Sample(linearClamp, uv).rgb;

    // If scene is near-black, render Rayleigh sky
    float sceneLum = dot(scene, float3(0.299f, 0.587f, 0.114f));
    if (sceneLum < 0.001f) {
        // Reconstruct view direction from UV + camera orientation
        float2 ndc = (uv - 0.5f) * 2.0f;
        ndc.y = -ndc.y;
        float3 viewDir = normalize(
            camForward + camRight * ndc.x * aspectRatio * tanHalfFov + camUp * ndc.y * tanHalfFov
        );
        scene = rayleighSky(viewDir, sunDir, time);
    }

    float3 hdr = scene + bloom * bloomIntensity;

    float3 ldr;
    switch (tonemapMode) {
        case 0:
            ldr = ACESFilm(hdr);
            break;
        case 1:
            ldr = AgXTonemap(hdr);
            break;
        case 2:
            ldr = AgXPunchy(hdr);
            break;
        case 3:
            ldr = UchimuraTonemap(hdr);
            break;
        case 4:
            ldr = PBRNeutralTonemap(hdr);
            break;
        default:
            ldr = AgXTonemap(hdr);
            break;
    }

    // sRGB OETF: linear segment below 0.0031308, power curve above
    ldr = max(ldr, 0.0f);
    ldr = select(ldr <= 0.0031308f, ldr * 12.92f, 1.055f * pow(ldr, 1.0f / 2.4f) - 0.055f);
    return float4(ldr, 1.0f);
}




