// PBR / Cook-Torrance BRDF pixel shader
// GGX NDF, Smith geometry, Fresnel-Schlick

struct PixelIn
{
    float3 Normal : NORMAL;
    float3 WorldPos : POSITION;
    float2 UV : TEXCOORD0;
    uint DrawIndex : BLENDINDICES0;
};

struct SceneCB
{
    matrix Model;
    matrix ViewProj;
    float4 CameraPos;
    float4 AmbientColor;
    float4 LightPos[8];
    float4 LightColor[8];
    float4 Albedo;
    float Roughness;
    float Metallic;
    float EmissiveStrength;
    float Reflective;
    float4 Emissive;
    // Directional light (shadow-casting)
    float4 DirLightDir;
    float4 DirLightColor;
    matrix LightViewProj;
    float ShadowBias;
    float ShadowMapTexelSize;
    float FogStartY;
    float FogDensity;
    float4 FogColor;
};

StructuredBuffer<SceneCB> drawData : register(t0);
Texture2D<float> shadowMapTex : register(t1);
TextureCube<float3> envMap : register(t2);
SamplerComparisonState shadowSampler : register(s0);
SamplerState envSampler : register(s1);

static const float PI = 3.14159265359f;

// GGX / Trowbridge-Reitz normal distribution
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdH = max(dot(N, H), 0.0f);
    float denom = (NdH * NdH) * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * denom * denom);
}

// Smith's Schlick-GGX geometry term
float GeometrySchlickGGX(float NdV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdV / (NdV * (1.0f - k) + k);
}

float GeometrySmith(float NdV, float NdL, float roughness)
{
    return GeometrySchlickGGX(NdV, roughness) * GeometrySchlickGGX(NdL, roughness);
}

// Fresnel-Schlick approximation
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float calcShadow(float3 worldPos, matrix lightVP, float bias, float texelSize)
{
    float4 lsPos = mul(lightVP, float4(worldPos, 1.0f));
    float3 proj = lsPos.xyz / lsPos.w;
    float2 uv = proj.xy * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y;

    if (any(uv < 0.0f) || any(uv > 1.0f) || proj.z > 1.0f || proj.z < 0.0f) {
        return 1.0f;
    }

    float depth = proj.z - bias;
    float shadow = 0.0f;
    [unroll] for (int x = -1; x <= 1; x++)[unroll] for (int y = -1; y <= 1; y++) shadow +=
        shadowMapTex.SampleCmpLevelZero(shadowSampler, uv + float2(x, y) * texelSize, depth);
    return shadow / 9.0f;
}

float4 main(PixelIn IN) : SV_Target
{
    SceneCB cb = drawData[IN.DrawIndex];

    float3 albedo = cb.Albedo.rgb;
    float roughness = clamp(cb.Roughness, 0.04f, 1.0f);
    float metallic = saturate(cb.Metallic);
    float3 emissive = cb.Emissive.rgb * cb.EmissiveStrength;

    float3 N = normalize(IN.Normal);
    float3 V = normalize(cb.CameraPos.xyz - IN.WorldPos);
    float NdV = max(dot(N, V), 0.0001f);

    // Reflectance at normal incidence (dialectric: 0.04, metal: albedo)
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    // --- Punctual lights (up to 8) ---
    float3 Lo = 0.0f;
    for (int i = 0; i < 8; ++i) {
        float3 toLight = cb.LightPos[i].xyz - IN.WorldPos;
        float dist = length(toLight);
        float3 L = toLight / max(dist, 0.0001f);
        float3 H = normalize(V + L);
        float attenuation = 1.0f / max(dist * dist * 0.01f, 0.0001f);
        float3 radiance = cb.LightColor[i].rgb * attenuation;

        float NdL = max(dot(N, L), 0.0f);

        // Cook-Torrance specular BRDF
        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(NdV, NdL, roughness);
        float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);

        float3 specular = (D * G * F) / (4.0f * NdV * NdL + 0.0001f);

        // Energy-conserving diffuse (Lambertian, metals have no diffuse)
        float3 kD = (1.0f - F) * (1.0f - metallic);
        Lo += (kD * albedo / PI + specular) * radiance * NdL;
    }

    // --- Directional light with shadow ---
    {
        float3 L = normalize(cb.DirLightDir.xyz);
        float3 H = normalize(V + L);
        float NdL = max(dot(N, L), 0.0f);
        float3 radiance = cb.DirLightColor.rgb;

        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(NdV, NdL, roughness);
        float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
        float3 specular = (D * G * F) / (4.0f * NdV * NdL + 0.0001f);
        float3 kD = (1.0f - F) * (1.0f - metallic);

        float shadow =
            calcShadow(IN.WorldPos, cb.LightViewProj, cb.ShadowBias, cb.ShadowMapTexelSize);
        Lo += (kD * albedo / PI + specular) * radiance * NdL * shadow;
    }

    // Simple ambient term (skybox-tinted)
    float3 ambient = cb.AmbientColor.rgb * albedo * (1.0f - metallic * 0.9f);

    float3 color = ambient + Lo + emissive;

    // Cubemap environment reflections
    if (cb.Reflective > 0.5f) {
        float3 R = reflect(-V, N);
        float3 envColor = envMap.SampleLevel(envSampler, R, roughness * 4.0f).rgb;
        float3 F = FresnelSchlick(NdV, F0);
        float envStrength = 1.0f - roughness * roughness;
        color += envColor * F * envStrength;
    }

    // Ocean fog: thick underwater, lighter near surface, darker with depth
    if (cb.FogDensity > 0.0f) {
        float depth = max(cb.FogStartY - IN.WorldPos.y, 0.0f);
        float fogFactor = 1.0f - exp(-depth * cb.FogDensity * 3.0f);
        float dist = length(IN.WorldPos - cb.CameraPos.xyz);
        float distFog = 1.0f - exp(-dist * cb.FogDensity * 0.01f);
        fogFactor = saturate(max(fogFactor, distFog));
        // Darken fog color with depth: surface = fogColor, deep = near-black
        float darkening = exp(-depth * cb.FogDensity * 0.5f);
        float3 deepColor = cb.FogColor.rgb * darkening;
        color = lerp(color, deepColor, fogFactor);
    }

    return float4(color, cb.Albedo.a);
}
