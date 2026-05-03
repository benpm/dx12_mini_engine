// Outline pixel shader — flat color from outline params

struct BindlessPayload
{
    uint drawDataIdx;
    uint drawIndex;
    float outlineWidth;
    float3 outlineColor;
};
ConstantBuffer<BindlessPayload> payload : register(b0);

#define outlineColor payload.outlineColor


float4 main() : SV_Target
{
    return float4(outlineColor, 1.0f);
}
