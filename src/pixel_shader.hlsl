// PBR / Cook-Torrance BRDF pixel shader
// GGX NDF, Smith geometry, Fresnel-Schlick

struct PixelIn
{
    float3 Normal   : NORMAL;
    float3 WorldPos : POSITION;
    float2 UV       : TEXCOORD0;
};

struct SceneCB
{
    matrix Model;
    matrix ViewProj;
    float4 CameraPos;
    float4 LightPos;
    float4 LightColor;
    float4 AmbientColor;
    float4 Albedo;
    float  Roughness;
    float  Metallic;
    float  EmissiveStrength;
    float  _pad;
    float4 Emissive;
};

ConstantBuffer<SceneCB> cb : register(b0);

static const float PI = 3.14159265359f;

// GGX / Trowbridge-Reitz normal distribution
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a    = roughness * roughness;
    float a2   = a * a;
    float NdH  = max(dot(N, H), 0.0f);
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

float4 main(PixelIn IN) : SV_Target
{
    float3 albedo    = cb.Albedo.rgb;
    float  roughness = clamp(cb.Roughness, 0.04f, 1.0f);
    float  metallic  = saturate(cb.Metallic);
    float3 emissive  = cb.Emissive.rgb * cb.EmissiveStrength;

    float3 N = normalize(IN.Normal);
    float3 V = normalize(cb.CameraPos.xyz - IN.WorldPos);
    float  NdV = max(dot(N, V), 0.0001f);

    // Reflectance at normal incidence (dialectric: 0.04, metal: albedo)
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    // --- Single punctual light ---
    float3 toLight   = cb.LightPos.xyz - IN.WorldPos;
    float  dist      = length(toLight);
    float3 L         = toLight / dist;
    float3 H         = normalize(V + L);
    float  attenuation = 1.0f / max(dist * dist, 0.0001f);
    float3 radiance  = cb.LightColor.rgb * attenuation;

    float  NdL = max(dot(N, L), 0.0f);

    // Cook-Torrance specular BRDF
    float  D  = DistributionGGX(N, H, roughness);
    float  G  = GeometrySmith(NdV, NdL, roughness);
    float3 F  = FresnelSchlick(max(dot(H, V), 0.0f), F0);

    float3 specular = (D * G * F) / (4.0f * NdV * NdL + 0.0001f);

    // Energy-conserving diffuse (Lambertian, metals have no diffuse)
    float3 kD      = (1.0f - F) * (1.0f - metallic);
    float3 diffuse = kD * albedo / PI;

    float3 Lo = (diffuse + specular) * radiance * NdL;

    // Simple ambient term (skybox-tinted)
    float3 ambient = cb.AmbientColor.rgb * albedo * (1.0f - metallic * 0.9f);

    float3 color = ambient + Lo + emissive;
    return float4(color, cb.Albedo.a);
}
