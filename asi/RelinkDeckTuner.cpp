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

#include "upscale_shaders.h"

namespace {

constexpr uintptr_t kPresetTable = 0x6b82460;  // 6 rows x 0x4C
constexpr uintptr_t kPresetIndex = 0x7034940;  // u32, 5 = Custom
constexpr uintptr_t kRenderSize = 0x6b822e0;   // 4 x i32 written from row +0x24..+0x30
constexpr uintptr_t kSizeCopy = 0x6bbaa0;      // function that copies them
constexpr uintptr_t kCamHook = 0x942698;       // mov rax,[rsi+60h]; vmovss xmm2,[rax+9D4h]
constexpr uintptr_t kCamResume = 0x9426a4;
constexpr int kRowSize = 0x4c;

const uint8_t kSizeCopyBytes[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53,
                                  0x48, 0x83, 0xec, 0x38, 0x8b, 0x05, 0x8a, 0x8e, 0x97, 0x06};
const uint8_t kCamHookBytes[] = {0x48, 0x8b, 0x46, 0x60, 0xc5, 0xfa, 0x10, 0x90, 0xd4, 0x09, 0x00, 0x00};

struct Config {
    int mode = 1;           // 0 = off, 1 = only in dialogue, 2 = always (test)
    double scale = 0.75;    // per axis
    int fields = 3;         // bit 1: row +0x24/+0x28, bit 2: row +0x2c/+0x30
    int exitDelayMs = 700;  // dialogue considered over after this long without camera hits
    int logPeriodMs = 2000; // periodic snapshot; 0 = only on changes
};

uintptr_t g_base;
volatile LONG64* g_camHits;
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
    void** vtbl = *static_cast<void***>(device);
    if (vtbl[kCreatePixelShaderSlot] == reinterpret_cast<void*>(&HookedCreatePixelShader)) return;
    DWORD old;
    if (!VirtualProtect(&vtbl[kCreatePixelShaderSlot], sizeof(void*), PAGE_READWRITE, &old)) return;
    g_realCreatePixelShader = reinterpret_cast<CreatePixelShaderFn>(vtbl[kCreatePixelShaderSlot]);
    vtbl[kCreatePixelShaderSlot] = reinterpret_cast<void*>(&HookedCreatePixelShader);
    VirtualProtect(&vtbl[kCreatePixelShaderSlot], sizeof(void*), old, &old);
    Log("ID3D11Device::CreatePixelShader hooked");
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
            void* p = VirtualAlloc(reinterpret_cast<void*>(addr), size, MEM_COMMIT | MEM_RESERVE,
                                   PAGE_EXECUTE_READWRITE);
            if (p) return p;
        }
    }
    return nullptr;
}

bool InstallCamHook() {
    uintptr_t site = g_base + kCamHook;
    auto* tramp = static_cast<uint8_t*>(AllocNear(site, 0x1000));
    if (!tramp) return false;
    uint8_t* p = tramp;
    // lock inc qword ptr [rip + off] -> counter stored at tramp+0x100
    g_camHits = reinterpret_cast<volatile LONG64*>(tramp + 0x100);
    *p++ = 0xf0; *p++ = 0x48; *p++ = 0xff; *p++ = 0x05;
    int32_t rel = static_cast<int32_t>((tramp + 0x100) - (p + 4));
    memcpy(p, &rel, 4); p += 4;
    memcpy(p, kCamHookBytes, sizeof(kCamHookBytes)); p += sizeof(kCamHookBytes);  // original code
    *p++ = 0xff; *p++ = 0x25; *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 0;              // jmp [rip+0]
    uintptr_t resume = g_base + kCamResume;
    memcpy(p, &resume, 8);

    uint8_t patch[sizeof(kCamHookBytes)];
    memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xe9;
    rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(tramp) - (site + 5));
    memcpy(patch + 1, &rel, 4);
    DWORD old;
    VirtualProtect(reinterpret_cast<void*>(site), sizeof(patch), PAGE_EXECUTE_READWRITE, &old);
    memcpy(reinterpret_cast<void*>(site), patch, sizeof(patch));
    VirtualProtect(reinterpret_cast<void*>(site), sizeof(patch), old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), sizeof(patch));
    return true;
}

void ReadConfig(Config& c) {
    char buf[64];
    c.mode = GetPrivateProfileIntA("RelinkDeckTuner", "Mode", 1, g_iniPath);
    GetPrivateProfileStringA("RelinkDeckTuner", "Scale", "0.75", buf, sizeof(buf), g_iniPath);
    c.scale = atof(buf);
    if (c.scale < 0.4) c.scale = 0.4;
    if (c.scale > 1.0) c.scale = 1.0;
    c.fields = GetPrivateProfileIntA("RelinkDeckTuner", "Fields", 3, g_iniPath);
    c.exitDelayMs = GetPrivateProfileIntA("RelinkDeckTuner", "ExitDelayMs", 700, g_iniPath);
    c.logPeriodMs = GetPrivateProfileIntA("RelinkDeckTuner", "LogPeriodMs", 2000, g_iniPath);
}

int32_t* Row(uint32_t index) {
    return reinterpret_cast<int32_t*>(g_base + kPresetTable + index * kRowSize);
}

DWORD WINAPI Worker(LPVOID) {
    Config cfg;
    ReadConfig(cfg);
    FILETIME lastIni{};
    LONG64 lastHits = 0, logHits = 0;
    ULONGLONG lastHitTime = 0, lastLog = 0, lastIniCheck = 0;
    bool inDialogue = false, applied = false;
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
                Log("config: Mode=%d Scale=%.2f Fields=%d ExitDelayMs=%d LogPeriodMs=%d", cfg.mode, cfg.scale,
                    cfg.fields, cfg.exitDelayMs, cfg.logPeriodMs);
            }
        }

        LONG64 hits = g_camHits ? *g_camHits : 0;
        if (hits != lastHits) lastHitTime = now;
        lastHits = hits;
        bool nowDialogue = lastHitTime && now - lastHitTime < static_cast<ULONGLONG>(cfg.exitDelayMs);
        if (nowDialogue != inDialogue) {
            inDialogue = nowDialogue;
            Log("dialogue camera %s (hits total %lld)", inDialogue ? "ON" : "OFF", hits);
        }

        uint32_t index = *reinterpret_cast<uint32_t*>(g_base + kPresetIndex);
        if (index >= 6) continue;
        int32_t* row = Row(index);
        bool want = cfg.mode == 2 || (cfg.mode == 1 && inDialogue);

        if (applied && (index != appliedIndex || memcmp(row + 9, wrote, sizeof(wrote)) != 0)) {
            // The game rewrote the row (options changed / preset switched): take its values as new originals.
            Log("row changed by the game: %d %d %d %d -> new originals", row[9], row[10], row[11], row[12]);
            int32_t* old = Row(appliedIndex);
            if (index != appliedIndex && memcmp(old + 9, wrote, sizeof(wrote)) == 0)
                memcpy(old + 9, orig, sizeof(orig));  // leave the preset we left untouched
            applied = false;
        }
        if (want && !applied) {
            memcpy(orig, row + 9, sizeof(orig));
            memcpy(wrote, orig, sizeof(wrote));
            for (int i = 0; i < 4; i++) {
                bool on = (i < 2) ? (cfg.fields & 1) : (cfg.fields & 2);
                if (on) wrote[i] = static_cast<int32_t>(orig[i] * cfg.scale) & ~1;
            }
            memcpy(row + 9, wrote, sizeof(wrote));
            applied = true;
            appliedIndex = index;
            Log("apply: preset %u  %dx%d / %dx%d -> %dx%d / %dx%d", index, orig[0], orig[1], orig[2], orig[3],
                wrote[0], wrote[1], wrote[2], wrote[3]);
        } else if (!want && applied) {
            memcpy(row + 9, orig, sizeof(orig));
            applied = false;
            Log("restore: preset %u  %dx%d / %dx%d", index, orig[0], orig[1], orig[2], orig[3]);
        }

        if (cfg.logPeriodMs > 0 && now - lastLog >= static_cast<ULONGLONG>(cfg.logPeriodMs)) {
            // The dialogue camera updates once per rendered frame, so hits per second = dialogue FPS.
            double dlgFps = lastLog ? (hits - logHits) * 1000.0 / static_cast<double>(now - lastLog) : 0.0;
            logHits = hits;
            lastLog = now;
            auto* g = reinterpret_cast<int32_t*>(g_base + kRenderSize);
            Log("state: preset=%u row=%d,%d,%d,%d globals=%d,%d,%d,%d dialogueFps=%.1f dialogue=%d applied=%d",
                index, row[9], row[10], row[11], row[12], g[0], g[1], g[2], g[3], dlgFps, inDialogue, applied);
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
    Log("RelinkDeckTuner 0.5 (GBFR v2.0.5), base %p", reinterpret_cast<void*>(g_base));

    if (!BytesMatch(kSizeCopy, kSizeCopyBytes, sizeof(kSizeCopyBytes))) {
        Log("render-size function not found at expected address: wrong game version, disabled");
        return;
    }
    if (!BytesMatch(kCamHook, kCamHookBytes, sizeof(kCamHookBytes))) {
        Log("camera hook site differs (game version or another mod patched it): disabled");
        return;
    }
    if (!InstallCamHook()) {
        Log("could not allocate trampoline: disabled");
        return;
    }
    Log("camera hook installed at +%zx", kCamHook);
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
