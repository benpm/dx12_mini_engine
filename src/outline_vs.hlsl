// Outline vertex shader — extrudes vertices along world normals

struct VertexIn
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
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

cbuffer OutlineParams : register(b3)
{
    float outlineWidth;
    float outlineR;
    float outlineG;
    float outlineB;
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

float4 main(VertexIn IN, uint instanceID : SV_InstanceID) : SV_Position
{
    PerObjectData objData = drawData[drawIndex + instanceID];
    float4 worldPos = mul(objData.Model, float4(IN.Position, 1.0f));
    float3 worldNormal = normalize(mul((float3x3)objData.Model, IN.Normal));
    worldPos.xyz += worldNormal * outlineWidth;
    return mul(ViewProj, worldPos);
}
