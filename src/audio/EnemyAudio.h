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

// ============================================================================
// EnemyAudio — звуки одного врага (крик обнаружения, стон, шаги, удар о
// стену).
//
// БАГФИКС ("шаги слышны только у одного противника, крик иногда вообще не
// проигрывается") — раньше этот класс сам вызывал PlaySoundW напрямую, а
// это ОДИН системный канал на весь процесс. С kEnemyCount=7 врагами (см.
// DungeonScene.h) это означало, что все 7 EnemyAudio + сам игрок
// (FootstepAudio) дрались за один и тот же канал. Теперь проигрывание идёт
// через AudioMixer (см. AudioMixer.h) — общий пул из НЕСКОЛЬКИХ по-
// настоящему независимых голосов с приоритетным вытеснением, так что 7
// врагов + игрок звучат одновременно, а не по очереди/вперемешку, и при
// нехватке голосов жертвуют собой в первую очередь именно частые
// малозначимые звуки (шаги), а не редкий крик/удар о стену.
//
// ДИНАМИЧЕСКАЯ ГРОМКОСТЬ ПО ДИСТАНЦИИ ("враг далеко — звука нет, ближе —
// чуть слышно, близко — хорошо слышно") — каждый play*() принимает volume
// (0..1), которую вычисляет ВЫЗЫВАЮЩИЙ код (EnemyAI::update(), см.
// DistanceVolume() там) — сам EnemyAudio ничего не знает о расстояниях.
//
// ОБЩАЯ ГРОМКОСТЬ ВРАГА ("сделать потише на процентов 20") — kVolumeScale
// ниже домножает ЛЮБОЙ переданный volume перед отправкой в микшер. Не
// трогает громкость шагов/эффектов ИГРОКА (FootstepAudio.h — отдельный,
// не масштабируется) — только звуки самого врага, как и просили.
//
// Ожидаемые файлы: assets/audio/enemy/detected_1.wav, detected_2.wav,
// moan_1.wav..moan_4.wav, footstep_walk_1.wav, footstep_walk_2.wav,
// footstep_run_1.wav, footstep_run_2.wav, wall_slam.wav, attack.wav.
//
// СТОНЫ (playMoan()) — редкие атмосферные звуки, ПОКА враг сам ищет
// игрока (патруль в Idle или Search). Источник — qubodup-GhostMoans (CC0,
// OpenGameArt), НЕ обрезаны по длине (эхо-хвост сохранён целиком).
//
// УДАР О СТЕНУ (playWallSlam()) — разовый звук в момент срыва Dash в
// Wall_slam (см. вызов в EnemyAI::update()). Источник — universfield
// door-slam (пользовательский ассет), один файл, без вариантов.
//
// УДАР ПРИ АТАКЕ (playAttack()) — разовый звук в момент реальной поимки
// игрока (Attack_Lunge). Источник — yodguard scorpion-claw-attack
// (пользовательский ассет), один файл, без вариантов.
//
// РЫЧАНИЕ В ПОГОНЕ — сознательно НЕ реализовано (было, убрано по запросу).
//
// ПРИОРИТЕТЫ (см. AudioMixer::play()) — шаги врага самые низкие (0,
// частые и малозначимые, первыми уступают голос при нехватке), стон
// повыше (1), крик/удар о стену/атака — максимальный (2, важные редкие
// события, вытесняют шаги/стон, но не друг друга).
//
// shutdown() этого класса намеренно НЕ трогает AudioMixer::instance() —
// микшер общий на все 7 врагов + игрока. Реальное выключение — из
// FootstepAudio::shutdown() (см. её комментарий).
// ============================================================================
class EnemyAudio {
public:
    bool init()
    {
#ifdef _WIN32
        if (m_available)
            return true;

        if (locateAsset(L"detected_1.wav").empty() ||
            locateAsset(L"moan_1.wav").empty()) {
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

    void shutdown()
    {
        m_available = false;
        m_detectedVariant = 0;
        m_moanVariant = 0;
        m_footstepWalkVariant = 0;
        m_footstepRunVariant = 0;
    }

    // Разовая реплика в момент обнаружения игрока (см. EnemyAI: играет
    // синхронно со стартом состояния Scream). volume — динамическая
    // громкость по дистанции (см. большой комментарий класса выше).
    void playDetected(float volume)
    {
#ifdef _WIN32
        const int variant = m_detectedVariant++ & 1;
        AudioMixer::instance().play(locateAsset(
            variant == 0
                ? L"detected_1.wav"
                : L"detected_2.wav"
        ), volume * kVolumeScale, kPriorityImportant);
#else
        (void)volume;
#endif
    }

    // Редкий стон, пока враг просто бродит/ищет игрока. Ротация по кругу
    // из 4 файлов.
    void playMoan(float volume)
    {
#ifdef _WIN32
        const int variant = m_moanVariant % kMoanVariantCount;
        m_moanVariant = (m_moanVariant + 1) % kMoanVariantCount;
        const wchar_t* names[kMoanVariantCount] = {
            L"moan_1.wav", L"moan_2.wav", L"moan_3.wav", L"moan_4.wav"
        };
        AudioMixer::instance().play(locateAsset(names[variant]), volume * kVolumeScale, kPriorityAmbient);
#else
        (void)volume;
#endif
    }

    // Шаг во время Walk_Nervous (обычная погоня/патруль/Search).
    void playFootstepWalk(float volume)
    {
#ifdef _WIN32
        const int variant = m_footstepWalkVariant++ & 1;
        AudioMixer::instance().play(locateAsset(
            variant == 0
                ? L"footstep_walk_1.wav"
                : L"footstep_walk_2.wav"
        ), volume * kVolumeScale, kPriorityFootstep);
#else
        (void)volume;
#endif
    }

    // Шаг во время Run_Frantic/Dash.
    void playFootstepRun(float volume)
    {
#ifdef _WIN32
        const int variant = m_footstepRunVariant++ & 1;
        AudioMixer::instance().play(locateAsset(
            variant == 0
                ? L"footstep_run_1.wav"
                : L"footstep_run_2.wav"
        ), volume * kVolumeScale, kPriorityFootstep);
#else
        (void)volume;
#endif
    }

    // Удар о стену в момент срыва Dash в Wall_slam (см. большой
    // комментарий класса выше).
    void playWallSlam(float volume)
    {
#ifdef _WIN32
        AudioMixer::instance().play(locateAsset(L"wall_slam.wav"), volume * kVolumeScale, kPriorityImportant);
#else
        (void)volume;
#endif
    }

    // Удар в момент срыва Dash в Attack_Lunge (реальная поимка игрока —
    // см. вызов в EnemyAI::update()). Один файл, без вариантов — как и
    // wall_slam, событие достаточно редкое.
    void playAttack(float volume)
    {
#ifdef _WIN32
        AudioMixer::instance().play(locateAsset(L"attack.wav"), volume * kVolumeScale, kPriorityImportant);
#else
        (void)volume;
#endif
    }

    bool isAvailable() const { return m_available; }

private:
    // Приоритеты для AudioMixer — см. большой комментарий класса выше.
    static constexpr int kPriorityFootstep = 0;
    static constexpr int kPriorityAmbient = 1;
    static constexpr int kPriorityImportant = 2;

    // "Сделать потише на процентов 20", потом "ещё на 20%" (0.64), затем
    // явно "поставь на 0.5 от исходного" — это НЕ ещё один множитель
    // поверх 0.64, а прямое значение с нуля: 0.5 исходной громкости.
    static constexpr float kVolumeScale = 0.5f;

#ifdef _WIN32
    // БАГФИКС/ОПТИМИЗАЦИЯ (найдено по пути) — locateAsset() раньше делала
    // полный обход файловой системы (GetModuleFileNameW + current_path() +
    // до kMaxLevelsUp+1 проверок std::filesystem::exists() НА КАЖДЫЙ
    // ПОДЪЁМ уровня, с ДВУХ стартовых точек) заново на КАЖДЫЙ вызов
    // playFootstepWalk()/playFootstepRun()/playMoan()/playDetected()/...
    // — то есть на каждый шаг игрока И каждого из 7 врагов, каждый раз
    // с нуля. Ассеты не двигаются в течение сессии, поэтому результат
    // для одного и того же имени файла ВСЕГДА один и тот же — кэшируем
    // его один раз (см. m_pathCache ниже), а не пересчитываем на каждый
    // шаг. static (а не per-instance) — все 7 врагов используют ОДИН И
    // ТОТ ЖЕ набор из ~10 файлов по одному и тому же относительному
    // пути, так что кэш имеет смысл шарить на всех, а не заводить свою
    // копию на каждого врага.
    std::wstring locateAsset(const wchar_t* name) const
    {
        static std::unordered_map<std::wstring, std::wstring> s_pathCache;

        const auto cached = s_pathCache.find(name);
        if (cached != s_pathCache.end())
            return cached->second; // пустая строка — тоже валидный кэшированный результат ("файла нет", не повторяем обход)

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

        constexpr int kMaxLevelsUp = 6;

        std::wstring result;
        for (const auto& start : startPoints) {
            std::filesystem::path dir = start;
            for (int level = 0; level <= kMaxLevelsUp; ++level) {
                const auto candidate = dir / L"assets" / L"audio" / L"enemy" / name;
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

        s_pathCache.emplace(name, result);
        return result;
    }

    bool m_available = false;
    int m_detectedVariant = 0;
    int m_moanVariant = 0;
    int m_footstepWalkVariant = 0;
    int m_footstepRunVariant = 0;
    static constexpr int kMoanVariantCount = 4;
#else
    std::wstring locateAsset(const wchar_t*) const { return {}; }
    bool m_available = false;
    int m_detectedVariant = 0;
    int m_moanVariant = 0;
    int m_footstepWalkVariant = 0;
    int m_footstepRunVariant = 0;
    static constexpr int kMoanVariantCount = 4;
#endif
};
