Texture2D<float4> sceneTexture : register(t0);
Texture2D<float4> bloomTexture : register(t1);
SamplerState linearClamp : register(s0);

cbuffer BloomConstants : register(b0)
{
    float2 texelSize;  // unused in composite but keeps constant buffer layout consistent
    float bloomIntensity;
    float _pad0;
};

float3 ACESFilm(float3 x)
{
    // ACES filmic tone mapping (Narkowicz 2015)
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 main(float2 uv : TEXCOORD) : SV_Target
{
    float3 scene = sceneTexture.Sample(linearClamp, uv).rgb;
    float3 bloom = bloomTexture.Sample(linearClamp, uv).rgb;

    float3 hdr = scene + bloom * bloomIntensity;

    // Tone mapping (ACES filmic)
    float3 ldr = ACESFilm(hdr);

    // Gamma correction (linear → sRGB)
    ldr = pow(ldr, 1.0f / 2.2f);

    return float4(ldr, 1.0f);
}
