// Replacement for the engine's temporal-upsampling resolve pixel shader (DXBC hash 0e4625da...):
// AMD FidelityFX FSR 1.0 EASU (edge-adaptive spatial upsampling), MIT licence, third_party/fsr1.
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

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    gMax = max(int2(orgScreen.xy) - 1, int2(0, 0));
    float2 duv = max(abs(float2(ddx(uv.x), ddy(uv.y))), 1e-6);
    float2 outSize = clamp(round(1.0 / duv), 1.0, 16384.0);  // full-screen pass: uv step = 1 / output size
    AU4 con0, con1, con2, con3;
    FsrEasuCon(con0, con1, con2, con3, orgScreen.x, orgScreen.y, orgScreen.x, orgScreen.y, outSize.x, outSize.y);
    AF3 c;
    FsrEasuF(c, AU2(pos.xy), con0, con1, con2, con3);
    return float4(max(c, 0.0), 1.0);
}
