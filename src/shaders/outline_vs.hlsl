// Outline vertex shader — extrudes vertices along world normals

struct VertexIn
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

#ifdef USE_BINDLESS
struct BindlessPayload
{
    uint drawDataIdx;
    uint drawIndex;
    float outlineWidth;
    float3 outlineColor;
};
ConstantBuffer<BindlessPayload> payload : register(b0);

cbuffer PerPass : register(b2)
{
    matrix ViewProj;
    float4 CameraPos;
};

    #define drawIndex payload.drawIndex
    #define outlineWidth payload.outlineWidth

#else
cbuffer PerPass : register(b1)
{
    matrix ViewProj;
    float4 CameraPos;
};

cbuffer DrawIndex : register(b2)
{
    uint drawIndex;
};

cbuffer OutlineParams : register(b3)
{
    float outlineWidth;
    float outlineR;
    float outlineG;
    float outlineB;
};
#endif

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

#ifdef USE_BINDLESS
StructuredBuffer<PerObjectData> drawDataTables[] : register(t0, space1);
    #define drawData drawDataTables[payload.drawDataIdx]
#else
StructuredBuffer<PerObjectData> drawData : register(t0);
#endif

float4 main(VertexIn IN, uint instanceID : SV_InstanceID) : SV_Position
{
    PerObjectData objData = drawData[drawIndex + instanceID];
    float4 worldPos = mul(objData.Model, float4(IN.Position, 1.0f));
    float3 worldNormal = normalize(mul((float3x3)objData.Model, IN.Normal));
    worldPos.xyz += worldNormal * outlineWidth;
    return mul(ViewProj, worldPos);
}
