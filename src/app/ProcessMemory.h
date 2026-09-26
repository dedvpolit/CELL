#pragma once

#include <cstddef>
#include <cstdio>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

// Process RAM for the [perf] log. PrivateUsage is read to match Task Manager's Private Working Set;
// WorkingSetSize also counts pages shared with other processes (DLLs, GPU driver) and reads much
// higher.
inline size_t GetProcessWorkingSetBytes()
{
#ifdef _WIN32
    using GetProcessMemoryInfoFn = BOOL (WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS_EX, DWORD);

    static GetProcessMemoryInfoFn s_fn = nullptr;
    static bool s_resolved = false;

    if (!s_resolved)
    {
        s_resolved = true;

        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (kernel32)
            s_fn = reinterpret_cast<GetProcessMemoryInfoFn>(
                GetProcAddress(kernel32, "K32GetProcessMemoryInfo"));

        if (!s_fn)
        {
            HMODULE psapi = LoadLibraryW(L"psapi.dll");
            if (psapi)
                s_fn = reinterpret_cast<GetProcessMemoryInfoFn>(
                    GetProcAddress(psapi, "GetProcessMemoryInfo"));
        }
    }

    if (!s_fn)
        return 0;

    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!s_fn(GetCurrentProcess(), &counters, sizeof(counters)))
        return 0;

    return (size_t)counters.PrivateUsage;
#else
    // Non-Windows builds: VmRSS from /proc/self/status is the equivalent of the working set on
    // Windows.
    std::FILE* f = std::fopen("/proc/self/status", "r");
    if (!f)
        return 0;

    size_t rssKb = 0;
    bool found = false;
    char line[256];
    while (std::fgets(line, sizeof(line), f))
    {
        if (std::sscanf(line, "VmRSS: %zu kB", &rssKb) == 1)
        {
            found = true;
            break;
        }
    }
    std::fclose(f);
    return found ? rssKb * 1024 : 0;
#endif
}
