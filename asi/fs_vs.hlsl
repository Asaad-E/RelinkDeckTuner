// Full-screen triangle for the post-DoF upscale pass (no vertex buffer, Draw(3, 0)).
// Outputs the same signature the upscale pixel shaders read: SV_Position + TEXCOORD0 (0..1).
void main(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0)
{
    uv = float2((id << 1) & 2, id & 2);
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
