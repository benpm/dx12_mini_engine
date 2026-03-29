// Normal pre-pass pixel shader: outputs world-space normal encoded to [0,1]
struct PixelIn
{
    float3 Normal : NORMAL;
    float3 WorldPos : POSITION;
    float2 UV : TEXCOORD0;
    uint DrawIndex : BLENDINDICES0;
};

float4 main(PixelIn IN) : SV_Target
{
    return float4(normalize(IN.Normal) * 0.5 + 0.5, 1.0);
}
