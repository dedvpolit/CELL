CELL — Footstep audio
=====================

Added four short PCM WAV samples:
  assets/audio/footsteps/footstep_walk_1.wav
  assets/audio/footsteps/footstep_walk_2.wav
  assets/audio/footsteps/footstep_run_1.wav
  assets/audio/footsteps/footstep_run_2.wav

The game uses FootstepAudio.h, a header-only lightweight wrapper around
PlaySoundW resolved dynamically from Windows winmm.dll. No OpenAL/SDL/FM0D
or additional linker library is required.

Footsteps are triggered from actual horizontal displacement after collision
resolution:
  walk: about one step every 0.68 m
  run:  about one step every 0.95 m

A new movement burst produces an immediate first step; switching walk/run
restarts the rhythm. Noclip does not make footsteps because it represents
flying/debug movement.

On non-Windows builds the audio wrapper is a safe no-op, so the renderer still
builds without a platform audio dependency.

CELL — Enemy audio (THE WRAPPED)
=================================

Final set under assets/audio/enemy/:

  footstep_walk_1.wav / footstep_walk_2.wav  - Walk_Nervous footfall (mixkit-monster-footstep-1975, edited)
  footstep_run_1.wav  / footstep_run_2.wav   - Run_Frantic/Dash footfall (mixkit-monster-footstep-1974, edited)
  moan_1.wav .. moan_4.wav                   - occasional moan while searching (qubodup-GhostMoans, CC0, OpenGameArt)
  detected_1.wav / detected_2.wav            - Scream.lol screech (mixkit-monster-wraith-passing-by, edited)

Footstep source clips (1974/1975) intentionally keep their natural
reverb/echo tail — trimmed only past the point where they're already
inaudible (~1.3s in), not cut down to a bare transient. Two pitch/speed-
shifted variants come from each single source file where only one real
recording was available (detected_2, footstep_run_2, footstep_walk_2) —
same trick, not exact duplicates.

Periodic growl during chase was tried and then explicitly dropped by
request — EnemyAudio::playGrowl() still exists but EnemyAI no longer calls
it, and growl_1/2.wav were removed from assets/audio/enemy/.

Footsteps are triggered from EnemyAI::update(), keyed off the animation
actually shown this frame (Walk vs Run, including patrol/Search which
reuse Walk_Nervous) — same real-displacement cadence idea as the player's
footsteps above:
  walk: about one step every 0.75 m
  run:  about one step every 1.10 m

Moans play only while the enemy is searching for the player on its own
(Idle patrol or Search state) — never during an active chase — on a
randomized interval (9-18s), reset each time the enemy leaves that mode.

Scream audio is triggered from EnemyAI's Scream state via
EnemyAudio::playDetected(), same call site as before.

CELL — Voice priority + wall slam + volume trim
=================================================

BUGFIX ("sounds overlap, some just don't play"): AudioMixer voice pool
raised 16 -> 24, and voice stealing (when the pool is full) is now
priority-based instead of round-robin: each play() call carries a
priority (0 = frequent/low-stakes like enemy footsteps, 1 = player
footsteps and enemy moans, 2 = rare important events — detection scream,
wall slam). When no free voice exists, the LOWEST-priority currently
playing voice is stolen first; if the new sound's own priority is lower
than everything currently playing, the new sound is the one dropped
instead — the failure mode is now "an extra footstep gets skipped
occasionally", not "the scream you needed to hear never played".

New: assets/audio/enemy/wall_slam.wav (dragon-studio door-slam, user
asset, trimmed to 2.15s with a short fade-out) plays once via
EnemyAudio::playWallSlam() the instant a Dash turns into Wall_slam,
volume by the same distance falloff as footsteps.

All enemy sounds (footsteps, moan, detected, wall slam) are now scaled by
a flat 0.8x (EnemyAudio::kVolumeScale) — "quieter by about 20%", per
request. Does not affect the player's own footsteps (FootstepAudio.h is
untouched by this scale).

CELL — Wall slam swap + another -20%
=====================================

wall_slam.wav replaced with universfield-door-slam-229310 (user asset),
1.25s, no trimming needed (already self-contained: clean impact around
0.15-0.3s, natural decay to silence by ~1.15s), tiny fade-out added at the
very end for safety.

EnemyAudio::kVolumeScale is now 0.8*0.8 = 0.64 of original — "another 20%
quieter" stacks on the earlier -20%, it isn't a reset back to a fresh -20%
off the original level.

CELL — Attack sound + volume set to 0.5
=========================================

New: assets/audio/enemy/attack.wav (yodguard scorpion-claw-attack, user
asset), 1.35s. Source was 3s with a strange constant low-level tail past
~1.6s (likely room tone, not part of the actual hit) — trimmed to the
real content (swell + impact + decay) with a short fade-out, matching the
Attack_Lunge clip's 1.25s length.

Plays via EnemyAudio::playAttack() at both places EnemyAI transitions into
Attack (real catch during Walk/Run pursuit, and the Dash-catches-player
branch) — same distance-based volume as footsteps/wall_slam.

EnemyAudio::kVolumeScale set explicitly to 0.5 of the original level (not
a further multiplier on top of the earlier 0.64 — a direct "set to half"
request).

CELL — Volume-crosstalk bugfix
================================

BUGFIX ("enemy sound gets louder when the player starts walking, even
though the enemy's distance didn't change"): AudioMixer used to set
volume via waveOutSetVolume(handle, volume) per voice, assuming it was
scoped to that one handle (per the formal docs). In practice, on many
real Windows audio drivers (especially the default WAVE_MAPPER on common
onboard sound), waveOutSetVolume actually controls the whole output
device/hardware mixer, not the specific handle — so playing the player's
own footstep at volume 1.0 was silently cranking up the device volume for
everything else already playing too, including a quieter, farther enemy
sound.

Fix: volume is no longer sent to the device at all. Each play() now
scales the PCM samples themselves in software, into a per-voice owned
buffer (Voice::scaledBuffer), before handing them to WinMM. Since the
device is never told to change its volume, one sound's loudness can no
longer bleed into another's.

CELL — RAM regression fix (100-120MB -> back toward 85-100MB baseline)
========================================================================

BUG: std::vector::resize() never releases already-allocated capacity when
shrinking, only grows it. AudioMixer's 24 voices are reused across
different files over a play session (a small footstep now, a big moan
later, small footsteps again) — without an explicit release, each voice's
scaledBuffer would silently grow to the size of the LARGEST file it ever
happened to play (moan_3.wav, 550KB) and stay there forever, even while
later playing only 120KB footsteps. With 24 voices eventually each
touching at least one moan over time, that's up to ~24 x 500KB =~ 12MB of
dead weight that never gets freed — this is the main suspect for the
100-120MB regression from the previous 85-100MB baseline (before the
audio system existed).

Fix: reclaimFinishedVoices() now calls scaledBuffer.clear() +
shrink_to_fit() the moment a voice is confirmed done playing, releasing
the memory back immediately instead of hoarding the historical peak size.
Verified shrink_to_fit() actually drops capacity to 0 on this toolchain
(libstdc++, used by both the MinGW-w64 Windows build and this dev
environment) with a standalone test before relying on it.
