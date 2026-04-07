// Infinite grid pixel shader
// Renders a Y=0 ground plane grid with unit lines and fade

cbuffer GridCB : register(b0)
{
    matrix ViewProj;
    matrix InvViewProj;
    float4 CameraPos;
};

struct PSInput
{
    float3 NearWorldPos : TEXCOORD0;
    float3 FarWorldPos : TEXCOORD1;
    float4 Position : SV_Position;
};

struct PSOutput
{
    float4 Color : SV_Target;
    float Depth : SV_Depth;
};

float4 grid(float3 worldPos, float camDist)
{
    float2 coord = worldPos.xz;
    float2 dpdx = ddx(coord);
    float2 dpdy = ddy(coord);

    // Unit grid (1m spacing)
    float2 grid1 = abs(frac(coord - 0.5f) - 0.5f);
    float2 lineWidth1 = fwidth(coord);
    float2 draw1 = smoothstep(float2(0, 0), lineWidth1 * 1.5f, grid1);
    float line1 = 1.0f - min(draw1.x, draw1.y);

    // Major grid (10m spacing)
    float2 grid10 = abs(frac(coord * 0.1f - 0.5f) - 0.5f);
    float2 lineWidth10 = fwidth(coord * 0.1f);
    float2 draw10 = smoothstep(float2(0, 0), lineWidth10 * 1.5f, grid10);
    float line10 = 1.0f - min(draw10.x, draw10.y);

    // Axis highlights
    float axisX = smoothstep(lineWidth1.y * 2.0f, 0.0f, abs(coord.y));  // Z axis (red)
    float axisZ = smoothstep(lineWidth1.x * 2.0f, 0.0f, abs(coord.x));  // X axis (blue)

    float3 color = float3(0.35f, 0.35f, 0.35f) * line1;
    color = lerp(color, float3(0.5f, 0.5f, 0.5f), line10);
    color = lerp(color, float3(0.8f, 0.2f, 0.2f), axisX);
    color = lerp(color, float3(0.2f, 0.2f, 0.8f), axisZ);

    float alpha = max(line1 * 0.4f, line10 * 0.6f);
    alpha = max(alpha, max(axisX, axisZ) * 0.8f);

    // Fade with distance
    float fade = 1.0f - saturate(camDist / 80.0f);
    fade *= fade;
    alpha *= fade;

    return float4(color, alpha);
}

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    // Ray from near to far plane
    float3 rayDir = IN.FarWorldPos - IN.NearWorldPos;

    // Intersect Y=0 plane
    float t = -IN.NearWorldPos.y / rayDir.y;

    // Discard if plane is behind camera or ray is parallel
    if (t < 0.0f || abs(rayDir.y) < 1e-6f) {
        discard;
    }

    float3 worldPos = IN.NearWorldPos + rayDir * t;
    float camDist = length(worldPos - CameraPos.xyz);

    OUT.Color = grid(worldPos, camDist);
    if (OUT.Color.a < 0.01f) {
        discard;
    }

    // Compute proper depth
    float4 clipPos = mul(ViewProj, float4(worldPos, 1.0f));
    OUT.Depth = clipPos.z / clipPos.w;

    return OUT;
}
