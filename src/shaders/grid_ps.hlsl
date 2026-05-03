// Infinite grid pixel shader
// Renders a Y=0 ground plane grid with unit lines and fade

// Bindless root sig: PerPassCB slot is b2.
cbuffer GridCB : register(b2)
{
    matrix ViewProj;
    matrix InvViewProj;
    float4 CameraPos;
    float4 GridParams;  // x=major grid size (m), y=subdivisions per major cell
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

    float majorGridSize = max(GridParams.x, 0.001f);
    float subdivisions = max(GridParams.y, 1.0f);
    float minorGridSize = majorGridSize / subdivisions;
    float minorScale = rcp(minorGridSize);
    float majorScale = rcp(majorGridSize);

    // Minor grid (major size / subdivisions spacing)
    float2 grid1 = abs(frac(coord * minorScale - 0.5f) - 0.5f);
    float2 lineWidth1 = fwidth(coord * minorScale);
    float2 draw1 = smoothstep(float2(0, 0), lineWidth1 * 1.5f, grid1);
    float line1 = 1.0f - min(draw1.x, draw1.y);

    // Major grid
    float2 grid10 = abs(frac(coord * majorScale - 0.5f) - 0.5f);
    float2 lineWidth10 = fwidth(coord * majorScale);
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

    // Compute proper depth
    float4 clipPos = mul(ViewProj, float4(worldPos, 1.0f));
    OUT.Depth = clipPos.z / clipPos.w;

    return OUT;
}
