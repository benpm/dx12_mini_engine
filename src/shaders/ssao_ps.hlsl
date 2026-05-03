// SSAO compute pass: hemisphere sampling in view space
struct VSOut
{
    float2 UV : TEXCOORD;
    float4 Position : SV_Position;
};

struct BindlessIndices
{
    uint normalIdx;
    uint depthIdx;
    uint noiseIdx;
    uint _pad;
};
ConstantBuffer<BindlessIndices> indices : register(b0);

cbuffer SsaoCB : register(b2)
{
    matrix View;
    matrix Proj;
    matrix InvProj;
    float4 Samples[32];
    float Radius;
    float Bias;
    float ScreenWidth;
    float ScreenHeight;
    int KernelSize;
    float3 _PadCB;
};

Texture2D textures[] : register(t0, space0);
SamplerState samplers[] : register(s0, space0);

#define normalTex textures[indices.normalIdx]
#define depthTex textures[indices.depthIdx]
#define noiseTex textures[indices.noiseIdx]
#define wrapSampler samplers[0]  // SSAO uses static sampler or first bindless sampler

// Reconstruct view-space position from UV and NDC depth
float3 ReconstructViewPos(float2 uv, float depth)
{
    float ndcX = uv.x * 2.0 - 1.0;
    float ndcY = (1.0 - uv.y) * 2.0 - 1.0;
    float4 clipPos = float4(ndcX, ndcY, depth, 1.0);
    float4 viewPos = mul(InvProj, clipPos);
    return viewPos.xyz / viewPos.w;
}

float main(VSOut IN) : SV_Target
{
    uint2 px = (uint2)IN.Position.xy;

    float depth = depthTex.Load(int3(px, 0));
    if (depth >= 0.9999) {
        return 1.0;  // background pixel — no occlusion
    }

    float3 fragViewPos = ReconstructViewPos(IN.UV, depth);

    // World-space normal → view-space
    float3 worldNormal = normalTex.Load(int3(px, 0)).xyz * 2.0 - 1.0;
    float3 viewNormal = normalize(mul((float3x3)View, worldNormal));

    // Random rotation from tiled 4×4 noise texture
    float2 noiseUV = float2(IN.Position.x / 4.0, IN.Position.y / 4.0);
    float2 randXY = noiseTex.Sample(wrapSampler, noiseUV);
    float3 randomVec = normalize(float3(randXY, 0.0));

    // Build TBN to orient hemisphere samples along viewNormal
    float3 tangent = normalize(randomVec - viewNormal * dot(randomVec, viewNormal));
    float3 bitangent = cross(viewNormal, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, viewNormal);

    float occlusion = 0.0;
    [unroll(32)] for (int i = 0; i < KernelSize; ++i)
    {
        float3 sampleVS = mul(Samples[i].xyz, TBN);
        float3 samplePos = fragViewPos + sampleVS * Radius;

        float4 sampleClip = mul(Proj, float4(samplePos, 1.0));
        float2 sampleNDC = sampleClip.xy / sampleClip.w;
        float2 sampleUV = float2(sampleNDC.x * 0.5 + 0.5, 0.5 - sampleNDC.y * 0.5);

        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) {
            continue;
        }

        uint2 samplePx = (uint2)(sampleUV * float2(ScreenWidth, ScreenHeight));
        float sampleDepth = depthTex.Load(int3(samplePx, 0));
        float3 sampleViewPos = ReconstructViewPos(sampleUV, sampleDepth);

        // Weight occlusion by distance (ignore far-away geometry)
        float rangeCheck = smoothstep(0.0, 1.0, Radius / abs(fragViewPos.z - sampleViewPos.z));
        // In LH view space: smaller Z = closer to camera; sampleViewPos.z <= samplePos.z → occluded
        occlusion += (sampleViewPos.z <= samplePos.z - Bias ? 1.0 : 0.0) * rangeCheck;
    }

    return 1.0 - occlusion / float(KernelSize);
}
