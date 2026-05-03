#ifdef USE_BINDLESS
struct BindlessPayload
{
    uint srcIdx;
    uint samplerIdx;
    uint _pad[2];
    float2 texelSize;
    float intensity;
};
ConstantBuffer<BindlessPayload> payload : register(b0);

Texture2D textures[] : register(t0, space0);
SamplerState samplers[] : register(s0, space0);

    #define srcTexture textures[payload.srcIdx]
    #define linearClamp samplers[payload.samplerIdx]
    #define texelSize payload.texelSize
    #define intensity payload.intensity

#else
Texture2D<float4> srcTexture : register(t0);
SamplerState linearClamp : register(s0);

cbuffer BloomConstants : register(b0)
{
    float2 texelSize;
    float intensity;
    float _pad0;
};
#endif

float4 main(float2 uv : TEXCOORD) : SV_Target
{
    // 9-tap tent filter (3x3 bilinear taps)
    // Kernel weights: [1,2,1; 2,4,2; 1,2,1] / 16
    float3 result = srcTexture.Sample(linearClamp, uv + float2(-1, -1) * texelSize).rgb * 1.0f +
                    srcTexture.Sample(linearClamp, uv + float2(0, -1) * texelSize).rgb * 2.0f +
                    srcTexture.Sample(linearClamp, uv + float2(1, -1) * texelSize).rgb * 1.0f +
                    srcTexture.Sample(linearClamp, uv + float2(-1, 0) * texelSize).rgb * 2.0f +
                    srcTexture.Sample(linearClamp, uv).rgb * 4.0f +
                    srcTexture.Sample(linearClamp, uv + float2(1, 0) * texelSize).rgb * 2.0f +
                    srcTexture.Sample(linearClamp, uv + float2(-1, 1) * texelSize).rgb * 1.0f +
                    srcTexture.Sample(linearClamp, uv + float2(0, 1) * texelSize).rgb * 2.0f +
                    srcTexture.Sample(linearClamp, uv + float2(1, 1) * texelSize).rgb * 1.0f;

    result /= 16.0f;
    // Additive blend state on the PSO handles accumulation with the destination mip
    return float4(result * intensity, 1.0f);
}
