struct VertexPosNormalColor
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float3 Color    : COLOR;
};

struct SceneConstantBuffer
{
    matrix Model;
    matrix ViewProj;
    float4 CameraPos;
    float4 LightPos;
    float4 LightColor;
    float4 AmbientColor;
};

ConstantBuffer<SceneConstantBuffer> cb : register(b0);

struct VertexShaderOutput
{
    float4 Color    : COLOR;
    float3 Normal   : NORMAL;
    float3 WorldPos : POSITION;
    float4 Position : SV_Position;
};

VertexShaderOutput main(VertexPosNormalColor IN)
{
    VertexShaderOutput OUT;
    float4 worldPos = mul(cb.Model, float4(IN.Position, 1.0f));
    OUT.WorldPos = worldPos.xyz;
    OUT.Position = mul(cb.ViewProj, worldPos);
    // Transform normal to world space (assuming uniform scaling, otherwise use inverse transpose)
    OUT.Normal = normalize(mul((float3x3)cb.Model, IN.Normal));
    OUT.Color = float4(IN.Color, 1.0f);
    return OUT;
}