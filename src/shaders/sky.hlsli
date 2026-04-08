// Shared sky + cloud functions
// Included by bloom_composite_ps.hlsl and pixel_shader.hlsl

#ifndef SKY_HLSLI
#define SKY_HLSLI

// --- Hash-based noise (no textures) ---

float hash2D(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float valueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    float a = hash2D(i);
    float b = hash2D(i + float2(1, 0));
    float c = hash2D(i + float2(0, 1));
    float d = hash2D(i + float2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float fbm(float2 p, int octaves)
{
    float v = 0.0f;
    float amp = 0.5f;
    float2 shift = float2(100.0f, 100.0f);
    for (int i = 0; i < octaves; ++i) {
        v += amp * valueNoise(p);
        p = p * 2.0f + shift;
        amp *= 0.5f;
    }
    return v;
}

// --- Rayleigh sky with procedural clouds ---

float3 rayleighSky(float3 viewDir, float3 sunDirection, float time)
{
    static const float3 betaR = float3(5.8e-3f, 13.5e-3f, 33.1e-3f);
    float sunDot = max(dot(viewDir, sunDirection), 0.0f);
    float upDot = max(viewDir.y, 0.0f);
    float phase = 0.75f * (1.0f + sunDot * sunDot);
    float opticalDepth = 1.0f / (upDot + 0.15f);
    float3 scatter = betaR * phase * opticalDepth;
    float3 sky = scatter * 15.0f;

    // Sun disc
    sky += float3(1.0f, 0.9f, 0.7f) * pow(sunDot, 256.0f) * 8.0f;

    // Warm horizon band
    float horizonFade = exp(-upDot * 3.0f);
    sky += float3(0.7f, 0.55f, 0.4f) * horizonFade * 0.4f;

    // Procedural clouds — spherical projection (works at any view angle)
    {
        float2 cloudUV = normalize(viewDir).xz * 1.2f;
        cloudUV += float2(time * 0.008f, time * 0.004f);

        float n = fbm(cloudUV * 2.5f, 5);
        // Higher threshold = more scattered clouds
        float cloudDensity = smoothstep(0.45f, 0.7f, n);

        // Fade clouds far below horizon
        float cloudFade = smoothstep(-0.5f, -0.1f, viewDir.y);
        cloudDensity *= cloudFade;

        // Cloud lighting: brighter on sun side
        float cloudLight = 0.7f + 0.3f * sunDot;
        float3 cloudColor = float3(1.0f, 0.98f, 0.95f) * cloudLight;

        // Darken cloud base slightly
        float3 cloudShadow = float3(0.65f, 0.68f, 0.78f) * (1.0f - n * 0.2f);
        cloudColor = lerp(cloudShadow, cloudColor, smoothstep(0.5f, 0.75f, n));

        sky = lerp(sky, cloudColor, cloudDensity * 0.8f);
    }

    return max(sky, 0.0f);
}

// Overload without time (static clouds, for backwards compat)
float3 rayleighSky(float3 viewDir, float3 sunDirection)
{
    return rayleighSky(viewDir, sunDirection, 0.0f);
}

#endif // SKY_HLSLI
