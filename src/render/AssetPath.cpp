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
    wchar_t modulePath[32768]{};
    const DWORD len = GetModuleFileNameW(
        nullptr,
        modulePath,
        static_cast<DWORD>(std::size(modulePath))
    );
    if (len > 0 && len < std::size(modulePath))
        startPoints.push_back(fs::path(modulePath).parent_path());
#endif

    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec)
        startPoints.push_back(cwd);

    // The exe usually sits a few levels below the project root (bin/Debug/app.exe next to a
    // top-level assets/) and the depth varies by build configuration, so it walks upward instead of
    // assuming a fixed number of levels.
    constexpr int kMaxLevelsUp = 6;

    for (const fs::path& start : startPoints) {
        fs::path dir = start;
        for (int level = 0; level <= kMaxLevelsUp; ++level) {
            const fs::path candidate = dir / relativePath;
            if (fs::exists(candidate, ec) && !ec)
                return candidate.string();

            const fs::path parent = dir.parent_path();
            if (parent == dir)
                break; // reached filesystem root
            dir = parent;
        }
    }

    return {};
}

} // namespace AssetPath
