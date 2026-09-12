#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>

#include "AudioMixer.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Footstep playback layer — теперь поверх общего AudioMixer (см.
// AudioMixer.h), а не отдельного PlaySoundW-канала.
//
// БАГФИКС ("шаги слышны только у одного противника") — раньше и этот
// класс, и EnemyAudio.h (по одному на каждого из kEnemyCount=7 врагов,
// см. DungeonScene.h) независимо дёргали PlaySoundW — а это ОДИН
// системный канал на весь процесс, так что шаги игрока и звуки всех 7
// врагов дрались за одно и то же место и глушили друг друга. Теперь оба
// класса используют общий пул голосов AudioMixer — играют одновременно,
// по-настоящему, без взаимного заглушения.
//
// Громкость собственных шагов игрока НЕ зависит от дистанции (игрок
// слышит себя всегда одинаково — он и есть слушатель, в отличие от
// EnemyAudio, где применяется динамическая громкость по расстоянию до
// врага, см. EnemyAI.cpp::DistanceVolume()) — играются с volume=1.0.
class FootstepAudio {
public:
    bool init()
    {
#ifdef _WIN32
        if (m_available)
            return true;

        // Verify the content pack exists beside the executable (or in the
        // current working tree). Missing audio never breaks the game.
        if (locateAsset(L"footstep_walk_1.wav").empty() ||
            locateAsset(L"footstep_run_1.wav").empty()) {
            return false;
        }

        if (!AudioMixer::instance().init())
            return false;

        m_available = true;
        return true;
#else
        m_available = false;
        return false;
#endif
    }

    // Единственное место, которое реально останавливает общий AudioMixer
    // (см. большой комментарий у EnemyAudio::shutdown() — она сознательно
    // НЕ трогает микшер, т.к. общий на 7 врагов). Player — один, и его
    // shutdown() вызывается ровно один раз при закрытии сцены (см.
    // DungeonScene::shutdown() -> PlayerController::shutdown()), когда
    // звук уже не нужен ни игроку, ни врагам.
    void shutdown()
    {
        AudioMixer::instance().shutdown();
        m_available = false;
        m_walkVariant = 0;
        m_runVariant = 0;
    }

    void playWalk()
    {
#ifdef _WIN32
        const int variant = m_walkVariant++ & 1;
        AudioMixer::instance().play(locateAsset(
            variant == 0
                ? L"footstep_walk_1.wav"
                : L"footstep_walk_2.wav"
        ), 1.0f, kPriority);
#endif
    }

    void playRun()
    {
#ifdef _WIN32
        const int variant = m_runVariant++ & 1;
        AudioMixer::instance().play(locateAsset(
            variant == 0
                ? L"footstep_run_1.wav"
                : L"footstep_run_2.wav"
        ), 1.0f, kPriority);
#endif
    }

    bool isAvailable() const { return m_available; }

private:
    // Приоритет для AudioMixer (см. EnemyAudio.h для остальной шкалы:
    // 0 — шаги врага, 1 — здесь и стон врага, 2 — крик/удар о стену) —
    // шаги игрока чуть важнее шагов врага (обратная связь самому игроку
    // о собственном движении), но не так критичны, как разовые события.
    static constexpr int kPriority = 1;

#ifdef _WIN32
    // ОПТИМИЗАЦИЯ (тот же приём, что и в EnemyAudio.h::locateAsset() —
    // см. большой комментарий там) — раньше полный обход файловой
    // системы повторялся на КАЖДЫЙ шаг игрока; имя файла всегда
    // разрешается в один и тот же путь за сессию, поэтому кэшируем.
    std::wstring locateAsset(const wchar_t* name) const
    {
        static std::unordered_map<std::wstring, std::wstring> s_pathCache;

        const auto cached = s_pathCache.find(name);
        if (cached != s_pathCache.end())
            return cached->second;

        std::vector<std::filesystem::path> startPoints;

        wchar_t modulePath[32768]{};
        const DWORD len = GetModuleFileNameW(
            nullptr,
            modulePath,
            static_cast<DWORD>(std::size(modulePath))
        );

        if (len > 0 && len < std::size(modulePath))
            startPoints.push_back(std::filesystem::path(modulePath).parent_path());

        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!ec)
            startPoints.push_back(cwd);

        // The executable typically lives a few directories below the
        // project/assets root (e.g. bin/Release/app.exe next to a
        // top-level assets/ folder), and that depth can vary by build
        // configuration. Walk upward from each start point instead of
        // assuming a fixed number of levels, so the pack is found
        // regardless of whether we're running from bin/Debug,
        // bin/Release, or the project root itself.
        constexpr int kMaxLevelsUp = 6;

        std::wstring result;
        for (const auto& start : startPoints) {
            std::filesystem::path dir = start;
            for (int level = 0; level <= kMaxLevelsUp; ++level) {
                const auto candidate = dir / L"assets" / L"audio" / L"footsteps" / name;
                if (std::filesystem::exists(candidate, ec) && !ec) {
                    result = candidate.wstring();
                    break;
                }

                const auto parent = dir.parent_path();
                if (parent == dir)
                    break; // reached filesystem root
                dir = parent;
            }
            if (!result.empty())
                break;
        }

        s_pathCache.emplace(name, result);
        return result;
    }

    bool m_available = false;
    int m_walkVariant = 0;
    int m_runVariant = 0;
#else
    std::wstring locateAsset(const wchar_t*) const { return {}; }
    bool m_available = false;
    int m_walkVariant = 0;
    int m_runVariant = 0;
#endif
};
