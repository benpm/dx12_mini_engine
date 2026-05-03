// SSAO blur pass: 3×3 box blur using pixel Load()
struct VSOut
{
    float2 UV : TEXCOORD;
    float4 Position : SV_Position;
};

#ifdef USE_BINDLESS
struct BindlessIndices
{
    uint ssaoIdx;
    uint _pad[3];
};
ConstantBuffer<BindlessIndices> indices : register(b0);
Texture2D textures[] : register(t0, space0);
    #define ssaoTex textures[indices.ssaoIdx]
#else
Texture2D<float> ssaoTex : register(t0);
#endif

float main(VSOut IN) : SV_Target
{
    int2 px = (int2)IN.Position.xy;
    float result = 0.0;
    [unroll] for (int x = -1; x <= 1; ++x)[unroll] for (int y = -1; y <= 1; ++y) result +=
        ssaoTex.Load(int3(px + int2(x, y), 0));
    return result / 9.0;
}
