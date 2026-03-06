Texture2D<float4> srcTexture : register(t0);
SamplerState linearClamp : register(s0);

cbuffer BloomConstants : register(b0)
{
    float2 texelSize;
    float _pad0;
    float _pad1;
};

float4 main(float2 uv : TEXCOORD) : SV_Target
{
    // 13-tap downsample (Jimenez 2014 / UE4 style)
    // Samples a 4x4 texel area with specific weights to approximate a tent filter
    float3 a = srcTexture.Sample(linearClamp, uv + float2(-2, -2) * texelSize).rgb;
    float3 b = srcTexture.Sample(linearClamp, uv + float2( 0, -2) * texelSize).rgb;
    float3 c = srcTexture.Sample(linearClamp, uv + float2( 2, -2) * texelSize).rgb;

    float3 d = srcTexture.Sample(linearClamp, uv + float2(-2,  0) * texelSize).rgb;
    float3 e = srcTexture.Sample(linearClamp, uv).rgb;
    float3 f = srcTexture.Sample(linearClamp, uv + float2( 2,  0) * texelSize).rgb;

    float3 g = srcTexture.Sample(linearClamp, uv + float2(-2,  2) * texelSize).rgb;
    float3 h = srcTexture.Sample(linearClamp, uv + float2( 0,  2) * texelSize).rgb;
    float3 i = srcTexture.Sample(linearClamp, uv + float2( 2,  2) * texelSize).rgb;

    float3 j = srcTexture.Sample(linearClamp, uv + float2(-1, -1) * texelSize).rgb;
    float3 k = srcTexture.Sample(linearClamp, uv + float2( 1, -1) * texelSize).rgb;
    float3 l = srcTexture.Sample(linearClamp, uv + float2(-1,  1) * texelSize).rgb;
    float3 m = srcTexture.Sample(linearClamp, uv + float2( 1,  1) * texelSize).rgb;

    // Weighted combination
    // Corner samples (a,c,g,i): 1/32 each = 4/32
    // Edge samples (b,d,f,h): 2/32 each = 8/32
    // Center cross (j,k,l,m): 4/32 each = 16/32
    // Center (e): 4/32
    float3 result = (a + c + g + i) * 0.03125f           // 1/32 each
                  + (b + d + f + h) * 0.0625f             // 2/32 each
                  + (j + k + l + m) * 0.125f              // 4/32 each
                  + e * 0.125f;                            // 4/32

    return float4(result, 1.0f);
}
