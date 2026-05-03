// Infinite grid vertex shader
// Generates a fullscreen triangle, outputs clip-space position + near/far world positions

// Grid uses the bindless root signature's PerPassCB slot (b2) — the GridCB
// payload (2 mat4 + 2 vec4 = 160 bytes) is too large to fit in the 32 DWORD
// root-constant slot 0. The engine writes the CB into perPass slot 10 each
// frame and binds it through `setConstantBuffer(bindlessPerPassCB, ...)`.
cbuffer GridCB : register(b2)
{
    matrix ViewProj;
    matrix InvViewProj;
    float4 CameraPos;
    float4 GridParams;
};

struct VSOutput
{
    float3 NearWorldPos : TEXCOORD0;
    float3 FarWorldPos : TEXCOORD1;
    float4 Position : SV_Position;
};

VSOutput main(uint vertexID : SV_VertexID)
{
    VSOutput OUT;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    float2 ndc = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    OUT.Position = float4(ndc, 0.0f, 1.0f);

    // Unproject near and far planes to world space
    float4 nearH = mul(InvViewProj, float4(ndc, 0.0f, 1.0f));
    float4 farH = mul(InvViewProj, float4(ndc, 1.0f, 1.0f));
    OUT.NearWorldPos = nearH.xyz / nearH.w;
    OUT.FarWorldPos = farH.xyz / farH.w;
    return OUT;
}
