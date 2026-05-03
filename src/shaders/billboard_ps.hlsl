struct BindlessPayload
{
    uint spriteIdx;
    uint samplerIdx;
    uint _pad[2];
};
ConstantBuffer<BindlessPayload> payload : register(b0);

Texture2D textures[] : register(t0, space0);
SamplerState samplers[] : register(s0, space0);

#define lightSprite textures[payload.spriteIdx]
#define spriteSampler samplers[payload.samplerIdx]

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
