// Upscale shader: AMD FidelityFX FSR 1.0, both stages in one pass (MIT licence, third_party/fsr1):
// EASU (edge-adaptive spatial upsampling) at the pixel and its four neighbours, then RCAS sharpening.
// Used for the final upscale after the DoF pass (and for the engine's temporal upsampler if that pass
// is not available).
// Same bindings and input signature as the original: t3 = current frame at render size,
// cb1.orgScreen.xy = render size. Gather4 is emulated with texel loads, so no sampler is needed.
#define A_GPU 1
#define A_HLSL 1
#include "third_party/fsr1/ffx_a.h"

cbuffer ParamBuffer : register(b1)
{
    float4 orgScreen;   // xy = source size in pixels, zw = 1 / source size
    uint isOddFrame_;
    uint isStopTUP_;
};

Texture2D<float4> g_CurrentTexture : register(t3);
static int2 gMax;

// Gather4 at normalized position p (a texel corner): .x=(0,1) .y=(1,1) .z=(1,0) .w=(0,0).
// EASU asks for R, G and B of the same position one after another; the 2x2 texels are loaded
// once for the R call and reused for G and B.
static AF3 gQ00, gQ10, gQ01, gQ11;
AF3 Tap(int2 c) { return g_CurrentTexture.Load(int3(clamp(c, int2(0, 0), gMax), 0)).rgb; }
void LoadQuad(AF2 p)
{
    int2 c = int2(floor(p * orgScreen.xy - 0.5));
    gQ00 = Tap(c); gQ10 = Tap(c + int2(1, 0)); gQ01 = Tap(c + int2(0, 1)); gQ11 = Tap(c + int2(1, 1));
}
AF4 FsrEasuRF(AF2 p) { LoadQuad(p); return AF4(gQ01.r, gQ11.r, gQ10.r, gQ00.r); }
AF4 FsrEasuGF(AF2 p) { return AF4(gQ01.g, gQ11.g, gQ10.g, gQ00.g); }
AF4 FsrEasuBF(AF2 p) { return AF4(gQ01.b, gQ11.b, gQ10.b, gQ00.b); }

#define FSR_EASU_F 1
#include "third_party/fsr1/ffx_fsr1.h"

#ifndef RCAS_SHARPNESS
#define RCAS_SHARPNESS 0.2          // stops: 0 = strongest, each +1 halves it (AMD's usual 0.2)
#endif
#define RCAS_LIMIT (0.25 - (1.0 / 16.0))

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    gMax = max(int2(orgScreen.xy) - 1, int2(0, 0));
    float2 duv = max(abs(float2(ddx(uv.x), ddy(uv.y))), 1e-6);
    float2 outSize = clamp(round(1.0 / duv), 1.0, 16384.0);
    AU4 con0, con1, con2, con3;
    FsrEasuCon(con0, con1, con2, con3, orgScreen.x, orgScreen.y, orgScreen.x, orgScreen.y, outSize.x, outSize.y);
    int2 ip = int2(pos.xy);
    int2 omax = int2(outSize) - 1;
    AF3 e, b, d, f, h;
    FsrEasuF(e, AU2(ip), con0, con1, con2, con3);
    FsrEasuF(b, AU2(clamp(ip + int2(0, -1), 0, omax)), con0, con1, con2, con3);
    FsrEasuF(h, AU2(clamp(ip + int2(0, 1), 0, omax)), con0, con1, con2, con3);
    FsrEasuF(d, AU2(clamp(ip + int2(-1, 0), 0, omax)), con0, con1, con2, con3);
    FsrEasuF(f, AU2(clamp(ip + int2(1, 0), 0, omax)), con0, con1, con2, con3);
    e = saturate(e); b = saturate(b); d = saturate(d); f = saturate(f); h = saturate(h);
    float3 mn4 = min(min(b, d), min(f, h));
    float3 mx4 = max(max(b, d), max(f, h));
    float3 hitMin = min(mn4, e) / (4.0 * mx4 + 1e-5);
    float3 hitMax = (1.0 - max(mx4, e)) / (4.0 * mn4 - 4.0 - 1e-5);
    float3 lobe3 = max(-hitMin, hitMax);
    float lobe = max(-RCAS_LIMIT, min(max(lobe3.r, max(lobe3.g, lobe3.b)), 0.0)) * exp2(-RCAS_SHARPNESS);
    float3 c = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
    return float4(saturate(c), 1.0);
}
