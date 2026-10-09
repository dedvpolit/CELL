#pragma once
#include "AssetLocate.h"
#include "AudioMixer.h"

// Interface sounds: a quiet tick when a button gets the cursor, a heavier one when it is pressed.
// The pressed sound also marks in-game pickups and opening a diary.
namespace UiAudio {

// Priority 2: never evicted by footsteps or moans.
constexpr int kPriority = 2;
constexpr float kHoverVolume = 0.45f;
constexpr float kClickVolume = 0.8f;

inline void PlayHover()
{
#ifdef _WIN32
    AudioMixer::instance().play(LocateAudioAsset(L"ui", L"hover.wav"), kHoverVolume, kPriority);
#endif
}

inline void PlayClick()
{
#ifdef _WIN32
    AudioMixer::instance().play(LocateAudioAsset(L"ui", L"click.wav"), kClickVolume, kPriority);
#endif
}

} // namespace UiAudio
