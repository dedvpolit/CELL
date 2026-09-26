#pragma once

#include <string>
#include <vector>

#include "AssetLocate.h"
#include "AudioMixer.h"

// Footstep playback for the player on top of the shared AudioMixer. Enemy footsteps (EnemyAudio.h,
// one instance per enemy) use the same voice pool. The player's own footsteps do not depend on
// distance (played at volume 1.0), unlike EnemyAudio, which applies distance-based volume.
class FootstepAudio {
public:
    bool init()
    {
#ifdef _WIN32
        if (m_available)
            return true;

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

    // The one place that actually stops the shared AudioMixer: EnemyAudio::shutdown() deliberately
    // does not (the mixer is shared by all enemies). There is one player, so this runs once on
    // scene teardown.
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
    // Priority for AudioMixer (see EnemyAudio.h for the scale: 0 enemy footsteps, 1 this and the
    // enemy moan, 2 scream/wall hit). The player's footsteps are slightly more important than the
    // enemies' (feedback about the player's own movement) but not as critical as one-off events.
    static constexpr int kPriority = 1;

    static std::wstring locateAsset(const wchar_t* name)
    {
        return LocateAudioAsset(L"footsteps", name);
    }

#ifdef _WIN32
    bool m_available = false;
    int m_walkVariant = 0;
    int m_runVariant = 0;
#else
    bool m_available = false;
    int m_walkVariant = 0;
    int m_runVariant = 0;
#endif
};
