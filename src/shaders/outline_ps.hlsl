// Outline pixel shader — flat color from outline params

#ifdef USE_BINDLESS
struct BindlessPayload
{
    uint drawDataIdx;
    uint drawIndex;
    float outlineWidth;
    float3 outlineColor;
};
ConstantBuffer<BindlessPayload> payload : register(b0);

    #define outlineColor payload.outlineColor

#else
cbuffer OutlineParams : register(b3)
{
    float outlineWidth;
    float outlineR;
    float outlineG;
    float outlineB;
};
#endif

float4 main() : SV_Target
{
#ifdef USE_BINDLESS
    return float4(outlineColor, 1.0f);
#else
    return float4(outlineR, outlineG, outlineB, 1.0f);
#endif
}
