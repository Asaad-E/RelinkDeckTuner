// Replacement for the engine's temporal-upsampling resolve pixel shader (DXBC hash 0e4625da...):
// Catmull-Rom bicubic upscale followed by AMD FidelityFX RCAS sharpening (FSR 1.0's second stage,
// MIT licence, third_party/fsr1), in one pass. RCAS needs the upscaled colour of the pixel and of
// its four neighbours; all five come from one 6x6 block of source texels (36 loads), because at the
// small scale factors used here (>= 0.7) a neighbour is less than 1.5 source texels away.
// Unlike EASU it does not reconstruct edges, so near-1:1 factors (0.9 -> 1152 to 1280) do not
// turn thin outlines into steps. Same bindings and input signature as the original.
cbuffer ParamBuffer : register(b1)
{
    float4 orgScreen;   // xy = source size in pixels, zw = 1 / source size
    uint isOddFrame_;
    uint isStopTUP_;
};

Texture2D<float4> g_CurrentTexture : register(t3);

#ifndef RCAS_SHARPNESS
#define RCAS_SHARPNESS 0.2          // stops: 0 = strongest, each +1 halves it (AMD's usual 0.2)
#endif
#define RCAS_LIMIT (0.25 - (1.0 / 16.0))

float CatmullRom(float d)
{
    d = abs(d);
    float d2 = d * d, d3 = d2 * d;
    return d < 1.0 ? 1.5 * d3 - 2.5 * d2 + 1.0 : (d < 2.0 ? -0.5 * d3 + 2.5 * d2 - 4.0 * d + 2.0 : 0.0);
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float2 size = orgScreen.xy;
    float2 duv = max(abs(float2(ddx(uv.x), ddy(uv.y))), 1e-6);
    float2 outSize = clamp(round(1.0 / duv), 1.0, 16384.0);  // full-screen pass: uv step = 1 / output size
    float2 step = size / outSize;                            // one output pixel in source texels
    float2 p = saturate(uv) * size - 0.5;                    // centre of this output pixel, texel space
    int2 base = int2(floor(p));
    int2 maxc = max(int2(size) - 1, int2(0, 0));

    // Horizontal pass for x = p - step, p, p + step on the six rows base-2 .. base+3.
    float3 rowW[6], rowC[6], rowE[6];
    [unroll] for (int j = 0; j < 6; j++)
    {
        int y = clamp(base.y - 2 + j, 0, maxc.y);
        rowW[j] = 0; rowC[j] = 0; rowE[j] = 0;
        [unroll] for (int i = 0; i < 6; i++)
        {
            int x = base.x - 2 + i;
            float3 t = saturate(g_CurrentTexture.Load(int3(clamp(x, 0, maxc.x), y, 0)).rgb);
            float dx = p.x - x;
            rowW[j] += CatmullRom(dx - step.x) * t;
            rowC[j] += CatmullRom(dx) * t;
            rowE[j] += CatmullRom(dx + step.x) * t;
        }
    }
    // Vertical pass: centre, west, east on row p; north and south on rows p -/+ step.
    float3 e = 0, d = 0, f = 0, b = 0, h = 0;
    [unroll] for (int k = 0; k < 6; k++)
    {
        float dy = p.y - (base.y - 2 + k);
        float wc = CatmullRom(dy);
        e += wc * rowC[k];
        d += wc * rowW[k];
        f += wc * rowE[k];
        b += CatmullRom(dy - step.y) * rowC[k];
        h += CatmullRom(dy + step.y) * rowC[k];
    }
    e = saturate(e); d = saturate(d); f = saturate(f); b = saturate(b); h = saturate(h);

    // RCAS (FsrRcasF in ffx_fsr1.h): the strongest negative lobe that keeps the cross in range.
    float3 mn4 = min(min(b, d), min(f, h));
    float3 mx4 = max(max(b, d), max(f, h));
    float3 hitMin = min(mn4, e) / (4.0 * mx4 + 1e-5);
    float3 hitMax = (1.0 - max(mx4, e)) / (4.0 * mn4 - 4.0 - 1e-5);
    float3 lobe3 = max(-hitMin, hitMax);
    float lobe = max(-RCAS_LIMIT, min(max(lobe3.r, max(lobe3.g, lobe3.b)), 0.0)) * exp2(-RCAS_SHARPNESS);
    float3 c = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
    return float4(saturate(c), 1.0);
}
