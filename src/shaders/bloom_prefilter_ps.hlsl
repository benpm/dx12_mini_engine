struct BindlessPayload
{
    uint srcIdx;
    uint samplerIdx;
    uint _pad[2];
    float2 texelSize;
    float threshold;
    float softKnee;
};
ConstantBuffer<BindlessPayload> payload : register(b0);

Texture2D textures[] : register(t0, space0);
SamplerState samplers[] : register(s0, space0);

#define srcTexture textures[payload.srcIdx]
#define linearClamp samplers[payload.samplerIdx]
#define texelSize payload.texelSize
#define threshold payload.threshold
#define softKnee payload.softKnee


float luma(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

// Soft threshold — smooth transition around the threshold value
float3 applyThreshold(float3 color)
{
    float br = luma(color);
    float knee = threshold * softKnee;
    float soft = br - threshold + knee;
    soft = clamp(soft, 0.0f, 2.0f * knee);
    soft = soft * soft / (4.0f * knee + 1e-5f);
    float contribution = max(soft, br - threshold) / max(br, 1e-5f);
    return color * max(contribution, 0.0f);
}

// Karis average: weight each sample by 1/(1+luma) to prevent firefly artifacts
float3 karisAverage(float3 a, float3 b, float3 c, float3 d)
{
    float wa = 1.0f / (1.0f + luma(a));
    float wb = 1.0f / (1.0f + luma(b));
    float wc = 1.0f / (1.0f + luma(c));
    float wd = 1.0f / (1.0f + luma(d));
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

float4 main(float2 uv : TEXCOORD) : SV_Target
{
    // 13-tap downsample (Jimenez 2014) with Karis average for anti-firefly
    // Sample pattern covers a 4x4 texel area using bilinear filtering
    float3 a = srcTexture.Sample(linearClamp, uv + float2(-1, -1) * texelSize).rgb;
    float3 b = srcTexture.Sample(linearClamp, uv + float2(0, -1) * texelSize).rgb;
    float3 c = srcTexture.Sample(linearClamp, uv + float2(1, -1) * texelSize).rgb;
    float3 d = srcTexture.Sample(linearClamp, uv + float2(-1, 0) * texelSize).rgb;
    float3 e = srcTexture.Sample(linearClamp, uv).rgb;
    float3 f = srcTexture.Sample(linearClamp, uv + float2(1, 0) * texelSize).rgb;
    float3 g = srcTexture.Sample(linearClamp, uv + float2(-1, 1) * texelSize).rgb;
    float3 h = srcTexture.Sample(linearClamp, uv + float2(0, 1) * texelSize).rgb;
    float3 i = srcTexture.Sample(linearClamp, uv + float2(1, 1) * texelSize).rgb;

    // Apply threshold before averaging
    a = applyThreshold(a);
    b = applyThreshold(b);
    c = applyThreshold(c);
    d = applyThreshold(d);
    e = applyThreshold(e);
    f = applyThreshold(f);
    g = applyThreshold(g);
    h = applyThreshold(h);
    i = applyThreshold(i);

    // 4 groups of 4 samples each, using Karis average
    float3 g0 = karisAverage(a, b, d, e);
    float3 g1 = karisAverage(b, c, e, f);
    float3 g2 = karisAverage(d, e, g, h);
    float3 g3 = karisAverage(e, f, h, i);

    // Weighted combination: center group gets more weight
    float3 result = (g0 + g1 + g2 + g3) * 0.25f;
    return float4(result, 1.0f);
}
