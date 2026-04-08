struct PSInput
{
    float3 Normal : NORMAL;
    float3 WorldPos : POSITION;
    float2 UV : TEXCOORD0;
    uint DrawIndex : BLENDINDICES0;
    float4 Position : SV_Position;
};

uint main(PSInput IN) : SV_TARGET
{
    return IN.DrawIndex;
}
