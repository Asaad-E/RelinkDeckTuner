// ---------- depth-of-field upscale fix (included by RelinkDeckTuner.cpp) ----------
// With the 3D rendered smaller than the screen, the last post pass, the layered-DoF compute shader
// (cs_filterlayereddof[_hi]), composes the frame at render size and stretches it into the
// full-size target by writing each pixel as a ceil(output/render)^2 block (outputPosRateX_/Y_):
// a nearest-neighbour enlargement that shows as stepped edges on hair and outlines. Everything
// before it (TAA, our upsampler) is clean.
// Fix: when the game creates that shader, hand it a copy whose outputPosRate reads are the
// constant 1.0 (it then writes 1:1 into the top-left render-size corner of the target), and right
// after each of its dispatches enlarge that corner over the whole target with our upscale shader.
// At native resolution the rate is 1.0 anyway and the extra pass is skipped.

void* AllocNear(uintptr_t target, size_t size);
bool BytesMatch(uintptr_t rva, const uint8_t* expected, size_t n);

constexpr uintptr_t kDofRenderSize = 0x6b822d8;  // 2 x i32: render width/height read by the DoF setup
constexpr int kCreateComputeShaderSlot = 18;     // ID3D11Device vtable index
constexpr int kDispatchSlot = 41;                // ID3D11DeviceContext vtable index

using CreateComputeShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*,
                                                          ID3D11ComputeShader**);
CreateComputeShaderFn g_realCreateComputeShader;
SRWLOCK g_passLock = SRWLOCK_INIT;  // the post-DoF resources are shared by all recording threads

bool g_dofFix;                               // DofFix in the .ini (read at start)
SRWLOCK g_dofLock = SRWLOCK_INIT;
std::vector<std::pair<ID3D11ComputeShader*, UINT>> g_dofShaders;  // patched shader, UAV slot of g_DofTexture
volatile LONG g_dofPasses;                   // post-DoF upscales done (state log)

// DXBC container checksum: MD5 rounds over everything after the 20-byte header, with the bit
// count placed in the first and last dwords of the final block (as fxc and the runtime do).
void DxbcChecksum(const uint8_t* data, size_t size, uint32_t out[4]) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t st[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    auto block = [&](const uint8_t* p) {
        uint32_t x[16];
        memcpy(x, p, 64);
        uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (b & d) | (c & ~d); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            f += a + K[i] + x[g];
            a = d; d = c; c = b;
            b += (f << R[i]) | (f >> (32 - R[i]));
        }
        st[0] += a; st[1] += b; st[2] += c; st[3] += d;
    };
    data += 20;
    size -= 20;
    size_t full = size / 64 * 64;
    for (size_t i = 0; i < full; i += 64) block(data + i);
    size_t rest = size - full;
    uint32_t bits = static_cast<uint32_t>(size * 8), part = (bits >> 2) | 1;
    uint8_t buf[64] = {};
    if (rest >= 56) {
        memcpy(buf, data + full, rest);
        buf[rest] = 0x80;
        block(buf);
        memset(buf, 0, 64);
        memcpy(buf, &bits, 4);
        memcpy(buf + 60, &part, 4);
        block(buf);
    } else {
        memcpy(buf, &bits, 4);
        memcpy(buf + 4, data + full, rest);
        buf[4 + rest] = 0x80;
        memcpy(buf + 60, &part, 4);
        block(buf);
    }
    memcpy(out, st, 16);
}

// Builds the patched copy of the layered-DoF compute shader. Returns the number of operands
// replaced (0 = not that shader or unexpected layout) and the UAV slot of g_DofTexture.
int PatchDofShader(const void* code, SIZE_T len, std::vector<uint8_t>& out, UINT& uavSlot) {
    const auto* d = static_cast<const uint8_t*>(code);
    auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, d + o, 4); return v; };
    if (len < 32 || memcmp(d, "DXBC", 4) != 0 || u32(24) != len) return 0;
    size_t rdef = 0, shex = 0, shexSize = 0;
    for (uint32_t i = 0, n = u32(28); i < n && 32 + 4 * i + 4 <= len; i++) {
        uint32_t off = u32(32 + 4 * i);
        if (off + 8 > len) return 0;
        if (!memcmp(d + off, "RDEF", 4)) rdef = off + 8;
        if (!memcmp(d + off, "SHEX", 4) || !memcmp(d + off, "SHDR", 4)) { shex = off + 8; shexSize = u32(off + 4); }
    }
    if (!rdef || !shex || shex + shexSize > len) return 0;
    auto str = [&](size_t o) { return reinterpret_cast<const char*>(d + rdef + o); };
    uint32_t cbCount = u32(rdef), cbOff = u32(rdef + 4), bindCount = u32(rdef + 8), bindOff = u32(rdef + 12);
    size_t varStride = memcmp(d + rdef + 28, "RD11", 4) == 0 ? 40 : 24;
    int64_t paramSlot = -1, dofSlot = -1;
    for (uint32_t i = 0; i < bindCount; i++) {
        size_t b = rdef + bindOff + 32 * i;
        uint32_t type = u32(b + 4), point = u32(b + 20);
        const char* name = str(u32(b));
        if (type == 0 && !strcmp(name, "ParamBuffer")) paramSlot = point;
        if (!strcmp(name, "g_DofTexture")) dofSlot = point;
    }
    int rateReg[2] = {-1, -1}, rateComp[2] = {-1, -1};
    for (uint32_t i = 0; i < cbCount; i++) {
        size_t c = rdef + cbOff + 24 * i;
        if (strcmp(str(u32(c)), "ParamBuffer")) continue;
        for (uint32_t k = 0, vc = u32(c + 4); k < vc; k++) {
            size_t v = rdef + u32(c + 8) + varStride * k;
            const char* vn = str(u32(v));
            int which = !strcmp(vn, "outputPosRateX_") ? 0 : !strcmp(vn, "outputPosRateY_") ? 1 : -1;
            if (which >= 0) { rateReg[which] = u32(v + 4) / 16; rateComp[which] = (u32(v + 4) % 16) / 4; }
        }
    }
    if (paramSlot < 0 || dofSlot < 0 || rateReg[0] < 0 || rateReg[1] < 0) return 0;

    out.assign(d, d + len);
    auto* t = reinterpret_cast<uint32_t*>(out.data() + shex);
    size_t count = shexSize / 4;
    std::vector<uint32_t> res(t, t + 2);
    int replaced = 0;
    for (size_t i = 2; i < count;) {
        uint32_t op = t[i] & 0x7ff;
        size_t ln = op == 0x35 ? t[i + 1] : (t[i] >> 24) & 0x7f;
        if (ln == 0 || i + ln > count) return 0;
        std::vector<uint32_t> ins(t + i, t + i + ln);
        if (op < 0x58 && op != 0x35) {  // computational instructions (declarations start at 0x58)
            std::vector<uint32_t> nw{ins[0]};
            int changed = 0;
            for (size_t j = 1; j < ins.size();) {
                uint32_t w = ins[j];
                bool cb = ((w >> 12) & 0xff) == 8 && ((w >> 20) & 3) == 2 && !(w >> 31) && ((w >> 22) & 0x1ff) == 0 &&
                          (w & 3) == 2 && j + 2 < ins.size() && ins[j + 1] == static_cast<uint32_t>(paramSlot);
                if (cb) {
                    // components read by the operand: mask, swizzle or single select
                    int mode = (w >> 2) & 3, sel[4], ns = 0;
                    if (mode == 1) for (int c = 0; c < 4; c++) sel[ns++] = (w >> (4 + 2 * c)) & 3;
                    else if (mode == 2) sel[ns++] = (w >> 4) & 3;
                    else for (int c = 0; c < 4; c++) if (w >> (4 + c) & 1) sel[ns++] = c;
                    for (int r = 0; r < 2 && cb; r++) {
                        bool all = ns > 0 && ins[j + 2] == static_cast<uint32_t>(rateReg[r]);
                        for (int c = 0; c < ns && all; c++) all = sel[c] == rateComp[r];
                        if (all) {
                            nw.push_back(0x4001);      // immediate32, 1 component
                            nw.push_back(0x3f800000);  // 1.0f
                            j += 3;
                            changed++;
                            cb = false;
                        }
                    }
                    if (!cb) continue;
                }
                nw.push_back(w);
                j++;
            }
            if (changed) {
                nw[0] = (nw[0] & ~(0x7fu << 24)) | (static_cast<uint32_t>(nw.size()) << 24);
                for (int k = 0; k < changed; k++) nw.push_back(0x3a | (1u << 24));  // nop keeps the size
                replaced += changed;
                ins.swap(nw);
            }
        }
        res.insert(res.end(), ins.begin(), ins.end());
        i += ln;
    }
    if (res.size() != count || !replaced) return 0;
    memcpy(t, res.data(), shexSize);
    uint32_t sum[4];
    DxbcChecksum(out.data(), out.size(), sum);
    memcpy(out.data() + 4, sum, 16);
    uavSlot = static_cast<UINT>(dofSlot);
    return replaced;
}

HRESULT STDMETHODCALLTYPE HookedCreateComputeShader(ID3D11Device* dev, const void* code, SIZE_T len,
                                                    ID3D11ClassLinkage* link, ID3D11ComputeShader** out) {
    if (g_dofFix && code && out) {
        std::vector<uint8_t> patched;
        UINT slot = 0;
        if (int n = PatchDofShader(code, len, patched, slot)) {
            HRESULT hr = g_realCreateComputeShader(dev, patched.data(), patched.size(), link, out);
            Log("layered-DoF compute shader patched (%d rate reads -> 1.0, target u%u): hr=0x%08lx %p", n, slot,
                static_cast<unsigned long>(hr), SUCCEEDED(hr) ? static_cast<void*>(*out) : nullptr);
            if (SUCCEEDED(hr)) {
                AcquireSRWLockExclusive(&g_dofLock);
                g_dofShaders.push_back({*out, slot});
                ReleaseSRWLockExclusive(&g_dofLock);
                return hr;
            }
        }
    }
    return g_realCreateComputeShader(dev, code, len, link, out);
}

// Resources of the post-DoF pass, created on first use (immediate-context thread only).
struct DofPass {
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11Texture2D* copy = nullptr;            // render-size copy of the corner
    ID3D11ShaderResourceView* copySrv = nullptr;
    UINT copyW = 0, copyH = 0;
    DXGI_FORMAT copyFmt = DXGI_FORMAT_UNKNOWN, viewFmt = DXGI_FORMAT_UNKNOWN;
    ID3D11Texture2D* target = nullptr;          // DoF target the RTV was made for (held)
    ID3D11RenderTargetView* targetRtv = nullptr;
    bool failed = false;
} g_dofPass;

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

bool PrepareDofPass(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* target, const D3D11_TEXTURE2D_DESC& td, DXGI_FORMAT viewFmt,
                    UINT rw, UINT rh) {
    DofPass& s = g_dofPass;
    if (!s.vs) {
        bool ok = SUCCEEDED(dev->CreateVertexShader(kFullscreenVS, sizeof(kFullscreenVS), nullptr, &s.vs));
        const void* ps = PostUpscalePS();
        SIZE_T psLen = PostUpscalePSSize();
        if (!g_testShader.empty()) { ps = g_testShader.data(); psLen = g_testShader.size(); }  // UpscalerShaderFile
        ok = ok && SUCCEEDED(dev->CreatePixelShader(ps, psLen, nullptr, &s.ps));
        D3D11_BUFFER_DESC bd = {32, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        ok = ok && SUCCEEDED(dev->CreateBuffer(&bd, nullptr, &s.cb));
        if (!ok) { s.failed = true; Log("post-DoF pass: shader/buffer creation failed, fix off"); return false; }
    }
    if (s.copyW != rw || s.copyH != rh || s.copyFmt != td.Format || s.viewFmt != viewFmt) {
        SafeRelease(s.copySrv);
        SafeRelease(s.copy);
        D3D11_TEXTURE2D_DESC cd = {};
        cd.Width = rw; cd.Height = rh; cd.MipLevels = 1; cd.ArraySize = 1; cd.Format = td.Format;
        cd.SampleDesc.Count = 1; cd.Usage = D3D11_USAGE_DEFAULT; cd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = viewFmt; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
        if (FAILED(dev->CreateTexture2D(&cd, nullptr, &s.copy)) ||
            FAILED(dev->CreateShaderResourceView(s.copy, &sd, &s.copySrv))) {
            s.failed = true;
            Log("post-DoF pass: %ux%u copy texture failed (format %d), fix off", rw, rh, static_cast<int>(td.Format));
            return false;
        }
        s.copyW = rw; s.copyH = rh; s.copyFmt = td.Format; s.viewFmt = viewFmt;
        float cbData[8] = {static_cast<float>(rw), static_cast<float>(rh), 1.0f / rw, 1.0f / rh, 0, 0, 0, 0};
        ctx->UpdateSubresource(s.cb, 0, nullptr, cbData, 0, 0);  // recorded with this pass
        Log("post-DoF pass: %ux%u corner -> %ux%u target with %s", rw, rh, td.Width, td.Height, PostUpscaleName());
    }
    if (s.target != target) {
        SafeRelease(s.targetRtv);
        SafeRelease(s.target);
        D3D11_RENDER_TARGET_VIEW_DESC rd = {};
        rd.Format = viewFmt; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        if (FAILED(dev->CreateRenderTargetView(target, &rd, &s.targetRtv))) {
            s.failed = true;
            Log("post-DoF pass: render target view failed, fix off");
            return false;
        }
        s.target = target;
        target->AddRef();
    }
    return true;
}

void PostDofUpscale(ID3D11DeviceContext* ctx, UINT slot) {
    if (g_dofPass.failed) return;
    int rw = *reinterpret_cast<volatile int*>(g_base + kDofRenderSize);
    int rh = *reinterpret_cast<volatile int*>(g_base + kDofRenderSize + 4);
    ID3D11UnorderedAccessView* uav = nullptr;
    ctx->CSGetUnorderedAccessViews(slot, 1, &uav);
    if (!uav) return;
    ID3D11Resource* res = nullptr;
    uav->GetResource(&res);
    ID3D11Texture2D* tex = nullptr;
    if (res) res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
    SafeRelease(res);
    D3D11_TEXTURE2D_DESC td = {};
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
    if (tex) { tex->GetDesc(&td); uav->GetDesc(&ud); }
    bool scaled = tex && rw > 0 && rh > 0 && static_cast<UINT>(rw) <= td.Width && static_cast<UINT>(rh) <= td.Height &&
                  (static_cast<UINT>(rw) < td.Width || static_cast<UINT>(rh) < td.Height) &&
                  (td.BindFlags & D3D11_BIND_RENDER_TARGET) && td.ArraySize == 1 && td.SampleDesc.Count == 1;
    ID3D11Device* dev = nullptr;
    if (scaled) ctx->GetDevice(&dev);
    if (scaled && dev) AcquireSRWLockExclusive(&g_passLock);
    if (scaled && dev && PrepareDofPass(dev, ctx, tex, td, ud.Format, rw, rh)) {
        DofPass& s = g_dofPass;
        D3D11_BOX box = {0, 0, 0, static_cast<UINT>(rw), static_cast<UINT>(rh), 1};
        ctx->CopySubresourceRegion(s.copy, 0, 0, 0, 0, tex, 0, &box);

        // Save the state this pass touches.
        ID3D11DeviceContext1* ctx1 = nullptr;
        ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&ctx1));
        ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        ID3D11DepthStencilView* dsv = nullptr;
        ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
        D3D11_VIEWPORT vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        ctx->RSGetViewports(&nvp, vps);
        ID3D11RasterizerState* rs = nullptr;
        ctx->RSGetState(&rs);
        ID3D11BlendState* bs = nullptr;
        float bf[4];
        UINT sampleMask;
        ctx->OMGetBlendState(&bs, bf, &sampleMask);
        ID3D11DepthStencilState* dss = nullptr;
        UINT stencilRef;
        ctx->OMGetDepthStencilState(&dss, &stencilRef);
        ID3D11InputLayout* il = nullptr;
        ctx->IAGetInputLayout(&il);
        D3D11_PRIMITIVE_TOPOLOGY topo;
        ctx->IAGetPrimitiveTopology(&topo);
        ID3D11VertexShader* vs = nullptr;
        ctx->VSGetShader(&vs, nullptr, nullptr);
        ID3D11GeometryShader* gs = nullptr;
        ctx->GSGetShader(&gs, nullptr, nullptr);
        ID3D11HullShader* hs = nullptr;
        ctx->HSGetShader(&hs, nullptr, nullptr);
        ID3D11DomainShader* ds = nullptr;
        ctx->DSGetShader(&ds, nullptr, nullptr);
        ID3D11PixelShader* ps = nullptr;
        ctx->PSGetShader(&ps, nullptr, nullptr);
        ID3D11ShaderResourceView* srv3 = nullptr;
        ctx->PSGetShaderResources(3, 1, &srv3);
        ID3D11Buffer* cb1 = nullptr;
        UINT cbFirst = 0, cbNum = 0;
        if (ctx1) ctx1->PSGetConstantBuffers1(1, 1, &cb1, &cbFirst, &cbNum);
        else ctx->PSGetConstantBuffers(1, 1, &cb1);

        // Enlarge the corner over the whole target.
        ctx->OMSetRenderTargets(1, &s.targetRtv, nullptr);  // also unbinds the target's UAV from the CS
        D3D11_VIEWPORT vp = {0, 0, static_cast<float>(td.Width), static_cast<float>(td.Height), 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(nullptr);
        ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        ctx->OMSetDepthStencilState(nullptr, 0);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(s.vs, nullptr, 0);
        ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->HSSetShader(nullptr, nullptr, 0);
        ctx->DSSetShader(nullptr, nullptr, 0);
        ctx->PSSetShader(s.ps, nullptr, 0);
        ctx->PSSetShaderResources(3, 1, &s.copySrv);
        ctx->PSSetConstantBuffers(1, 1, &s.cb);
        ctx->Draw(3, 0);

        // Restore.
        ctx->PSSetShaderResources(3, 1, &srv3);
        if (ctx1 && cbNum) ctx1->PSSetConstantBuffers1(1, 1, &cb1, &cbFirst, &cbNum);
        else ctx->PSSetConstantBuffers(1, 1, &cb1);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->DSSetShader(ds, nullptr, 0);
        ctx->HSSetShader(hs, nullptr, 0);
        ctx->GSSetShader(gs, nullptr, 0);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->IASetPrimitiveTopology(topo);
        ctx->IASetInputLayout(il);
        ctx->OMSetDepthStencilState(dss, stencilRef);
        ctx->OMSetBlendState(bs, bf, sampleMask);
        ctx->RSSetState(rs);
        ctx->RSSetViewports(nvp, vps);
        ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv);
        ctx->CSSetUnorderedAccessViews(slot, 1, &uav, nullptr);
        for (auto*& r : rtvs) SafeRelease(r);
        SafeRelease(dsv); SafeRelease(rs); SafeRelease(bs); SafeRelease(dss); SafeRelease(il); SafeRelease(vs);
        SafeRelease(gs); SafeRelease(hs); SafeRelease(ds); SafeRelease(ps); SafeRelease(srv3); SafeRelease(cb1);
        SafeRelease(ctx1);
        InterlockedIncrement(&g_dofPasses);
    }
    if (scaled && dev) ReleaseSRWLockExclusive(&g_passLock);
    SafeRelease(dev);
    SafeRelease(tex);
    SafeRelease(uav);
}

// The engine issues the DoF dispatch itself at kDofDispatchCall (call [rax+148h] on the D3D11
// context, x/y groups from the pass, z = 1). That 6-byte call becomes call [rip+X] into DofDispatch,
// which makes the same call through the context's current function table and then runs the post
// pass. No D3D11 function tables are modified (the native runtime rewrites its own).
constexpr uintptr_t kDofDispatchCall = 0x244c336;
const uint8_t kDofDispatchCallBytes[] = {0xff, 0x90, 0x48, 0x01, 0x00, 0x00};  // call qword ptr [rax+148h]
// The pass runs 4 times per frame (invocationCount_ 0..3, loop at 0x244bfd7), each time writing the
// whole target; the counter is stored as a float at kDofInvocation (0x244c278) before each dispatch.
constexpr uintptr_t kDofInvocationStore = 0x244c278, kDofInvocation = 0x6e164f8;
const uint8_t kDofInvocationStoreBytes[] = {0xc5, 0xfa, 0x11, 0x05, 0x78, 0xa2, 0x9c, 0x04};  // vmovss [rip+..], xmm0
constexpr float kDofLastInvocation = 3.0f;

void STDMETHODCALLTYPE DofDispatch(ID3D11DeviceContext* ctx, UINT x, UINT y, UINT z) {
    using Fn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT);
    reinterpret_cast<Fn>((*reinterpret_cast<void***>(ctx))[kDispatchSlot])(ctx, x, y, z);
    if (g_dofShaders.empty() || *reinterpret_cast<volatile float*>(g_base + kDofInvocation) < kDofLastInvocation) return;
    ID3D11ComputeShader* cs = nullptr;
    ctx->CSGetShader(&cs, nullptr, nullptr);
    if (!cs) return;
    int slot = -1;
    AcquireSRWLockShared(&g_dofLock);
    for (auto& e : g_dofShaders)
        if (e.first == cs) slot = static_cast<int>(e.second);
    ReleaseSRWLockShared(&g_dofLock);
    cs->Release();
    if (slot >= 0) PostDofUpscale(ctx, static_cast<UINT>(slot));
}

bool InstallDofDispatchCall() {
    uintptr_t site = g_base + kDofDispatchCall;
    if (!BytesMatch(kDofDispatchCall, kDofDispatchCallBytes, sizeof(kDofDispatchCallBytes)) ||
        !BytesMatch(kDofInvocationStore, kDofInvocationStoreBytes, sizeof(kDofInvocationStoreBytes)))
        return false;
    auto* slot = static_cast<void**>(AllocNear(site, 16));
    if (!slot) return false;
    *slot = reinterpret_cast<void*>(&DofDispatch);
    intptr_t disp = reinterpret_cast<intptr_t>(slot) - static_cast<intptr_t>(site + 6);
    if (disp < INT32_MIN || disp > INT32_MAX) return false;
    uint8_t patch[6] = {0xff, 0x15};  // call qword ptr [rip+disp32]
    int32_t d32 = static_cast<int32_t>(disp);
    memcpy(patch + 2, &d32, 4);
    DWORD old;
    VirtualProtect(reinterpret_cast<void*>(site), 6, PAGE_EXECUTE_READWRITE, &old);
    memcpy(reinterpret_cast<void*>(site), patch, 6);
    VirtualProtect(reinterpret_cast<void*>(site), 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), 6);
    return true;
}

// Called with the device lock held: hooks CreateComputeShader (device function tables are static).
void HookDofFix(void* device) {
    if (!g_dofFix) return;
    void** vtbl = *static_cast<void***>(device);
    DWORD old;
    if (vtbl[kCreateComputeShaderSlot] == reinterpret_cast<void*>(&HookedCreateComputeShader) ||
        !VirtualProtect(&vtbl[kCreateComputeShaderSlot], sizeof(void*), PAGE_READWRITE, &old))
        return;
    g_realCreateComputeShader = reinterpret_cast<CreateComputeShaderFn>(vtbl[kCreateComputeShaderSlot]);
    vtbl[kCreateComputeShaderSlot] = reinterpret_cast<void*>(&HookedCreateComputeShader);
    VirtualProtect(&vtbl[kCreateComputeShaderSlot], sizeof(void*), old, &old);
    Log("ID3D11Device::CreateComputeShader hooked");
}
