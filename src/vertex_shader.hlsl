struct VertexIn
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 UV       : TEXCOORD0;
};

struct SceneCB
{
    matrix Model;
    matrix ViewProj;
    float4 CameraPos;
    float4 LightPos;
    float4 LightColor;
    float4 AmbientColor;
    float4 Albedo;
    float  Roughness;
    float  Metallic;
    float  EmissiveStrength;
    float  _pad;
    float4 Emissive;
};

ConstantBuffer<SceneCB> cb : register(b0);

struct VertexOut
{
    float3 Normal   : NORMAL;
    float3 WorldPos : POSITION;
    float2 UV       : TEXCOORD0;
    float4 Position : SV_Position;
};

VertexOut main(VertexIn IN)
{
    VertexOut OUT;
    float4 worldPos = mul(cb.Model, float4(IN.Position, 1.0f));
    OUT.WorldPos    = worldPos.xyz;
    OUT.Position    = mul(cb.ViewProj, worldPos);
    // Normal to world space (assumes uniform scale; use inverse-transpose for non-uniform)
    OUT.Normal = normalize(mul((float3x3)cb.Model, IN.Normal));
    OUT.UV     = IN.UV;
    return OUT;
}
