struct VSOutput
{
    float2 UV       : TEXCOORD;
    float4 Position : SV_Position;
};

VSOutput main(uint vertexID : SV_VertexID)
{
    VSOutput OUT;
    // Generate fullscreen triangle: 3 vertices cover the entire screen
    // vertexID 0: (-1,-1), 1: (-1, 3), 2: (3,-1)
    OUT.UV = float2((vertexID << 1) & 2, vertexID & 2);
    OUT.Position = float4(OUT.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return OUT;
}
