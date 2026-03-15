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
    float4 AmbientColor;
    float4 LightPos[8];
    float4 LightColor[8];
    float4 Albedo;
    float  Roughness;
    float  Metallic;
    float  EmissiveStrength;
    float  _pad;
    float4 Emissive;
    float4 DirLightDir;
    float4 DirLightColor;
    matrix LightViewProj;
    float  ShadowBias;
    float  ShadowMapTexelSize;
    float  _pad2[2];
};

StructuredBuffer<SceneCB> drawData : register(t0);
cbuffer DrawIndex : register(b0) { uint drawIndex; };

struct VertexOut
{
    float3 Normal     : NORMAL;
    float3 WorldPos   : POSITION;
    float2 UV         : TEXCOORD0;
    uint   DrawIndex  : BLENDINDICES0;
    float4 Position   : SV_Position;
};

VertexOut main(VertexIn IN)
{
    SceneCB cb = drawData[drawIndex];
    VertexOut OUT;
    float4 worldPos = mul(cb.Model, float4(IN.Position, 1.0f));
    OUT.WorldPos    = worldPos.xyz;
    OUT.Position    = mul(cb.ViewProj, worldPos);
    // Normal to world space (assumes uniform scale; use inverse-transpose for non-uniform)
    OUT.Normal = normalize(mul((float3x3)cb.Model, IN.Normal));
    OUT.UV     = IN.UV;
    OUT.DrawIndex = drawIndex;
    return OUT;
}
