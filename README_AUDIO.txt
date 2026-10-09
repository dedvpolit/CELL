CELL - Audio
============

Reference for how sound works in the engine. Source of truth is the code; this file
explains the design and lists the assets and their sources.

1. Architecture
---------------

AudioMixer (src/audio/AudioMixer.h) is a process-wide singleton on top of WinMM
(waveOut*), loaded dynamically from winmm.dll: no OpenAL/SDL/FMOD and no extra linker
library. On non-Windows builds it is a safe no-op (isAvailable() == false), so the
renderer still builds without a platform audio dependency.

SFX pool
  - kVoiceCount = 24 independent HWAVEOUT voices, opened once in init().
  - play(path, volume01, priority). When no voice is free, the lowest-priority playing
    voice is stolen (ties: the oldest). A new sound with a lower priority than everything
    playing is dropped instead. Priorities: 0 enemy footsteps, 1 player footsteps and
    enemy moans, 2 rare important events (detection scream, wall slam, attack).
  - Why not PlaySoundW: it is a single system channel per process, so with several
    enemies plus the player any request made while another sound played was silently
    dropped (especially footsteps with their 1.2-1.3 s echo tail).
  - Volume is applied in software into a per-voice buffer (Voice::scaledBuffer) before
    the sound is handed to WinMM. waveOutSetVolume() is deliberately not used: on many
    Windows drivers (notably the default WAVE_MAPPER) it changes the volume of the whole
    device, so one loud sound raised every other playing sound.
  - Fixed format: mono, 16-bit, 48000 Hz. Other WAV files are converted on first load.
    Decoded SFX stay cached in memory for the process lifetime.
  - Each voice's scaledBuffer keeps its capacity between plays (no shrink_to_fit):
    shrinking on every idle and regrowing on every play would churn allocations many
    times a second (footsteps are frequent). Worst case this is a stable ~12 MB.

Music channel
  - A separate HWAVEOUT, so a long track can never be evicted by voice stealing.
  - The track is streamed in ~100 ms double-buffered chunks, each re-scaled from the
    current combined volume (MASTER x MUSIC x duck) when it is filled. That is what lets
    the diary-read duck and the sliders take effect mid-track; a single pre-scaled SFX
    buffer cannot do this.
  - setMusicDuckTarget() only sets where the ramp is headed; update() (called once per
    frame, unconditionally, from Application::tick()) moves the multiplier there at
    kMusicDuckRampPerSecond = 0.08/s, so a 20% step fades over about 2.5 s.
  - One track at a time, no looping. MP3 tracks are decoded on demand, one chunk at a
    time (Mp3Stream.h), so opening a track is instant and its memory stays at a few KB.
    Other formats are decoded whole into MusicChannel::pcm.

Volume settings
  - MASTER and MUSIC sliders on the Settings screen. The values live in Application
    (m_masterVolume/m_musicVolume) and are pushed to the mixer at the top of every frame,
    before AudioMixer::update() refills the stream.
  - MASTER scales every sound (SFX and music). MUSIC scales only the music channel.

2. Assets (assets/audio/)
-------------------------

Player footsteps (footsteps/): footstep_walk_1.wav, footstep_walk_2.wav,
footstep_run_1.wav, footstep_run_2.wav. Made by the author (dedvpolit).

Enemy sounds (enemy/), for the enemy "THE WRAPPED":
  footstep_walk_1/2.wav   Walk_Nervous footfall (mixkit-monster-footstep-1975, edited)
  footstep_run_1/2.wav    Run_Frantic/Dash footfall (mixkit-monster-footstep-1974, edited)
  moan_1 .. moan_4.wav    occasional moan while searching (qubodup GhostMoans, CC0,
                          OpenGameArt)
  detected_1/2.wav        scream on detection (mixkit-monster-wraith-passing-by, edited)
  wall_slam.wav           Dash hits a wall (universfield-door-slam-229310, Pixabay,
                          1.25 s)
  attack.wav              Attack_Lunge hit (yodguard scorpion-claw-attack, Pixabay,
                          1.35 s: trimmed to the real content, matches the 1.25 s clip)

  The footstep clips keep their natural reverb tail, trimmed only past the point where
  it is inaudible (~1.3 s in). detected_2, footstep_run_2 and footstep_walk_2 are
  pitch/speed-shifted variants of the same single source recording as their _1 pair.

Music (music/): cartoon_music-horror-soundscape-ambience-533209.mp3 (Pixabay),
re-encoded from the source master with libmp3lame VBR (-q:a 3), which keeps the audible
bandwidth at about half the size of the 256 kbps master. It is stereo with almost no
mid/side correlation, so it must not be downmixed to mono at the source.

3. Behavior
-----------

Player footsteps (FootstepAudio.h, called from PlayerController::processInput()):
  - Triggered by the actual horizontal displacement after collision resolution: one step
    per 0.68 m walking, per 0.95 m running. Volume is not distance-based (always 1.0).
  - A new movement burst plays a step immediately; switching walk/run restarts the rhythm.
  - Noclip makes no footsteps (it represents flying/debug movement).

Enemy sounds (EnemyAudio.h; volume 0..1 is computed by the caller from the distance to
the player, EnemyAudio itself knows nothing about positions):
  - Footsteps come from EnemyAI::update(), keyed off the animation actually shown this
    frame (Walk vs Run, including patrol and Search, which reuse Walk_Nervous): one step
    per 0.75 m walking, per 1.10 m running.
  - Moans only while the enemy wanders or searches on its own (Idle patrol or Search),
    never during a chase, at a random interval of 9-18 s that is re-rolled each time.
  - Detection scream: EnemyAudio::playDetected() when the Scream state starts. Wall slam:
    playWallSlam() the instant a Dash turns into Wall_slam. Attack: playAttack() at both
    places EnemyAI enters Attack (a real catch during pursuit, and a Dash that catches
    the player).
  - Every enemy sound is multiplied by EnemyAudio::kVolumeScale = 0.5 on top of the
    distance-based volume. The player's own footsteps are not scaled.
  - Chase growling is deliberately not implemented.

Gameplay ambience (GameplayMusic.h, ticked every frame by DungeonScene::tickAmbientMusic()):
  - While gameplay is active the single track is replayed after a random 60-75 s silence.
    When gameplay is not active (menu, pause, ...) the track stops, the duck resets to
    full volume and the replay timer is cleared.
  - Diary duck: the overall volume steps down by 20 percentage points of the original at
    2, 4, 7, 10 and 12 diaries read, reaching silence at the fifth step. These are five
    flat steps off the original volume, not a repeated x0.8, which would only reach ~33%.
  - The duck target is recomputed every frame from diariesReadCount(), independent of
    AppState and not tied to DungeonScene::processInput(): that function is skipped while
    the diary-reading overlay is open, but m_diariesRead flips the instant a diary is
    opened, so the duck must keep fading during exactly that window.

"Unreliable vision" sounds (DungeonScene::updateGlitchEffects()):
  - From 300 s of real, unpaused play time in the current run, one of three sounds is
    chosen at random: a fake enemy moan, fake WALKING footsteps, or fake RUNNING footsteps
    for 2-4 s. The footstep variant that matches what the player is actually doing is
    excluded (it would only layer over their own steps), so walking can produce
    mismatched running steps and vice versa; standing still allows either.
  - The fake-footsteps timers tick independently of the trigger, so an effect that has
    started plays out its full duration even if the stage changes, and they are reset
    when a map is loaded.

4. Tuning knobs
---------------

  AudioMixer.h        kVoiceCount, kMusicChunkSamples, kMusicDuckRampPerSecond
  EnemyAudio.h        kVolumeScale, the priorities
  GameplayMusic.h     kMinGapSeconds/kMaxGapSeconds, kDuckThresholds
  PlayerController.cpp  player step lengths (0.68 / 0.95)
  EnemyAI.cpp         kEnemyWalkStepLength/kEnemyRunStepLength, the moan interval,
                      kPerceptionInterval

5. Known limitations
--------------------

  - Two 100 ms music chunks are queued, so a frame that takes longer than about 200 ms
    underruns the music.
  - The search for audio files exists in three copies (AssetLocate.h, EnemyAudio.h,
    FootstepAudio.h), each with its own cache.
  - The WAV loader trusts chunk sizes from the file, so a corrupt WAV can throw
    std::bad_alloc; nothing in the project catches exceptions.
  - Audio is Windows-only (WinMM); other platforms get no sound.
