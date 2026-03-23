Texture2D lightSprite : register(t0);
SamplerState spriteSampler : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PSInput input) : SV_Target
{
    float4 sprite = lightSprite.Sample(spriteSampler, input.uv);
    return sprite * input.color;
}
