// RelinkDeckTuner.asi - lower the render resolution only while a dialogue/cutscene camera is active.
// Granblue Fantasy: Relink v2.0.5 only: every address is checked against the expected bytes first.
//
// Dialogue detection: a counter is bumped by a small hook in the cutscene camera update
// (0x942698, next to GBFRelinkFix's "Cutscene FOV" hook at 0x9426a4). Hits in the last
// ExitDelayMs => dialogue.
// Resolution: 0x6bbaa0 copies graphics preset row[index] +0x24..+0x30 into the render-size
// globals every frame, so we scale those fields while in dialogue and restore them after.
//
// Upscaler: the engine's temporal upsampler is replaced by FSR1 EASU or bicubic (see below).
//
// The .ini next to the .asi is re-read every second (can be edited while the game runs);
// the .log next to it records what happens.
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <intrin.h>

#include "upscale_shaders.h"

namespace {

constexpr uintptr_t kPresetTable = 0x6b82460;  // 6 rows x 0x4C
constexpr uintptr_t kPresetIndex = 0x7034940;  // u32, 5 = Custom
constexpr uintptr_t kRenderSize = 0x6b822e0;   // 4 x i32 written from row +0x24..+0x30
constexpr uintptr_t kSizeCopy = 0x6bbaa0;      // function that copies them
constexpr uintptr_t kCamHook = 0x942698;       // mov rax,[rsi+60h]; vmovss xmm2,[rax+9D4h]
constexpr uintptr_t kCamResume = 0x9426a4;
constexpr int kRowSize = 0x4c;
// Scene-graph LOD selection (0x23a4bac: vmulss xmm3, [table + flag*4]) scales each model's screen size
// by this 2-float table before comparing it with the model's lod_screen_size_thresholds. Entry 1
// (1.0) is the normal factor; lowering it makes scenery pick lighter LODs. Only read there.
constexpr uintptr_t kLodFactorTable = 0x54abf98;
const float kLodFactorDefault[2] = {0.25f, 1.0f};

const uint8_t kSizeCopyBytes[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53,
                                  0x48, 0x83, 0xec, 0x38, 0x8b, 0x05, 0x8a, 0x8e, 0x97, 0x06};
const uint8_t kCamHookBytes[] = {0x48, 0x8b, 0x46, 0x60, 0xc5, 0xfa, 0x10, 0x90, 0xd4, 0x09, 0x00, 0x00};

struct Config {
    int mode = 1;           // 0 = off, 1 = only in dialogue, 2 = always (test)
    double scale = 0.75;    // per axis
    int fields = 3;         // bit 1: row +0x24/+0x28, bit 2: row +0x2c/+0x30
    int exitDelayMs = 700;  // dialogue considered over after this long without camera hits
    int logPeriodMs = 2000; // periodic snapshot; 0 = only on changes
    double lodBias = 1.0;   // scenery LOD factor during dialogues (1.0 = unchanged)
    double baseScale = 1.0; // 3D scale outside dialogues in Mode 1 (1.0 = native)
    int keyBase = 0x78;     // virtual key that cycles BaseScale (F9); 0 = disabled
    int keyDialogue = 0x79; // virtual key that cycles Scale (F10); 0 = disabled
};

uintptr_t g_base;
volatile LONG64* g_camHits;
volatile LONG64* g_frames;  // bumped once per frame by the main loop (render-size update)
char g_iniPath[MAX_PATH], g_logPath[MAX_PATH];
FILE* g_log;

void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// ---------- upscaler replacement ----------
// When the render size is lowered, the engine upsamples the 3D image with its temporal upsampler
// (pixel shader below, "TUP"). That shader expects alternating-column reconstruction and leaves
// horizontal bands at arbitrary scales, so when the game creates it we hand it FSR1 EASU or a
// bicubic shader instead (same bindings). Matched by the DXBC checksum, nothing else is touched.
const uint8_t kTupShaderHash[16] = {0x0e, 0x46, 0x25, 0xda, 0x89, 0x89, 0x54, 0x02,
                                    0x38, 0x98, 0x44, 0xe5, 0x78, 0x56, 0x12, 0x32};

using CreateDeviceFn = HRESULT(WINAPI*)(void*, int, HMODULE, UINT, const void*, UINT, UINT, void**, void*, void**);
using CreatePixelShaderFn = HRESULT(STDMETHODCALLTYPE*)(void*, const void*, SIZE_T, void*, void**);
constexpr int kCreatePixelShaderSlot = 15;  // ID3D11Device vtable index

CreateDeviceFn g_realCreateDevice;
CreatePixelShaderFn g_realCreatePixelShader;
int g_upscaler;  // 1 = FSR1 EASU, 2 = bicubic

HRESULT STDMETHODCALLTYPE HookedCreatePixelShader(void* device, const void* code, SIZE_T len, void* linkage,
                                                  void** out) {
    if (code && len > 20 && memcmp(static_cast<const uint8_t*>(code) + 4, kTupShaderHash, 16) == 0) {
        const void* repl = g_upscaler == 2 ? kBicubicPS : kEasuPS;
        SIZE_T replLen = g_upscaler == 2 ? sizeof(kBicubicPS) : sizeof(kEasuPS);
        HRESULT hr = g_realCreatePixelShader(device, repl, replLen, linkage, out);
        Log("temporal upsampler shader replaced by %s: hr=0x%08lx", g_upscaler == 2 ? "bicubic" : "FSR1 EASU",
            static_cast<unsigned long>(hr));
        if (SUCCEEDED(hr)) return hr;
        Log("replacement rejected, keeping the original shader");
    }
    return g_realCreatePixelShader(device, code, len, linkage, out);
}

void HookDeviceVtable(void* device) {
    static SRWLOCK lock = SRWLOCK_INIT;  // devices created from several threads must not race here
    AcquireSRWLockExclusive(&lock);
    void** vtbl = *static_cast<void***>(device);
    DWORD old;
    if (vtbl[kCreatePixelShaderSlot] != reinterpret_cast<void*>(&HookedCreatePixelShader) &&
        VirtualProtect(&vtbl[kCreatePixelShaderSlot], sizeof(void*), PAGE_READWRITE, &old)) {
        g_realCreatePixelShader = reinterpret_cast<CreatePixelShaderFn>(vtbl[kCreatePixelShaderSlot]);
        vtbl[kCreatePixelShaderSlot] = reinterpret_cast<void*>(&HookedCreatePixelShader);
        VirtualProtect(&vtbl[kCreatePixelShaderSlot], sizeof(void*), old, &old);
        Log("ID3D11Device::CreatePixelShader hooked");
    }
    ReleaseSRWLockExclusive(&lock);
}

HRESULT WINAPI HookedCreateDevice(void* adapter, int driverType, HMODULE sw, UINT flags, const void* levels,
                                  UINT numLevels, UINT sdk, void** device, void* level, void** context) {
    HRESULT hr = g_realCreateDevice(adapter, driverType, sw, flags, levels, numLevels, sdk, device, level, context);
    if (SUCCEEDED(hr) && device && *device) HookDeviceVtable(*device);
    return hr;
}

// Replace one entry of the game's import table (no code patching).
bool HookImport(const char* dll, const char* func, void* hook, void** original) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleA(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; imp++) {
        if (_stricmp(reinterpret_cast<char*>(base + imp->Name), dll) != 0) continue;
        if (!imp->OriginalFirstThunk) continue;  // no name table: cannot match by name
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(byName->Name, func) != 0) continue;
            DWORD old;
            VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            *original = reinterpret_cast<void*>(slots->u1.Function);
            slots->u1.Function = reinterpret_cast<ULONG_PTR>(hook);
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
            return true;
        }
    }
    return false;
}

bool BytesMatch(uintptr_t rva, const uint8_t* expected, size_t n) {
    return memcmp(reinterpret_cast<void*>(g_base + rva), expected, n) == 0;
}

void* AllocNear(uintptr_t target, size_t size) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t gran = si.dwAllocationGranularity;
    for (uintptr_t delta = gran; delta < 0x70000000; delta += gran) {
        for (int sign = -1; sign <= 1; sign += 2) {
            uintptr_t addr = ((target + sign * (intptr_t)delta) / gran) * gran;
            if (addr == 0) continue;
            void* p = VirtualAlloc(reinterpret_cast<void*>(addr), size, MEM_COMMIT | MEM_RESERVE,
                                   PAGE_EXECUTE_READWRITE);
            if (p) return p;
        }
    }
    return nullptr;
}

// Counting hook: replaces `len` bytes of position-independent code at `site` with a jump to a
// trampoline that does `lock inc qword [counter]`, runs the original bytes and jumps back.
volatile LONG64* InstallCounterHook(uintptr_t site, const uint8_t* orig, size_t len) {
    auto* tramp = static_cast<uint8_t*>(AllocNear(site, 0x1000));
    if (!tramp || len < 5 || len > 16) return nullptr;
    intptr_t dist = reinterpret_cast<intptr_t>(tramp) - static_cast<intptr_t>(site + 5);
    if (dist < INT32_MIN + 0x1000 || dist > INT32_MAX - 0x1000) return nullptr;  // jmp rel32 range
    uint8_t* p = tramp;
    *p++ = 0xf0; *p++ = 0x48; *p++ = 0xff; *p++ = 0x05;  // lock inc qword ptr [rip + rel]
    int32_t rel = static_cast<int32_t>((tramp + 0x100) - (p + 4));
    memcpy(p, &rel, 4); p += 4;
    memcpy(p, orig, len); p += len;                         // original code
    *p++ = 0xff; *p++ = 0x25; *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 0;  // jmp [rip+0]
    uintptr_t resume = site + len;
    memcpy(p, &resume, 8);

    FlushInstructionCache(GetCurrentProcess(), tramp, 0x1000);

    uint8_t patch[16];
    memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xe9;
    rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(tramp) - (site + 5));
    memcpy(patch + 1, &rel, 4);
    DWORD old;
    VirtualProtect(reinterpret_cast<void*>(site), len, PAGE_EXECUTE_READWRITE, &old);
    memcpy(reinterpret_cast<void*>(site), patch, len);
    VirtualProtect(reinterpret_cast<void*>(site), len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), len);
    return reinterpret_cast<volatile LONG64*>(tramp + 0x100);
}

// Frame statistics hook on the per-frame function: counts frames and, with RDTSC, keeps the longest
// interval between two frames and how many intervals exceed a threshold (stutter). Data block at
// tramp+0x100: frames, prevTsc, maxDelta, slowCount, slowThreshold (all u64).
struct FrameStats { volatile LONG64 frames, prev, maxDelta, slow, threshold; };
FrameStats* g_fs;

FrameStats* InstallFrameStatsHook(uintptr_t site, const uint8_t* orig, size_t len) {
    auto* tramp = static_cast<uint8_t*>(AllocNear(site, 0x1000));
    if (!tramp || len < 5 || len > 16) return nullptr;
    intptr_t dist = reinterpret_cast<intptr_t>(tramp) - static_cast<intptr_t>(site + 5);
    if (dist < INT32_MIN + 0x1000 || dist > INT32_MAX - 0x1000) return nullptr;
    auto* fs = reinterpret_cast<FrameStats*>(tramp + 0x100);
    fs->threshold = INT64_MAX;  // set after TSC calibration
    uint8_t* p = tramp;
    auto bytes = [&](std::initializer_list<uint8_t> b) { for (uint8_t x : b) *p++ = x; };
    auto rip = [&](const volatile void* target) {  // rel32 to target; the disp ends the instruction
        int32_t d = static_cast<int32_t>(reinterpret_cast<const volatile uint8_t*>(target) - (p + 4));
        memcpy(p, &d, 4); p += 4;
    };
    bytes({0x50, 0x52});                                // push rax; push rdx
    bytes({0xf0, 0x48, 0xff, 0x05}); rip(&fs->frames);  // lock inc qword [frames]
    bytes({0x0f, 0x31});                                // rdtsc
    bytes({0x48, 0xc1, 0xe2, 0x20});                    // shl rdx, 32
    bytes({0x48, 0x09, 0xd0});                          // or rax, rdx
    bytes({0x48, 0x89, 0xc2});                          // mov rdx, rax
    bytes({0x48, 0x87, 0x15}); rip(&fs->prev);          // xchg rdx, [prev]
    bytes({0x48, 0x85, 0xd2});                          // test rdx, rdx
    uint8_t* jzDone = p; bytes({0x74, 0x00});           // jz done
    bytes({0x48, 0x29, 0xd0});                          // sub rax, rdx      (delta)
    bytes({0x48, 0x3b, 0x05}); rip(&fs->maxDelta);      // cmp rax, [maxDelta]
    uint8_t* jbeNoMax = p; bytes({0x76, 0x00});         // jbe nomax
    bytes({0x48, 0x89, 0x05}); rip(&fs->maxDelta);      // mov [maxDelta], rax
    jbeNoMax[1] = static_cast<uint8_t>(p - (jbeNoMax + 2));
    bytes({0x48, 0x3b, 0x05}); rip(&fs->threshold);     // cmp rax, [threshold]
    uint8_t* jbeDone = p; bytes({0x76, 0x00});          // jbe done
    bytes({0xf0, 0x48, 0xff, 0x05}); rip(&fs->slow);    // lock inc qword [slow]
    jzDone[1] = static_cast<uint8_t>(p - (jzDone + 2));
    jbeDone[1] = static_cast<uint8_t>(p - (jbeDone + 2));
    bytes({0x5a, 0x58});                                // pop rdx; pop rax
    memcpy(p, orig, len); p += len;                     // original code
    bytes({0xff, 0x25, 0, 0, 0, 0});                    // jmp [rip+0]
    uintptr_t resume = site + len;
    memcpy(p, &resume, 8);
    FlushInstructionCache(GetCurrentProcess(), tramp, 0x1000);

    uint8_t patch[16];
    memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xe9;
    int32_t rel = static_cast<int32_t>(dist);
    memcpy(patch + 1, &rel, 4);
    DWORD old;
    VirtualProtect(reinterpret_cast<void*>(site), len, PAGE_EXECUTE_READWRITE, &old);
    memcpy(reinterpret_cast<void*>(site), patch, len);
    VirtualProtect(reinterpret_cast<void*>(site), len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), len);
    return fs;
}

// TSC ticks per millisecond, measured against QueryPerformanceCounter over ~200 ms.
double CalibrateTscPerMs() {
    LARGE_INTEGER f, q0, q1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&q0);
    unsigned long long t0 = __rdtsc();
    Sleep(200);
    QueryPerformanceCounter(&q1);
    unsigned long long t1 = __rdtsc();
    double ms = (q1.QuadPart - q0.QuadPart) * 1000.0 / f.QuadPart;
    return ms > 0 ? (t1 - t0) / ms : 0;
}

void ReadConfig(Config& c) {
    char buf[64];
    c.mode = GetPrivateProfileIntA("RelinkDeckTuner", "Mode", 1, g_iniPath);
    GetPrivateProfileStringA("RelinkDeckTuner", "Scale", "0.75", buf, sizeof(buf), g_iniPath);
    c.scale = atof(buf);
    if (!(c.scale >= 0.4)) c.scale = 0.4;  // also catches NaN
    if (c.scale > 1.0) c.scale = 1.0;
    c.fields = GetPrivateProfileIntA("RelinkDeckTuner", "Fields", 3, g_iniPath);
    c.exitDelayMs = GetPrivateProfileIntA("RelinkDeckTuner", "ExitDelayMs", 700, g_iniPath);
    if (c.exitDelayMs < 0) c.exitDelayMs = 0;
    c.logPeriodMs = GetPrivateProfileIntA("RelinkDeckTuner", "LogPeriodMs", 2000, g_iniPath);
    GetPrivateProfileStringA("RelinkDeckTuner", "BaseScale", "1.0", buf, sizeof(buf), g_iniPath);
    c.baseScale = atof(buf);
    if (!(c.baseScale >= 0.4)) c.baseScale = 0.4;
    if (c.baseScale > 1.0) c.baseScale = 1.0;
    c.keyBase = GetPrivateProfileIntA("RelinkDeckTuner", "HotkeyBaseScale", 0x78, g_iniPath);
    c.keyDialogue = GetPrivateProfileIntA("RelinkDeckTuner", "HotkeyDialogueScale", 0x79, g_iniPath);
    GetPrivateProfileStringA("RelinkDeckTuner", "DialogueLodBias", "1.0", buf, sizeof(buf), g_iniPath);
    c.lodBias = atof(buf);
    if (!(c.lodBias >= 0.1)) c.lodBias = 0.1;
    if (c.lodBias > 1.0) c.lodBias = 1.0;
}

float* g_lodFactor;  // &table[1], made writable at init; null if the table did not match

void SetLodFactor(float v) {
    if (!g_lodFactor || *g_lodFactor == v) return;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(g_lodFactor), *reinterpret_cast<LONG*>(&v));
    Log("scenery LOD factor -> %.2f", v);
}

// Write width/height pairs as single 64-bit stores so the main thread, which copies these fields
// every frame, never sees a new width with an old height.
void StoreSizes(int32_t* dst, const int32_t* src) {
    for (int pair = 0; pair < 2; pair++) {
        int64_t v;
        memcpy(&v, src + pair * 2, sizeof(v));
        auto* d = dst + pair * 2;
        if ((reinterpret_cast<uintptr_t>(d) & 7) == 0)
            InterlockedExchange64(reinterpret_cast<volatile LONG64*>(d), v);
        else
            memcpy(d, &v, sizeof(v));
    }
}

int32_t* Row(uint32_t index) {
    return reinterpret_cast<int32_t*>(g_base + kPresetTable + index * kRowSize);
}

// Hotkeys: each press moves a scale to the next step and saves it to the .ini (which the loop then
// re-reads, so the .ini always shows the value in use).
const double kScaleSteps[] = {1.0, 0.95, 0.9, 0.85, 0.8, 0.75};

double NextStep(double cur) {
    for (double s : kScaleSteps)
        if (s < cur - 0.001) return s;
    return kScaleSteps[0];
}

bool KeyPressed(int vk, bool& wasDown) {
    if (!vk) return false;
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

void SaveScale(const char* key, double v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", v);
    WritePrivateProfileStringA("RelinkDeckTuner", key, buf, g_iniPath);
    Log("hotkey: %s -> %s", key, buf);
}

DWORD WINAPI Worker(LPVOID) {
    bool baseKeyDown = false, dlgKeyDown = false;
    double tscPerMs = CalibrateTscPerMs();
    if (g_fs && tscPerMs > 0) InterlockedExchange64(&g_fs->threshold, static_cast<LONG64>(40.0 * tscPerMs));
    Log("TSC %.0f ticks/ms; slow frame = longer than 40 ms", tscPerMs);
    LONG64 lastSlow = 0;
    Config cfg;
    ReadConfig(cfg);
    FILETIME lastIni{};
    LONG64 lastHits = 0, logHits = 0, logFrames = 0;
    ULONGLONG lastHitTime = 0, lastLog = 0, lastIniCheck = 0;
    bool inDialogue = false, applied = false;
    double appliedScale = 1.0;
    uint32_t appliedIndex = 0;
    int32_t orig[4]{}, wrote[4]{};

    for (;;) {
        Sleep(16);
        ULONGLONG now = GetTickCount64();

        if (now - lastIniCheck >= 1000) {
            lastIniCheck = now;
            WIN32_FILE_ATTRIBUTE_DATA fa;
            if (GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &fa) &&
                CompareFileTime(&fa.ftLastWriteTime, &lastIni) != 0) {
                lastIni = fa.ftLastWriteTime;
                ReadConfig(cfg);
                Log("config: Mode=%d Scale=%.2f BaseScale=%.2f Fields=%d ExitDelayMs=%d LogPeriodMs=%d DialogueLodBias=%.2f",
                    cfg.mode, cfg.scale, cfg.baseScale, cfg.fields, cfg.exitDelayMs, cfg.logPeriodMs, cfg.lodBias);
            }
        }

        if (KeyPressed(cfg.keyBase, baseKeyDown)) {
            cfg.baseScale = NextStep(cfg.baseScale);
            SaveScale("BaseScale", cfg.baseScale);
        }
        if (KeyPressed(cfg.keyDialogue, dlgKeyDown)) {
            cfg.scale = NextStep(cfg.scale);
            SaveScale("Scale", cfg.scale);
        }

        LONG64 hits = g_camHits ? *g_camHits : 0;
        if (hits != lastHits) lastHitTime = now;
        lastHits = hits;
        bool nowDialogue = lastHitTime && now - lastHitTime < static_cast<ULONGLONG>(cfg.exitDelayMs);
        SetLodFactor(nowDialogue && cfg.mode != 0 ? static_cast<float>(cfg.lodBias) : 1.0f);
        if (nowDialogue != inDialogue) {
            inDialogue = nowDialogue;
            Log("dialogue camera %s (hits total %lld)", inDialogue ? "ON" : "OFF", hits);
        }

        uint32_t index = *reinterpret_cast<uint32_t*>(g_base + kPresetIndex);
        if (index >= 6) continue;
        int32_t* row = Row(index);
        // Mode 1: Scale in dialogues, BaseScale elsewhere. Mode 2: Scale always. Mode 0: native.
        double target = cfg.mode == 0 ? 1.0 : (cfg.mode == 2 || inDialogue) ? cfg.scale : cfg.baseScale;
        bool want = target < 0.999;

        if (applied && (index != appliedIndex || memcmp(row + 9, wrote, sizeof(wrote)) != 0)) {
            // The game rewrote the row (options changed / preset switched): take its values as new originals.
            Log("row changed by the game: %d %d %d %d -> new originals", row[9], row[10], row[11], row[12]);
            int32_t* old = Row(appliedIndex);
            if (index != appliedIndex && memcmp(old + 9, wrote, sizeof(wrote)) == 0)
                memcpy(old + 9, orig, sizeof(orig));  // leave the preset we left untouched
            applied = false;
        }
        if (want && (!applied || target != appliedScale) && row[9] > 0 && row[10] > 0) {  // skip unfilled rows
            if (!applied) memcpy(orig, row + 9, sizeof(orig));
            memcpy(wrote, orig, sizeof(wrote));
            for (int i = 0; i < 4; i++) {
                bool on = (i < 2) ? (cfg.fields & 1) : (cfg.fields & 2);
                if (on) wrote[i] = static_cast<int32_t>(orig[i] * target) & ~1;
            }
            appliedScale = target;
            StoreSizes(row + 9, wrote);
            applied = true;
            appliedIndex = index;
            Log("apply: preset %u  %dx%d / %dx%d -> %dx%d / %dx%d", index, orig[0], orig[1], orig[2], orig[3],
                wrote[0], wrote[1], wrote[2], wrote[3]);
        } else if (!want && applied) {
            StoreSizes(row + 9, orig);
            applied = false;
            Log("restore: preset %u  %dx%d / %dx%d", index, orig[0], orig[1], orig[2], orig[3]);
        }

        if (cfg.logPeriodMs > 0 && now - lastLog >= static_cast<ULONGLONG>(cfg.logPeriodMs)) {
            // The dialogue camera updates once per rendered frame, so hits per second = dialogue FPS.
            double dlgFps = lastLog ? (hits - logHits) * 1000.0 / static_cast<double>(now - lastLog) : 0.0;
            LONG64 frames = g_frames ? *g_frames : 0;
            double fps = lastLog ? (frames - logFrames) * 1000.0 / static_cast<double>(now - lastLog) : 0.0;
            logHits = hits;
            logFrames = frames;
            double maxMs = 0;
            LONG64 slow = 0;
            if (g_fs && tscPerMs > 0) {
                maxMs = InterlockedExchange64(&g_fs->maxDelta, 0) / tscPerMs;
                slow = g_fs->slow - lastSlow;
                lastSlow = g_fs->slow;
            }
            lastLog = now;
            auto* g = reinterpret_cast<int32_t*>(g_base + kRenderSize);
            Log("state: preset=%u row=%d,%d,%d,%d globals=%d,%d,%d,%d fps=%.1f maxFrameMs=%.1f slow40=%lld dialogueFps=%.1f dialogue=%d applied=%d",
                index, row[9], row[10], row[11], row[12], g[0], g[1], g[2], g[3], fps, maxMs, slow, dlgFps,
                inDialogue, applied);
        }
    }
}

void Init(HMODULE self) {
    char path[MAX_PATH];
    GetModuleFileNameA(self, path, MAX_PATH);
    char* dot = strrchr(path, '.');
    if (dot) *dot = 0;
    snprintf(g_iniPath, sizeof(g_iniPath), "%s.ini", path);
    snprintf(g_logPath, sizeof(g_logPath), "%s.log", path);
    g_log = fopen(g_logPath, "a");
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    Log("RelinkDeckTuner 0.11 (GBFR v2.0.5), base %p", reinterpret_cast<void*>(g_base));

    if (!BytesMatch(kSizeCopy, kSizeCopyBytes, sizeof(kSizeCopyBytes))) {
        Log("render-size function not found at expected address: wrong game version, disabled");
        return;
    }
    if (!BytesMatch(kCamHook, kCamHookBytes, sizeof(kCamHookBytes))) {
        Log("camera hook site differs (game version or another mod patched it): disabled");
        return;
    }
    g_camHits = InstallCounterHook(g_base + kCamHook, kCamHookBytes, sizeof(kCamHookBytes));
    if (!g_camHits) {
        Log("could not allocate trampoline: disabled");
        return;
    }
    Log("camera hook installed at +%zx", kCamHook);
    g_fs = InstallFrameStatsHook(g_base + kSizeCopy, kSizeCopyBytes, 6);  // push r15/r14/r13
    if (g_fs) g_frames = &g_fs->frames;
    if (BytesMatch(kLodFactorTable, reinterpret_cast<const uint8_t*>(kLodFactorDefault), sizeof(kLodFactorDefault))) {
        DWORD old;
        auto* t = reinterpret_cast<float*>(g_base + kLodFactorTable);
        if (VirtualProtect(t, sizeof(kLodFactorDefault), PAGE_READWRITE, &old)) g_lodFactor = t + 1;
    }
    Log(g_lodFactor ? "scenery LOD factor table found" : "scenery LOD factor table not found: DialogueLodBias off");
    Log(g_frames ? "frame counter installed at +%zx" : "frame counter not installed (+%zx)", kSizeCopy);
    g_upscaler = GetPrivateProfileIntA("RelinkDeckTuner", "Upscaler", 1, g_iniPath);
    if (g_upscaler == 1 || g_upscaler == 2) {
        if (HookImport("d3d11.dll", "D3D11CreateDevice", reinterpret_cast<void*>(&HookedCreateDevice),
                       reinterpret_cast<void**>(&g_realCreateDevice)))
            Log("D3D11CreateDevice hooked (upscaler replacement on)");
        else
            Log("D3D11CreateDevice import not found: upscaler replacement off");
    }
    CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        Init(module);
    }
    return TRUE;
}
