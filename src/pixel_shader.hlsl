struct PixelShaderInput
{
    float4 Color    : COLOR;
    float3 Normal   : NORMAL;
    float3 WorldPos : POSITION;
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

float4 main(PixelShaderInput IN) : SV_Target
{
    float3 normal = normalize(IN.Normal);
    float3 lightDir = normalize(cb.LightPos.xyz - IN.WorldPos);
    float3 viewDir = normalize(cb.CameraPos.xyz - IN.WorldPos);
    
    // Ambient
    float3 ambient = cb.AmbientColor.xyz * IN.Color.xyz;
    
    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0f);
    float3 diffuse = diff * cb.LightColor.xyz * IN.Color.xyz;
    
    // Specular
    float3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), 32.0f); // shininess = 32
    float3 specular = spec * cb.LightColor.xyz; // white specular reflection
    
    float3 result = ambient + diffuse + specular;
    return float4(result, IN.Color.a);
}