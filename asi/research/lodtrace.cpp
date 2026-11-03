// Research-only ASI (PC): find who reads a model's lod_screen_size_thresholds at runtime.
// After DelaySec seconds it scans committed private memory for the float sequence
// {1.0, 0.6, 0.24, 0.2} (bg012c's thresholds), logs every match, then puts hardware read/write
// watchpoints (DR0..DR3) on the 2nd float of the first matches in every thread and logs the RIP of
// each hit (first 64 distinct RIPs, with counts) to lodtrace.log for 20 seconds, then clears them.
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

static FILE* g_log;
static volatile LONG g_active;
static ULONG_PTR g_rips[64];
static volatile LONG g_counts[64];
static uintptr_t g_base;

static LONG CALLBACK Vectored(EXCEPTION_POINTERS* e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !g_active) return EXCEPTION_CONTINUE_SEARCH;
    if (!(e->ContextRecord->Dr6 & 0xf)) return EXCEPTION_CONTINUE_SEARCH;
    ULONG_PTR rip = e->ContextRecord->Rip;
    for (int i = 0; i < 64; i++) {
        ULONG_PTR cur = g_rips[i];
        if (cur == rip) { InterlockedIncrement(&g_counts[i]); break; }
        if (cur == 0 && InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&g_rips[i]),
                                                          reinterpret_cast<void*>(rip), nullptr) == nullptr) {
            InterlockedIncrement(&g_counts[i]); break;
        }
    }
    e->ContextRecord->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void SetWatch(const uintptr_t* addrs, int n) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = {sizeof(te)};
    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!t) continue;
        SuspendThread(t);
        CONTEXT c = {}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(t, &c)) {
            c.Dr0 = n > 0 ? addrs[0] : 0; c.Dr1 = n > 1 ? addrs[1] : 0;
            c.Dr2 = n > 2 ? addrs[2] : 0; c.Dr3 = n > 3 ? addrs[3] : 0;
            DWORD64 dr7 = 0;
            for (int i = 0; i < n && i < 4; i++) dr7 |= (1ull << (i * 2)) | (3ull << (16 + i * 4)) | (3ull << (18 + i * 4));  // RW, 4 bytes
            c.Dr7 = dr7;
            SetThreadContext(t, &c);
        }
        ResumeThread(t);
        CloseHandle(t);
    }
    CloseHandle(snap);
}

// Pages can change protection or be freed while we scan: a fault just ends this region.
// Candidate encodings of bg012c's LOD1..3 thresholds {0.6, 0.24, 0.2}: as stored, squared,
// and as stored but with stride 8 (interleaved with another float).
static const float kPats[3][3] = {{0.6f, 0.24f, 0.2f}, {0.36f, 0.0576f, 0.04f}, {0.6f, 0.24f, 0.2f}};
static const int kStride[3] = {1, 1, 2};
static uintptr_t g_hits[3][16];
static int g_nhits[3];
static void ScanRegion(const uint32_t* w, SIZE_T n) {
    __try {
        for (int k = 0; k < 3; k++) {
            uint32_t a, b, c; memcpy(&a, &kPats[k][0], 4); memcpy(&b, &kPats[k][1], 4); memcpy(&c, &kPats[k][2], 4);
            int st = kStride[k];
            for (SIZE_T i = 0; i + 2 * st < n; i++)
                if (w[i] == a && w[i + st] == b && w[i + 2 * st] == c && g_nhits[k] < 16)
                    g_hits[k][g_nhits[k]++] = reinterpret_cast<uintptr_t>(w + i);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static DWORD WINAPI Worker(LPVOID) {
    char ini[MAX_PATH]; GetModuleFileNameA(nullptr, ini, MAX_PATH);
    int delay = 75;
    Sleep(delay * 1000);
    fprintf(g_log, "scanning\n"); fflush(g_log);
    DWORD t0 = GetTickCount();
    unsigned long long scanned = 0, lastReport = 0;
    MEMORY_BASIC_INFORMATION mbi;
    for (uint8_t* p = nullptr; VirtualQuery(p, &mbi, sizeof(mbi)); p = static_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS | PAGE_NOCACHE | PAGE_WRITECOMBINE)) || mbi.Type != MEM_PRIVATE ||
            !(mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY))) continue;
        ScanRegion(static_cast<const uint32_t*>(mbi.BaseAddress), mbi.RegionSize / 4);
        scanned += mbi.RegionSize;
        if (scanned - lastReport > (1ull << 30)) {
            lastReport = scanned;
            fprintf(g_log, "  %llu MB scanned, %lu ms\n", scanned >> 20, GetTickCount() - t0);
            fflush(g_log);
        }
    }
    uintptr_t w[4]; int nw = 0;
    for (int k = 0; k < 3; k++) {
        fprintf(g_log, "pattern %d: %d matches\n", k, g_nhits[k]);
        for (int i = 0; i < g_nhits[k]; i++) {
            float prev = *reinterpret_cast<const float*>(g_hits[k][i] - 4 * kStride[k]);
            fprintf(g_log, "  %p prev=%g\n", (void*)g_hits[k][i], prev);
            if ((k > 0 || prev != 1.0f) && nw < 4) w[nw++] = g_hits[k][i];  // not a raw minfo copy
        }
    }
    fflush(g_log);
    if (!nw) { fprintf(g_log, "no derived copies found\n"); fflush(g_log); return 0; }
    AddVectoredExceptionHandler(1, Vectored);
    g_active = 1;
    SetWatch(w, nw);
    fprintf(g_log, "watching %d addresses for 20 s\n", nw); fflush(g_log);
    Sleep(20000);
    SetWatch(w, 0);
    g_active = 0;
    for (int i = 0; i < 64 && g_rips[i]; i++)
        fprintf(g_log, "rip %p (exe+%llx) hits %ld\n", (void*)g_rips[i],
                (unsigned long long)(g_rips[i] - g_base), g_counts[i]);
    fprintf(g_log, "done\n"); fflush(g_log);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE m, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        char path[MAX_PATH]; GetModuleFileNameA(m, path, MAX_PATH);
        char* d = strrchr(path, '.'); if (d) strcpy(d, ".log");
        g_log = fopen(path, "w");
        g_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    }
    return TRUE;
}
