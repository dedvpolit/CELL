#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Finds assets/audio/<subfolder>/<name> by walking up from the executable and the working directory
// Empty if not found
inline std::wstring LocateAudioAsset(const wchar_t* subfolder, const wchar_t* name)
{
#ifdef _WIN32
    static std::unordered_map<std::wstring, std::wstring> s_pathCache;

    const std::wstring cacheKey = std::wstring(subfolder) + L"/" + name;
    const auto cached = s_pathCache.find(cacheKey);
    if (cached != s_pathCache.end())
        // an empty string is also a valid cached result ("file not found"): do not search again
        return cached->second;

    std::vector<std::filesystem::path> startPoints;

    wchar_t modulePath[32768]{};
    const DWORD len = GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (len > 0 && len < std::size(modulePath))
        startPoints.push_back(std::filesystem::path(modulePath).parent_path());

    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!ec)
        startPoints.push_back(cwd);

    constexpr int kMaxLevelsUp = 6;
    std::wstring result;
    for (const auto& start : startPoints) {
        std::filesystem::path dir = start;
        for (int level = 0; level <= kMaxLevelsUp; ++level) {
            const auto candidate = dir / L"assets" / L"audio" / subfolder / name;
            if (std::filesystem::exists(candidate, ec) && !ec) {
                result = candidate.wstring();
                break;
            }
            const auto parent = dir.parent_path();
            if (parent == dir)
                break;
            dir = parent;
        }
        if (!result.empty())
            break;
    }

    s_pathCache.emplace(cacheKey, result);
    return result;
#else
    (void)subfolder;
    (void)name;
    return {};
#endif
}
