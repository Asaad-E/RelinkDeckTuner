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

static DWORD WINAPI Worker(LPVOID) {
    char ini[MAX_PATH]; GetModuleFileNameA(nullptr, ini, MAX_PATH);
    int delay = 90;
    Sleep(delay * 1000);
    const float pat[4] = {1.0f, 0.6f, 0.24f, 0.2f};
    uintptr_t found[64]; int nf = 0;
    MEMORY_BASIC_INFORMATION mbi;
    for (uint8_t* p = nullptr; VirtualQuery(p, &mbi, sizeof(mbi)); p = static_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY))) continue;
        auto* b = static_cast<uint8_t*>(mbi.BaseAddress);
        for (SIZE_T i = 0; i + 16 <= mbi.RegionSize; i += 4)
            if (memcmp(b + i, pat, 16) == 0 && nf < 64) found[nf++] = reinterpret_cast<uintptr_t>(b + i);
    }
    fprintf(g_log, "matches: %d\n", nf);
    for (int i = 0; i < nf; i++) {
        MEMORY_BASIC_INFORMATION m; VirtualQuery(reinterpret_cast<void*>(found[i]), &m, sizeof(m));
        fprintf(g_log, "  %p region %p type %lx\n", (void*)found[i], m.AllocationBase, m.Type);
    }
    fflush(g_log);
    if (!nf) return 0;
    uintptr_t w[4]; int nw = 0;
    for (int i = 0; i < nf && nw < 4; i++) w[nw++] = found[i] + 4;  // watch the 0.6 (LOD1 threshold)
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
