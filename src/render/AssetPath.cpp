#include "AssetPath.h"
#include <filesystem>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace AssetPath {

std::string Resolve(const std::string& relativePath)
{
    namespace fs = std::filesystem;

    std::vector<fs::path> startPoints;

#ifdef _WIN32
    // Папка самого exe — приоритетная точка поиска (совпадает с тем,
    // куда .cbp реально копирует assets/, см. ExtraCommands в .cbp).
    wchar_t modulePath[32768]{};
    const DWORD len = GetModuleFileNameW(
        nullptr,
        modulePath,
        static_cast<DWORD>(std::size(modulePath))
    );
    if (len > 0 && len < std::size(modulePath))
        startPoints.push_back(fs::path(modulePath).parent_path());
#endif

    // Текущая рабочая директория — запасной вариант (например, запуск
    // из-под IDE, где working dir настроен на корень проекта).
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec)
        startPoints.push_back(cwd);

    // Экзешник обычно лежит на несколько уровней ниже корня проекта
    // (bin/Debug/app.exe рядом с assets/ в корне) — глубина может
    // отличаться в зависимости от конфигурации сборки, поэтому
    // поднимаемся вверх, а не полагаемся на фиксированное число уровней.
    constexpr int kMaxLevelsUp = 6;

    for (const fs::path& start : startPoints) {
        fs::path dir = start;
        for (int level = 0; level <= kMaxLevelsUp; ++level) {
            const fs::path candidate = dir / relativePath;
            if (fs::exists(candidate, ec) && !ec)
                return candidate.string();

            const fs::path parent = dir.parent_path();
            if (parent == dir)
                break; // дошли до корня файловой системы
            dir = parent;
        }
    }

    return {};
}

} // namespace AssetPath
