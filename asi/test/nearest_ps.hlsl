// Test reference: the engine's non-temporal path (isStopTUP_ = 1), a nearest-neighbour texel copy.
cbuffer ParamBuffer : register(b1) { float4 orgScreen; };
Texture2D<float4> g_CurrentTexture : register(t3);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    return float4(g_CurrentTexture.Load(int3(int2(uv * orgScreen.xy), 0)).rgb, 1.0);
}
