// Replacement for the engine's temporal-upsampling resolve pixel shader (DXBC hash 0e4625da...).
// The original reconstructs alternating columns/rows across frames (TUP) and, with an arbitrary
// render scale such as 0.85, leaves horizontal bands. This one does a plain Catmull-Rom bicubic
// resample of the current frame with 16 texel loads (no sampler needed, same bindings and
// input signature as the original). At a 1:1 scale it returns the source texels unchanged.
cbuffer ParamBuffer : register(b1)
{
    float4 orgScreen;   // xy = source size in pixels, zw = 1 / source size
    uint isOddFrame_;
    uint isStopTUP_;
};

Texture2D<float4> g_CurrentTexture : register(t3);

float4 CatmullRomWeights(float t)
{
    float t2 = t * t, t3 = t2 * t;
    return float4(-0.5 * t3 + t2 - 0.5 * t,
                  1.5 * t3 - 2.5 * t2 + 1.0,
                  -1.5 * t3 + 2.0 * t2 + 0.5 * t,
                  0.5 * t3 - 0.5 * t2);
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float2 size = orgScreen.xy;
    float2 p = saturate(uv) * size - 0.5;
    float2 base = floor(p);
    float2 f = p - base;
    float4 wx = CatmullRomWeights(f.x);
    float4 wy = CatmullRomWeights(f.y);
    int2 maxc = int2(size) - 1;
    float3 acc = 0;
    [unroll] for (int j = 0; j < 4; j++)
    {
        int y = clamp(int(base.y) + j - 1, 0, maxc.y);
        float3 row = 0;
        [unroll] for (int i = 0; i < 4; i++)
        {
            int x = clamp(int(base.x) + i - 1, 0, maxc.x);
            row += wx[i] * g_CurrentTexture.Load(int3(x, y, 0)).rgb;
        }
        acc += wy[j] * row;
    }
    return float4(max(acc, 0.0), 1.0);
}
