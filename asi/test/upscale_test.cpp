// Offline test for the replacement upscale shaders: runs a compiled pixel shader (.cso) the same
// way the game does (t3 = source, cb1.orgScreen = source size, full-screen triangle with TEXCOORD0)
// on a PNG and writes the 1280x720 result as PNG.
// Usage: upscale_test <shader.cso> <in.png> <out.png> [outW outH]
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <cstdio>
#include <vector>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

#define CHECK(x) do { HRESULT _hr = (x); if (FAILED(_hr)) { printf("FAILED 0x%08lx: %s (line %d)\n", (unsigned long)_hr, #x, __LINE__); return 1; } } while (0)

static std::vector<char> ReadFile(const wchar_t* path) {
    std::vector<char> d; FILE* f = _wfopen(path, L"rb"); if (!f) return d;
    fseek(f, 0, SEEK_END); d.resize(ftell(f)); fseek(f, 0, SEEK_SET); fread(d.data(), 1, d.size(), f); fclose(f); return d;
}

static const char* kVS = R"(
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O main(uint id : SV_VertexID) { O o; o.uv = float2((id << 1) & 2, id & 2); o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }
)";

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) { printf("usage: upscale_test shader.cso in.png out.png [w h]\n"); return 2; }
    UINT outW = argc > 5 ? _wtoi(argv[4]) : 1280, outH = argc > 5 ? _wtoi(argv[5]) : 720;
    CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    IWICImagingFactory* wic; CHECK(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
    IWICBitmapDecoder* dec; CHECK(wic->CreateDecoderFromFilename(argv[2], nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec));
    IWICBitmapFrameDecode* frame; CHECK(dec->GetFrame(0, &frame));
    IWICFormatConverter* conv; CHECK(wic->CreateFormatConverter(&conv));
    CHECK(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    UINT w, h; conv->GetSize(&w, &h);
    std::vector<BYTE> src(w * h * 4); CHECK(conv->CopyPixels(nullptr, w * 4, (UINT)src.size(), src.data()));

    ID3D11Device* dev; ID3D11DeviceContext* ctx;
    CHECK(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx));
    D3D11_TEXTURE2D_DESC td = {w, h, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE};
    D3D11_SUBRESOURCE_DATA sd = {src.data(), w * 4};
    ID3D11Texture2D* tex; CHECK(dev->CreateTexture2D(&td, &sd, &tex));
    ID3D11ShaderResourceView* srv; CHECK(dev->CreateShaderResourceView(tex, nullptr, &srv));
    D3D11_TEXTURE2D_DESC rd = {outW, outH, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET};
    ID3D11Texture2D* rt; CHECK(dev->CreateTexture2D(&rd, nullptr, &rt));
    ID3D11RenderTargetView* rtv; CHECK(dev->CreateRenderTargetView(rt, nullptr, &rtv));
    float cb[8] = {(float)w, (float)h, 1.0f / w, 1.0f / h, 0, 0, 0, 0};
    D3D11_BUFFER_DESC bd = {sizeof(cb), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
    D3D11_SUBRESOURCE_DATA cbd = {cb};
    ID3D11Buffer* cbuf; CHECK(dev->CreateBuffer(&bd, &cbd, &cbuf));
    ID3DBlob* vsb; ID3DBlob* err = nullptr;
    CHECK(D3DCompile(kVS, strlen(kVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsb, &err));
    ID3D11VertexShader* vs; CHECK(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs));
    auto psCode = ReadFile(argv[1]);
    ID3D11PixelShader* ps; CHECK(dev->CreatePixelShader(psCode.data(), psCode.size(), nullptr, &ps));

    D3D11_VIEWPORT vp = {0, 0, (float)outW, (float)outH, 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ctx->PSSetShaderResources(3, 1, &srv);
    ctx->PSSetConstantBuffers(1, 1, &cbuf);
    ctx->Draw(3, 0);

    // GPU timing of 100 runs
    D3D11_QUERY_DESC qd = {D3D11_QUERY_TIMESTAMP_DISJOINT}; ID3D11Query* disj; dev->CreateQuery(&qd, &disj);
    qd.Query = D3D11_QUERY_TIMESTAMP; ID3D11Query *t0, *t1; dev->CreateQuery(&qd, &t0); dev->CreateQuery(&qd, &t1);
    ctx->Begin(disj); ctx->End(t0); for (int i = 0; i < 100; i++) ctx->Draw(3, 0); ctx->End(t1); ctx->End(disj);
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj; UINT64 a, b;
    while (ctx->GetData(disj, &dj, sizeof(dj), 0) != S_OK) {}
    while (ctx->GetData(t0, &a, sizeof(a), 0) != S_OK) {}
    while (ctx->GetData(t1, &b, sizeof(b), 0) != S_OK) {}
    if (!dj.Disjoint) printf("gpu time per pass: %.3f ms\n", (double)(b - a) / dj.Frequency * 1000.0 / 100);

    rd.Usage = D3D11_USAGE_STAGING; rd.BindFlags = 0; rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* st; CHECK(dev->CreateTexture2D(&rd, nullptr, &st));
    ctx->CopyResource(st, rt);
    D3D11_MAPPED_SUBRESOURCE m; CHECK(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m));
    std::vector<BYTE> out(outW * outH * 4);
    for (UINT y = 0; y < outH; y++) memcpy(&out[y * outW * 4], (BYTE*)m.pData + y * m.RowPitch, outW * 4);
    ctx->Unmap(st, 0);

    IWICBitmap* bmp; CHECK(wic->CreateBitmapFromMemory(outW, outH, GUID_WICPixelFormat32bppRGBA, outW * 4, (UINT)out.size(), out.data(), &bmp));
    IWICStream* stream; CHECK(wic->CreateStream(&stream)); CHECK(stream->InitializeFromFilename(argv[3], GENERIC_WRITE));
    IWICBitmapEncoder* enc; CHECK(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)); CHECK(enc->Initialize(stream, WICBitmapEncoderNoCache));
    IWICBitmapFrameEncode* fe; CHECK(enc->CreateNewFrame(&fe, nullptr)); CHECK(fe->Initialize(nullptr));
    CHECK(fe->SetSize(outW, outH)); WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA; CHECK(fe->SetPixelFormat(&fmt));
    CHECK(fe->WriteSource(bmp, nullptr)); CHECK(fe->Commit()); CHECK(enc->Commit());
    printf("ok %ux%u -> %ux%u\n", w, h, outW, outH);
    return 0;
}
