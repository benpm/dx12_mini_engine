struct VertexIn
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

struct BindlessIndices
{
    uint drawDataIdx;
    uint shadowMapIdx;
    uint envMapIdx;
    uint ssaoIdx;
    uint shadowSamplerIdx;
    uint envSamplerIdx;
    uint drawIndex;
};
ConstantBuffer<BindlessIndices> indices : register(b0);

cbuffer PerFrame : register(b1)
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

cbuffer PerPass : register(b2)
{
    matrix ViewProj;
    matrix PrevViewProj;
    float4 CameraPos;
};

#define drawIndex indices.drawIndex


struct PerObjectData
{
    matrix Model;
    matrix PrevModel;
    float4 Albedo;
    float Roughness;
    float Metallic;
    float EmissiveStrength;
    float Reflective;
    float4 Emissive;
};

StructuredBuffer<PerObjectData> drawDataTables[] : register(t0, space1);
#define drawData drawDataTables[indices.drawDataIdx]

struct VertexOut
{
    float3 Normal : NORMAL;
    float3 WorldPos : POSITION;
    float2 UV : TEXCOORD0;
    uint DrawIndex : BLENDINDICES0;
    float4 PrevClipPos : TEXCOORD1;
    float4 Position : SV_Position;
};

VertexOut main(VertexIn IN, uint instanceID : SV_InstanceID)
{
    PerObjectData objData = drawData[drawIndex + instanceID];
    VertexOut OUT;
    float4 worldPos = mul(objData.Model, float4(IN.Position, 1.0f));
    OUT.WorldPos = worldPos.xyz;
    OUT.Position = mul(ViewProj, worldPos);

    float4 prevWorldPos = mul(objData.PrevModel, float4(IN.Position, 1.0f));
    OUT.PrevClipPos = mul(PrevViewProj, prevWorldPos);

    // Normal to world space (assumes uniform scale; use inverse-transpose for non-uniform)
    OUT.Normal = normalize(mul((float3x3)objData.Model, IN.Normal));
    OUT.UV = IN.UV;
    OUT.DrawIndex = drawIndex + instanceID;
    return OUT;
}
