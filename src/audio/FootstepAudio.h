#pragma once

#include <string>
#include <vector>

#include "AssetLocate.h"
#include "AudioMixer.h"

// Player footsteps through the shared AudioMixer, always at full volume
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

    // The only owner that stops the shared mixer; EnemyAudio does not
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
    // Priority 1:
    // slightly above enemy footsteps (0)
    // below screams and wall hits (2)
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
