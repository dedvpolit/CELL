#pragma once

#include <random>

#include "AudioMixer.h"
#include "AssetLocate.h"

// Gameplay ambience: replays one track at random 60-75 s gaps while gameplay is active, and owns
// the diary-read duck (volume steps down 20 percentage points at 2/4/7/10/12 diaries, silent at the
// fifth step). Smoothing lives in AudioMixer; this class only sets the target. It is ticked every
// frame by DungeonScene::tickAmbientMusic(), even while the reading overlay pauses processInput(),
// so the duck follows diariesReadCount().
class GameplayMusic {
public:
    // active = false (menu, paused, AudioMixer not initialized yet, ...) stops any playing track,
    // resets the duck to full volume and clears the replay timer, so gameplay resumes with a fresh
    // wait rather than mid-countdown.
    void update(float deltaTime, bool active, int diariesRead)
    {
        AudioMixer& mixer = AudioMixer::instance();

        if (!active) {
            mixer.setMusicDuckTarget(1.0f);
            if (m_trackActiveLastFrame)
                mixer.stopMusic();
            m_trackActiveLastFrame = false;
            m_waitTimer = 0.0f;
            m_haveNextWait = false;
            return;
        }

        if (!mixer.isAvailable())
            return;

        mixer.setMusicDuckTarget(DuckTargetForDiaryCount(diariesRead));

        if (mixer.isMusicPlaying()) {
            m_trackActiveLastFrame = true;
            return;
        }

        if (!m_haveNextWait) {
            std::uniform_real_distribution<float> gapDist(kMinGapSeconds, kMaxGapSeconds);
            m_nextWaitSeconds = gapDist(m_rng);
            m_waitTimer = 0.0f;
            m_haveNextWait = true;
        }

        m_waitTimer += deltaTime;
        if (m_waitTimer < m_nextWaitSeconds)
            return;

        if (mixer.playMusic(LocateAudioAsset(L"music", kTrackFileName)))
            m_trackActiveLastFrame = true;
        m_haveNextWait = false; // roll a new gap regardless of success, rather than retry every frame
    }

private:
    static constexpr const wchar_t* kTrackFileName =
        L"cartoon_music-horror-soundscape-ambience-533209.mp3";

    // 1.0/0.8/0.6/0.4/0.2/0.0: five 20-point steps, reaching silence at the fifth threshold.
    static constexpr int kDuckThresholds[5] = { 2, 4, 7, 10, 12 };

    static float DuckTargetForDiaryCount(int diariesRead)
    {
        int stepsReached = 0;
        for (int threshold : kDuckThresholds) {
            if (diariesRead >= threshold)
                ++stepsReached;
        }
        return 1.0f - 0.2f * (float)stepsReached;
    }

    static constexpr float kMinGapSeconds = 60.0f;
    static constexpr float kMaxGapSeconds = 75.0f;

    std::mt19937 m_rng{ std::random_device{}() };
    float m_waitTimer = 0.0f;
    float m_nextWaitSeconds = 0.0f;
    bool m_haveNextWait = false;
    bool m_trackActiveLastFrame = false;
};
