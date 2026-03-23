struct VSInput
{
    float2 corner : POSITION;
    float2 uv : TEXCOORD0;
    float3 instancePos : INSTANCE_POS;
    float4 instanceColor : INSTANCE_COLOR;
    float instanceSize : INSTANCE_SIZE;
};

struct PSInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

cbuffer BillboardCB : register(b0)
{
    float4x4 viewProj;
    float4 camRight;
    float4 camUp;
};

PSInput main(VSInput input)
{
    PSInput output;

    float2 offset = input.corner * input.instanceSize;
    float3 worldPos = input.instancePos + camRight.xyz * offset.x + camUp.xyz * offset.y;

    output.position = mul(viewProj, float4(worldPos, 1.0));
    output.uv = input.uv;
    output.color = input.instanceColor;

    return output;
}
