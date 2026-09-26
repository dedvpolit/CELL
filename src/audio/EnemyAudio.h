#pragma once

#include <string>
#include <vector>

#include "AssetLocate.h"
#include "AudioMixer.h"

// Sounds for one enemy (scream, moan, footsteps, wall slam, attack) played through AudioMixer. Each
// play*() takes a 0..1 volume computed by the caller (distance-based); kVolumeScale quiets only
// enemy sounds. Priorities (AudioMixer::play()): footsteps 0, moan 1, scream/hit/attack 2. Files:
// assets/audio/enemy/{detected,moan,footstep_walk,footstep_run}_N.wav, wall_slam.wav, attack.wav.
// shutdown() does not stop the shared mixer; FootstepAudio::shutdown() does. Chase growling is
// deliberately not implemented.
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

    void playWallSlam(float volume)
    {
#ifdef _WIN32
        AudioMixer::instance().play(locateAsset(L"wall_slam.wav"), volume * kVolumeScale, kPriorityImportant);
#else
        (void)volume;
#endif
    }

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
    static constexpr int kPriorityFootstep = 0;
    static constexpr int kPriorityAmbient = 1;
    static constexpr int kPriorityImportant = 2;

    // applied directly to the passed volume, not a cumulative multiplier
    static constexpr float kVolumeScale = 0.5f;

    static std::wstring locateAsset(const wchar_t* name)
    {
        return LocateAudioAsset(L"enemy", name);
    }

#ifdef _WIN32
    bool m_available = false;
    int m_detectedVariant = 0;
    int m_moanVariant = 0;
    int m_footstepWalkVariant = 0;
    int m_footstepRunVariant = 0;
    static constexpr int kMoanVariantCount = 4;
#else
    bool m_available = false;
    int m_detectedVariant = 0;
    int m_moanVariant = 0;
    int m_footstepWalkVariant = 0;
    int m_footstepRunVariant = 0;
    static constexpr int kMoanVariantCount = 4;
#endif
};
