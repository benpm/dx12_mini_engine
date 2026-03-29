// Outline vertex shader — extrudes vertices along world normals

struct VertexIn
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

// Must match SceneCB layout in vertex_shader.hlsl exactly
struct SceneCB
{
    matrix Model;
    matrix ViewProj;
    float4 CameraPos;
    float4 AmbientColor;
    float4 LightPos[8];
    float4 LightColor[8];
    float4 Albedo;
    float Roughness;
    float Metallic;
    float EmissiveStrength;
    float Reflective;
    float4 Emissive;
    float4 DirLightDir;
    float4 DirLightColor;
    matrix LightViewProj;
    float ShadowBias;
    float ShadowMapTexelSize;
    float FogStartY;
    float FogDensity;
    float4 FogColor;
};

StructuredBuffer<SceneCB> drawData : register(t0);
cbuffer DrawIndex : register(b0)
{
    uint drawIndex;
};
cbuffer OutlineParams : register(b1)
{
    float outlineWidth;
    float outlineR;
    float outlineG;
    float outlineB;
};

float4 main(VertexIn IN) : SV_Position
{
    SceneCB cb = drawData[drawIndex];
    float4 worldPos = mul(cb.Model, float4(IN.Position, 1.0f));
    float3 worldNormal = normalize(mul((float3x3)cb.Model, IN.Normal));
    worldPos.xyz += worldNormal * outlineWidth;
    return mul(cb.ViewProj, worldPos);
}
