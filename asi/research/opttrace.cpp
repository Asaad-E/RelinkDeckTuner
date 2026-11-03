// Research-only ASI (PC): which option functions see a hidden graphics option (option D).
// Hooks the entry of four functions that take (settings*, value*, key):
//   0x21b060 menu write (generic map or setter), 0x2096b0 setter, 0x20d320 getter, 0x209520 getter 2.
// Writes (value, type byte) are logged on every call; reads are counted per key and logged once a
// second. Only the keys below are traced. Output: opttrace.log next to the .asi.
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <initializer_list>

namespace {

struct Site { uintptr_t rva; uint8_t orig[8]; size_t len; const char* name; bool write; };
Site kSites[] = {
    {0x21b060, {0x56, 0x48, 0x83, 0xec, 0x40, 0x48, 0x89, 0xd6}, 8, "menuWrite21b060", true},
    {0x2096b0, {0x55, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x38}, 8, "setter2096b0", true},
    {0x20d320, {0x55, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x58}, 8, "getter20d320", false},
    {0x209520, {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x20}, 7, "getter209520", false},
};
constexpr int kSiteCount = sizeof(kSites) / sizeof(kSites[0]);

struct Key { uint32_t key; const char* name; };
const Key kKeys[] = {
    {0xffd48260, "DECAL"}, {0xf1d8b677, "DOF"}, {0x4c1d6fce, "BG"}, {0x2f6f51d1, "SHADOW"},
    {0xfbce1e5b, "CHARA"}, {0x4c37cdf9, "EFFECT"}, {0x9a9f05f8, "RENDERING(control)"},
};
constexpr int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

FILE* g_log;
CRITICAL_SECTION g_cs;
uintptr_t g_base;
volatile LONG g_reads[kSiteCount][kKeyCount];

void Log(const char* fmt, ...) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    EnterCriticalSection(&g_cs);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_cs);
}

int KeyIndex(uint32_t key) {
    for (int i = 0; i < kKeyCount; i++)
        if (kKeys[i].key == key) return i;
    return -1;
}

// Called from the trampolines with the original arguments.
extern "C" void __fastcall OnCall(uint64_t site, uintptr_t settings, uintptr_t value, uint64_t key) {
    int k = KeyIndex(static_cast<uint32_t>(key));
    if (k < 0) return;
    if (kSites[site].write) {
        auto* v = reinterpret_cast<const uint8_t*>(value);
        Log("%s key=%s settings=%p value=%d type=%d thread=%lu", kSites[site].name, kKeys[k].name,
            reinterpret_cast<void*>(settings), *reinterpret_cast<const int32_t*>(v), v[0x20], GetCurrentThreadId());
    } else {
        InterlockedIncrement(&g_reads[site][k]);
    }
}

void* AllocNear(uintptr_t target, size_t size) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t gran = si.dwAllocationGranularity;
    for (uintptr_t delta = gran; delta < 0x70000000; delta += gran)
        for (int sign = -1; sign <= 1; sign += 2) {
            uintptr_t addr = ((target + sign * (intptr_t)delta) / gran) * gran;
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(addr), size, MEM_COMMIT | MEM_RESERVE,
                                       PAGE_EXECUTE_READWRITE))
                return p;
        }
    return nullptr;
}

bool Hook(int id) {
    const Site& s = kSites[id];
    uintptr_t site = g_base + s.rva;
    if (memcmp(reinterpret_cast<void*>(site), s.orig, s.len) != 0) return false;
    auto* tramp = static_cast<uint8_t*>(AllocNear(site, 0x100));
    if (!tramp) return false;
    uint8_t* p = tramp;
    auto bytes = [&](std::initializer_list<uint8_t> b) { for (uint8_t x : b) *p++ = x; };
    // Entry: rsp = 8 mod 16. 7 pushes -> 0 mod 16, then 0x20 shadow space.
    bytes({0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x50, 0x41, 0x52, 0x41, 0x53});  // push rcx,rdx,r8,r9,rax,r10,r11
    bytes({0x48, 0x83, 0xec, 0x20});                                          // sub rsp, 0x20
    bytes({0x4d, 0x89, 0xc1});                                                // mov r9, r8   (key)
    bytes({0x49, 0x89, 0xd0});                                                // mov r8, rdx  (value*)
    bytes({0x48, 0x89, 0xca});                                                // mov rdx, rcx (settings)
    bytes({0xb9}); int32_t idv = id; memcpy(p, &idv, 4); p += 4;              // mov ecx, id
    bytes({0x48, 0xb8}); uintptr_t fn = reinterpret_cast<uintptr_t>(&OnCall); memcpy(p, &fn, 8); p += 8;
    bytes({0xff, 0xd0});                                                      // call rax
    bytes({0x48, 0x83, 0xc4, 0x20});                                          // add rsp, 0x20
    bytes({0x41, 0x5b, 0x41, 0x5a, 0x58, 0x41, 0x59, 0x41, 0x58, 0x5a, 0x59});  // pop r11,r10,rax,r9,r8,rdx,rcx
    memcpy(p, s.orig, s.len); p += s.len;                                     // original prologue
    bytes({0xff, 0x25, 0, 0, 0, 0});                                          // jmp [rip+0]
    uintptr_t resume = site + s.len;
    memcpy(p, &resume, 8);
    FlushInstructionCache(GetCurrentProcess(), tramp, 0x100);

    intptr_t dist = reinterpret_cast<intptr_t>(tramp) - static_cast<intptr_t>(site + 5);
    if (dist < INT32_MIN || dist > INT32_MAX) return false;
    uint8_t patch[8];
    memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xe9;
    int32_t rel = static_cast<int32_t>(dist);
    memcpy(patch + 1, &rel, 4);
    DWORD old;
    VirtualProtect(reinterpret_cast<void*>(site), s.len, PAGE_EXECUTE_READWRITE, &old);
    memcpy(reinterpret_cast<void*>(site), patch, s.len);
    VirtualProtect(reinterpret_cast<void*>(site), s.len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), s.len);
    return true;
}

DWORD WINAPI Worker(LPVOID) {
    for (;;) {
        Sleep(1000);
        for (int s = 0; s < kSiteCount; s++)
            for (int k = 0; k < kKeyCount; k++) {
                LONG n = InterlockedExchange(&g_reads[s][k], 0);
                if (n) Log("%s key=%s reads=%ld", kSites[s].name, kKeys[k].name, n);
            }
    }
}

void Init(HMODULE self) {
    char path[MAX_PATH];
    GetModuleFileNameA(self, path, MAX_PATH);
    if (char* dot = strrchr(path, '.')) strcpy(dot, ".log");
    g_log = fopen(path, "a");
    InitializeCriticalSection(&g_cs);
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    Log("opttrace start, base %p", reinterpret_cast<void*>(g_base));
    for (int i = 0; i < kSiteCount; i++)
        Log("hook %s at +%zx: %s", kSites[i].name, kSites[i].rva, Hook(i) ? "ok" : "FAILED (bytes differ)");
    CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        Init(mod);
    }
    return TRUE;
}
