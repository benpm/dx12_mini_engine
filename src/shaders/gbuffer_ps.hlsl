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
    float Time;
};

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

struct PixelIn
{
    float3 Normal : NORMAL;
    float3 WorldPos : POSITION;
    float2 UV : TEXCOORD0;
    uint DrawIndex : BLENDINDICES0;
    float4 PrevClipPos : TEXCOORD1;
    float4 Position : SV_Position;
};

struct PixelOut
{
    float4 Normal : SV_Target0;
    float4 Albedo : SV_Target1;
    float2 Material : SV_Target2;
    float2 Motion : SV_Target3;
};

PixelOut main(PixelIn IN)
{
    PerObjectData objData = drawData[IN.DrawIndex];

    PixelOut OUT;
    OUT.Normal = float4(normalize(IN.Normal) * 0.5 + 0.5, 1.0);
    OUT.Albedo = objData.Albedo;
    OUT.Material = float2(objData.Roughness, objData.Metallic);

    // Compute motion vectors
    float2 curPos = IN.Position.xy / IN.Position.w;  // Screen space [-1, 1]
    float2 prevPos = IN.PrevClipPos.xy / IN.PrevClipPos.w;

    // Transform to [0, 1] and then to motion space
    OUT.Motion = (curPos - prevPos) * 0.5;

    return OUT;
}