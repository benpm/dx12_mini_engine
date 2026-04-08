// SSAO blur pass: 3×3 box blur using pixel Load()
struct VSOut
{
    float2 UV : TEXCOORD;
    float4 Position : SV_Position;
};

Texture2D<float> ssaoTex : register(t0);

float main(VSOut IN) : SV_Target
{
    int2 px = (int2)IN.Position.xy;
    float result = 0.0;
    [unroll] for (int x = -1; x <= 1; ++x)
        [unroll] for (int y = -1; y <= 1; ++y)
            result += ssaoTex.Load(int3(px + int2(x, y), 0));
    return result / 9.0;
}
