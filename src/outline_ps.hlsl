// Outline pixel shader — flat color from outline params

cbuffer OutlineParams : register(b1)
{
    float outlineWidth;
    float outlineR;
    float outlineG;
    float outlineB;
};

float4 main() : SV_Target
{
    return float4(outlineR, outlineG, outlineB, 1.0f);
}
