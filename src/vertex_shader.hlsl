struct VertexIn
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

cbuffer PerFrame : register(b0)
{
    float4 AmbientColor;
    float4 LightPos[8];
    float4 LightColor[8];
    float4 DirLightDir;
    float4 DirLightColor;
    matrix LightViewProj;
    float ShadowBias;
    float ShadowMapTexelSize;
    float FogStartY;
    float FogDensity;
    float4 FogColor;
};

cbuffer PerPass : register(b1)
{
    matrix ViewProj;
    float4 CameraPos;
};

cbuffer DrawIndex : register(b2)
{
    uint drawIndex;
};

struct PerObjectData
{
    matrix Model;
    float4 Albedo;
    float Roughness;
    float Metallic;
    float EmissiveStrength;
    float Reflective;
    float4 Emissive;
};

StructuredBuffer<PerObjectData> drawData : register(t0);

struct VertexOut
{
    float3 Normal : NORMAL;
    float3 WorldPos : POSITION;
    float2 UV : TEXCOORD0;
    uint DrawIndex : BLENDINDICES0;
    float4 Position : SV_Position;
};

VertexOut main(VertexIn IN, uint instanceID : SV_InstanceID)
{
    PerObjectData objData = drawData[drawIndex + instanceID];
    VertexOut OUT;
    float4 worldPos = mul(objData.Model, float4(IN.Position, 1.0f));
    OUT.WorldPos = worldPos.xyz;
    OUT.Position = mul(ViewProj, worldPos);
    // Normal to world space (assumes uniform scale; use inverse-transpose for non-uniform)
    OUT.Normal = normalize(mul((float3x3)objData.Model, IN.Normal));
    OUT.UV = IN.UV;
    OUT.DrawIndex = drawIndex + instanceID;
    return OUT;
}
