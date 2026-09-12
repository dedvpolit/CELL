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

// ============================================================================
// GetProcessWorkingSetBytes() — сколько ОЗУ реально занимает процесс
// прямо сейчас, для консольного perf-лога рядом с FPS (см. Application::
// tick() — "[perf] fps = ... | ram=...").
//
// БАГФИКС ("консоль показывает 150-170 МБ, а Диспетчер задач — 80-92 МБ
// для того же самого запущенного процесса одновременно") — это не гонка
// данных и не два разных момента времени, а два РАЗНЫХ по смыслу числа:
// раньше здесь читался PROCESS_MEMORY_COUNTERS::WorkingSetSize — ПОЛНЫЙ
// рабочий набор процесса, включая страницы, которые физически
// присутствуют в ОЗУ, но РАЗДЕЛЯЮТСЯ с другими процессами (загруженные
// системные DLL, компоненты видеодрайвера, отображённые в адресное
// пространство через тот же GPU-контекст, который использует и Проводник,
// и другие приложения). Диспетчер задач же в столбце "Память" по
// умолчанию (начиная с Vista) показывает Private Working Set — ТОЛЬКО
// страницы, которые принадлежат ИСКЛЮЧИТЕЛЬНО этому процессу и ни с кем
// не общие. Оба числа верны — это просто разные метрики одного и того же
// процесса в один и тот же момент; PrivateUsage (см. ниже) — тот же смысл,
// что и "Память (закрытый рабочий набор)"/"Private Working Set" в
// Диспетчере задач, поэтому теперь читаем именно его.
// ============================================================================
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

    // PrivateUsage — то же самое число, что Диспетчер задач показывает
    // как "Память (закрытый рабочий набор)" (см. большой комментарий
    // выше) — НЕ WorkingSetSize (общий рабочий набор, раздутый общими
    // страницами видеодрайвера и системных DLL).
    return (size_t)counters.PrivateUsage;
#else
    // Не-Windows (сборка/отладка движка на этой платформе локально, см.
    // остальные dev-инструменты) — VmRSS из /proc/self/status несёт тот
    // же смысл, что Working Set в Windows.
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
