#pragma once

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// See src/audio/Mp3Decoder.cpp for why the implementation lives in its own translation unit: this
// include only declares the functions (mp3dec_load_w() etc.).
#include "minimp3_ex.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

// Process-wide pool of independent voices on WinMM (waveOut*), used instead of PlaySoundW, which is
// a single channel per process and silently dropped sounds requested while another played.
// kVoiceCount HWAVEOUTs are opened once in init(); play() takes a free voice or steals the least
// important one.
//
// Volume is applied in software into a per-voice buffer (Voice::scaledBuffer): waveOutSetVolume()
// is not used because many drivers apply it to the whole device. The format is fixed at mono /
// 16-bit / 48000 Hz (other WAVs are converted on load); decoded SFX are cached for the process
// lifetime and shared read-only between voices.
//
// On non-Windows platforms it is a no-op (isAvailable() == false).
class AudioMixer {
public:
    // Identifies one play() call so it can be stopped early via stop() without silencing an
    // unrelated sound that later reused the same voice slot. startOrder is a generation check: if
    // the slot was recycled it will not match and stop() does nothing.
    struct VoiceHandle {
        int index = -1;
        std::uint64_t startOrder = 0;
    };

    static AudioMixer& instance()
    {
        static AudioMixer mixer;
        return mixer;
    }

    // Idempotent: called from every EnemyAudio::init() (one per enemy) and from
    // FootstepAudio::init() (player). It opens the voices only once; repeat calls while available
    // just return true.
    bool init()
    {
#ifdef _WIN32
        if (m_available)
            return true;

        HMODULE winmm = LoadLibraryW(L"winmm.dll");
        if (!winmm)
            return false;

        m_waveOutOpen = reinterpret_cast<WaveOutOpenFn>(GetProcAddress(winmm, "waveOutOpen"));
        m_waveOutClose = reinterpret_cast<WaveOutCloseFn>(GetProcAddress(winmm, "waveOutClose"));
        m_waveOutPrepareHeader = reinterpret_cast<WaveOutPrepareHeaderFn>(GetProcAddress(winmm, "waveOutPrepareHeader"));
        m_waveOutUnprepareHeader = reinterpret_cast<WaveOutUnprepareHeaderFn>(GetProcAddress(winmm, "waveOutUnprepareHeader"));
        m_waveOutWrite = reinterpret_cast<WaveOutWriteFn>(GetProcAddress(winmm, "waveOutWrite"));
        m_waveOutReset = reinterpret_cast<WaveOutResetFn>(GetProcAddress(winmm, "waveOutReset"));

        if (!m_waveOutOpen || !m_waveOutClose || !m_waveOutPrepareHeader ||
            !m_waveOutUnprepareHeader || !m_waveOutWrite || !m_waveOutReset) {
            FreeLibrary(winmm);
            return false;
        }

        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 1;
        fmt.nSamplesPerSec = kSampleRate;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = (fmt.nChannels * fmt.wBitsPerSample) / 8;
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        fmt.cbSize = 0;

        int openedCount = 0;
        for (auto& v : m_voices) {
            v = Voice{};
            const MMRESULT r = m_waveOutOpen(&v.handle, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL);
            v.opened = (r == MMSYSERR_NOERROR);
            if (v.opened)
                ++openedCount;
        }

        if (openedCount == 0) {
            FreeLibrary(winmm);
            return false;
        }

        // A dedicated handle for the streamed music channel (see MusicChannel), separate from the
        // SFX pool so a long ambient track can never be evicted by voice stealing. Failing to open
        // it is not fatal: SFX still work and music just does not play.
        m_music = MusicChannel{};
        const MMRESULT musicOpenResult = m_waveOutOpen(&m_music.handle, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL);
        m_music.opened = (musicOpenResult == MMSYSERR_NOERROR);

        m_winmm = winmm;
        m_available = true;
        return true;
#else
        return false;
#endif
    }

    void shutdown()
    {
#ifdef _WIN32
        for (auto& v : m_voices) {
            if (v.opened) {
                m_waveOutReset(v.handle);
                if (v.busy)
                    m_waveOutUnprepareHeader(v.handle, &v.header, sizeof(WAVEHDR));
                m_waveOutClose(v.handle);
            }
            v = Voice{};
        }

        if (m_music.opened) {
            m_waveOutReset(m_music.handle);
            for (int i = 0; i < 2; ++i) {
                if (m_music.chunkQueued[i])
                    m_waveOutUnprepareHeader(m_music.handle, &m_music.headers[i], sizeof(WAVEHDR));
            }
            m_waveOutClose(m_music.handle);
        }
        m_music = MusicChannel{};

        if (m_winmm)
            FreeLibrary(m_winmm);
        m_winmm = nullptr;
        m_available = false;
        // The PCM cache is not cleared: the data is small, and the next init() (a new level) skips
        // re-reading and parsing from disk.
#endif
    }

    // volume (0..1) is computed by the caller; the mixer knows nothing about positions. When the
    // pool is full the lowest-priority voice is evicted (ties: oldest), and a new sound below
    // everything playing is dropped. Priorities: 0 ambient/frequent, 1 atmospheric, 2 important
    // one-offs. The returned VoiceHandle is needed only to stop() this exact sound; scaled by
    // MASTER only (music never uses this pool).
    VoiceHandle play(const std::wstring& absolutePath, float volume, int priority)
    {
#ifdef _WIN32
        if (!m_available || absolutePath.empty())
            return VoiceHandle{};

        volume *= m_masterVolume;

        if (volume < 0.0f) volume = 0.0f;
        if (volume > 1.0f) volume = 1.0f;
        // Below the audibility threshold: do not spend a voice on a sound nobody would hear
        // (distance falloff has its own far cutoff in EnemyAI.cpp; this is a separate, lower
        // technical floor for tiny nonzero values).
        if (volume <= 0.002f)
            return VoiceHandle{};

        const std::shared_ptr<std::vector<int16_t>> pcm = getOrLoadPcm(absolutePath);
        if (!pcm || pcm->empty())
            return VoiceHandle{};

        reclaimFinishedVoices();

        int chosen = -1;
        for (int i = 0; i < (int)m_voices.size(); ++i) {
            if (m_voices[i].opened && !m_voices[i].busy) {
                chosen = i;
                break;
            }
        }

        if (chosen < 0) {
            // All voices busy: find the least important one (by priority, ties broken by the oldest
            // startOrder) instead of taking the next one round-robin, which dropped important
            // sounds as often as footsteps.
            int worstIdx = -1;
            int worstPriority = INT_MAX;
            std::uint64_t worstOrder = UINT64_MAX;
            for (int i = 0; i < (int)m_voices.size(); ++i) {
                const Voice& vv = m_voices[i];
                if (!vv.opened || !vv.busy)
                    continue;
                if (vv.priority < worstPriority ||
                    (vv.priority == worstPriority && vv.startOrder < worstOrder)) {
                    worstPriority = vv.priority;
                    worstOrder = vv.startOrder;
                    worstIdx = i;
                }
            }

            if (worstIdx < 0)
                return VoiceHandle{}; // no voice ever opened at all — shouldn't happen

            if (priority < worstPriority)
                // the new sound is less important than everything playing: drop it instead of
                // cutting off something important
                return VoiceHandle{};

            chosen = worstIdx;
            Voice& stolen = m_voices[chosen];
            m_waveOutReset(stolen.handle);
            if (stolen.busy)
                m_waveOutUnprepareHeader(stolen.handle, &stolen.header, sizeof(WAVEHDR));
            stolen.busy = false;
        }

        Voice& v = m_voices[chosen];
        if (!v.opened)
            return VoiceHandle{};

        // Volume is applied into this voice's own buffer (pcm is the shared read-only original); it
        // is safe to overwrite because WinMM is not reading it (the voice was free or was just
        // reset).
        v.scaledBuffer.resize(pcm->size());
        for (size_t i = 0; i < pcm->size(); ++i) {
            float sample = (float)(*pcm)[i] * volume;
            if (sample > 32767.0f) sample = 32767.0f;
            if (sample < -32768.0f) sample = -32768.0f;
            v.scaledBuffer[i] = (int16_t)sample;
        }

        v.header = WAVEHDR{};
        v.header.lpData = reinterpret_cast<LPSTR>(v.scaledBuffer.data());
        v.header.dwBufferLength = (DWORD)(v.scaledBuffer.size() * sizeof(int16_t));

        if (m_waveOutPrepareHeader(v.handle, &v.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
            return VoiceHandle{};

        if (m_waveOutWrite(v.handle, &v.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            m_waveOutUnprepareHeader(v.handle, &v.header, sizeof(WAVEHDR));
            return VoiceHandle{};
        }

        v.priority = priority;
        v.startOrder = m_playCounter++;
        v.busy = true;
        return VoiceHandle{ chosen, v.startOrder };
#else
        (void)absolutePath;
        (void)volume;
        (void)priority;
        return VoiceHandle{};
#endif
    }

    // Stops one playback early, identified by the handle play() returned. A stale handle (already
    // finished, or the slot recycled for another sound) is a no-op instead of silencing whatever
    // plays there now.
    void stop(VoiceHandle handle)
    {
#ifdef _WIN32
        if (!m_available || handle.index < 0 || handle.index >= (int)m_voices.size())
            return;

        Voice& v = m_voices[handle.index];
        if (!v.opened || !v.busy || v.startOrder != handle.startOrder)
            return; // already finished on its own, or this voice moved on to something else

        m_waveOutReset(v.handle);
        m_waveOutUnprepareHeader(v.handle, &v.header, sizeof(WAVEHDR));
        v.busy = false;
#else
        (void)handle;
#endif
    }

    // Scales every sound the mixer plays, the SFX pool and the music channel alike. Applied lazily
    // (per play() call for SFX, per chunk for music), never retroactively to audio already handed
    // to WinMM.
    void setMasterVolume(float volume01)
    {
        m_masterVolume = std::clamp(volume01, 0.0f, 1.0f);
    }
    float masterVolume() const { return m_masterVolume; }

    // Scales the music channel only, on top of the master volume; plain SFX are unaffected.
    void setMusicVolume(float volume01)
    {
        m_musicVolume = std::clamp(volume01, 0.0f, 1.0f);
    }
    float musicVolume() const { return m_musicVolume; }

    // Streamed music channel: play() scales a whole clip once, so a later volume change cannot
    // reach it. This channel streams double-buffered chunks (kMusicChunkSamples) re-scaled from the
    // current volume, which lets the diary duck fade smoothly. One track, no looping; GameplayMusic
    // decides what plays next. A dedicated HWAVEOUT keeps it immune to voice stealing.

    // Starts a new track, replacing the current one; false if the channel is unavailable or
    // decoding fails. The whole file is decoded synchronously on the calling thread (about 0.2 s
    // for the current track at -O2), so it stalls the frame. It decodes into the reused m_music.pcm
    // buffer, which stopMusicInternal() deliberately leaves intact.
    bool playMusic(const std::wstring& absolutePath)
    {
#ifdef _WIN32
        if (!m_available || !m_music.opened || absolutePath.empty())
            return false;

        // Stop first: the decode below writes into m_music.pcm, which may still be the buffer being
        // played. On decode failure outSamples is never touched (both decoders write only on
        // success), so stale data there is harmless.
        stopMusicInternal();

        if (!LoadAndConvertAudioFile(absolutePath, m_music.pcm) || m_music.pcm.empty())
            return false;

        m_music.cursorSample = 0;
        m_music.active = true;

        // Prime both chunks immediately so playback starts at once instead of waiting for the first
        // update() to notice an empty buffer.
        fillAndQueueMusicChunk(0);
        fillAndQueueMusicChunk(1);
        return true;
#else
        (void)absolutePath;
        return false;
#endif
    }

    // Stops the streamed channel immediately (not a fade: for that, ramp setMusicDuckTarget() down
    // and let the track finish on its own, then do not start another). Safe to call when nothing is
    // playing.
    void stopMusic()
    {
#ifdef _WIN32
        stopMusicInternal();
#endif
    }

    // True while the streamed channel still has audio to play (false once the track has run past
    // its last sample and both chunks have drained). GameplayMusic polls this to know when to
    // schedule the next random track.
    bool isMusicPlaying() const
    {
#ifdef _WIN32
        return m_music.active;
#else
        return false;
#endif
    }

    // Sets where the duck ramp (see update()) is headed; it is not an instant jump. update() moves
    // the actual multiplier toward this at kMusicDuckRampPerSecond, giving a gradual fade rather
    // than a cut.
    void setMusicDuckTarget(float duck01)
    {
        m_musicDuckTarget = std::clamp(duck01, 0.0f, 1.0f);
    }

    // Per-frame maintenance: advances the duck ramp and refills finished chunks. Call it every
    // frame unconditionally, even while gameplay is paused (e.g. the diary overlay).
    void update(float deltaTime)
    {
#ifdef _WIN32
        if (deltaTime > 0.0f) {
            const float maxStep = kMusicDuckRampPerSecond * deltaTime;
            if (m_musicDuckCurrent < m_musicDuckTarget)
                m_musicDuckCurrent = std::min(m_musicDuckTarget, m_musicDuckCurrent + maxStep);
            else if (m_musicDuckCurrent > m_musicDuckTarget)
                m_musicDuckCurrent = std::max(m_musicDuckTarget, m_musicDuckCurrent - maxStep);
        }

        if (!m_available || !m_music.opened || !m_music.active)
            return;

        bool anyStillActive = false;
        for (int i = 0; i < 2; ++i) {
            if (!m_music.chunkQueued[i]) {
                continue;
            }
            if (m_music.headers[i].dwFlags & WHDR_DONE) {
                m_waveOutUnprepareHeader(m_music.handle, &m_music.headers[i], sizeof(WAVEHDR));
                m_music.chunkQueued[i] = false;
                if (m_music.cursorSample < m_music.pcm.size())
                    fillAndQueueMusicChunk(i);
            }
            if (m_music.chunkQueued[i])
                anyStillActive = true;
        }

        if (!anyStillActive && m_music.cursorSample >= m_music.pcm.size()) {
            // Both chunks drained and no more source data: the track ended. m_music.pcm is
            // intentionally kept (reused buffer) so the next play does not reallocate it.
            m_music.active = false;
        }
#else
        (void)deltaTime;
#endif
    }

    bool isAvailable() const { return m_available; }

private:
    AudioMixer() = default;
    ~AudioMixer() = default;
    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    // headroom for overlapping long-echo footsteps of all enemies plus moans/screams and the player
    static constexpr int kVoiceCount = 24;
    static constexpr unsigned long kSampleRate = 48000;

    float m_masterVolume = 1.0f;
    float m_musicVolume = 1.0f;

    float m_musicDuckTarget = 1.0f;
    float m_musicDuckCurrent = 1.0f;
    // A 0.2 step takes 0.2 / 0.08 = 2.5 s to settle: audible as a fade, not instant, without
    // feeling stuck.
    static constexpr float kMusicDuckRampPerSecond = 0.08f;

#ifdef _WIN32
    struct Voice {
        HWAVEOUT handle = nullptr;
        WAVEHDR header{};
        bool opened = false;
        bool busy = false;
        int priority = 0;
        std::uint64_t startOrder = 0;
        // Own buffer, scaled to this playback's volume (see the class comment); not shared with the
        // m_pcmCache entries.
        std::vector<int16_t> scaledBuffer;
    };

    // Marks finished voices free. Voice::scaledBuffer is deliberately not shrunk: it settles at the
    // largest file played and is reused, avoiding allocation churn on frequent footsteps.
    void reclaimFinishedVoices()
    {
        for (auto& v : m_voices) {
            if (v.opened && v.busy && (v.header.dwFlags & WHDR_DONE)) {
                m_waveOutUnprepareHeader(v.handle, &v.header, sizeof(WAVEHDR));
                v.busy = false;
            }
        }
    }

    struct MusicChannel {
        HWAVEOUT handle = nullptr;
        bool opened = false;
        WAVEHDR headers[2]{};
        std::vector<int16_t> chunkBuffers[2];
        bool chunkQueued[2] = { false, false };
        // Reused across plays (see playMusic()) instead of freed: one heap allocation for the
        // process lifetime.
        std::vector<int16_t> pcm;
        size_t cursorSample = 0;
        bool active = false; // a track is loaded/playing (may be inaudible if duck has faded it to 0)
    };

    // ~100 ms per chunk: short enough for a duck/volume change to read as a smooth fade, long
    // enough that refilling once per frame normally keeps WinMM fed. A frame stall longer than the
    // two queued chunks (~200 ms) would underrun.
    static constexpr size_t kMusicChunkSamples = kSampleRate / 10;

    // Copies the next chunk of samples from m_music.pcm into chunkBuffers[idx] at the current
    // combined volume (master x music x duck) and submits it to WinMM. A short final chunk is
    // padded with silence instead of being special-cased.
    void fillAndQueueMusicChunk(int idx)
    {
        MusicChannel& m = m_music;
        if (!m.opened || m.pcm.empty())
            return;

        const float volume = std::clamp(m_masterVolume * m_musicVolume * m_musicDuckCurrent, 0.0f, 1.0f);

        std::vector<int16_t>& buf = m.chunkBuffers[idx];
        buf.assign(kMusicChunkSamples, 0);

        const size_t remaining = (m.cursorSample < m.pcm.size()) ? (m.pcm.size() - m.cursorSample) : 0;
        const size_t toCopy = std::min(remaining, kMusicChunkSamples);
        for (size_t i = 0; i < toCopy; ++i) {
            float sample = (float)m.pcm[m.cursorSample + i] * volume;
            if (sample > 32767.0f) sample = 32767.0f;
            if (sample < -32768.0f) sample = -32768.0f;
            buf[i] = (int16_t)sample;
        }
        m.cursorSample += toCopy;

        m.headers[idx] = WAVEHDR{};
        m.headers[idx].lpData = reinterpret_cast<LPSTR>(buf.data());
        m.headers[idx].dwBufferLength = (DWORD)(buf.size() * sizeof(int16_t));

        if (m_waveOutPrepareHeader(m.handle, &m.headers[idx], sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
            return;
        if (m_waveOutWrite(m.handle, &m.headers[idx], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            m_waveOutUnprepareHeader(m.handle, &m.headers[idx], sizeof(WAVEHDR));
            return;
        }
        m.chunkQueued[idx] = true;
    }

    // Shared teardown for playMusic() and stopMusic(): resets the channel to idle. It leaves
    // m_music.pcm's contents untouched; that buffer is reused between plays, not freed.
    void stopMusicInternal()
    {
        if (!m_music.opened)
            return;
        m_waveOutReset(m_music.handle); // marks any queued headers WHDR_DONE and stops audio immediately
        for (int i = 0; i < 2; ++i) {
            if (m_music.chunkQueued[i]) {
                m_waveOutUnprepareHeader(m_music.handle, &m_music.headers[i], sizeof(WAVEHDR));
                m_music.chunkQueued[i] = false;
            }
        }
        m_music.cursorSample = 0;
        m_music.active = false;
    }

    // Shared tail of both loaders below: mono float samples in [-1, 1] at srcSampleRate -> int16
    // samples at the mixer's fixed kSampleRate, by linear interpolation (good enough for game
    // audio). Both formats go through the same resampling path.
    static void DownmixedToOutputSamples(const std::vector<float>& mono, unsigned int srcSampleRate,
                                          std::vector<int16_t>& outSamples)
    {
        if (srcSampleRate == kSampleRate) {
            outSamples.resize(mono.size());
            for (size_t i = 0; i < mono.size(); ++i) {
                float v = mono[i];
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                outSamples[i] = (int16_t)(v * 32767.0f);
            }
            return;
        }

        const size_t outCount = (size_t)((double)mono.size() * (double)kSampleRate / (double)srcSampleRate);
        outSamples.resize(outCount);
        for (size_t i = 0; i < outCount; ++i) {
            const double srcPos = (double)i * (double)srcSampleRate / (double)kSampleRate;
            const size_t i0 = (size_t)srcPos;
            const size_t i1 = (i0 + 1 < mono.size()) ? i0 + 1 : i0;
            const float frac = (float)(srcPos - (double)i0);
            float v = mono[i0] * (1.0f - frac) + mono[i1] * frac;
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            outSamples[i] = (int16_t)(v * 32767.0f);
        }
    }

    // Reads a canonical PCM WAV (8/16 bit, mono/stereo, any rate) and converts it to the voice
    // format: channel averaging plus linear resampling.
    static bool LoadAndConvertWav(const std::wstring& path, std::vector<int16_t>& outSamples)
    {
        std::ifstream file(path.c_str(), std::ios::binary);
        if (!file)
            return false;

        char riffHeader[12];
        file.read(riffHeader, 12);
        if (!file || std::strncmp(riffHeader, "RIFF", 4) != 0 || std::strncmp(riffHeader + 8, "WAVE", 4) != 0)
            return false;

        WORD channels = 0, bitsPerSample = 0;
        DWORD sampleRate = 0;
        std::vector<uint8_t> dataChunk;
        bool haveFmt = false;

        while (file) {
            char chunkId[4];
            uint32_t chunkSize = 0;
            file.read(chunkId, 4);
            file.read(reinterpret_cast<char*>(&chunkSize), 4);
            if (!file)
                break;

            if (std::strncmp(chunkId, "fmt ", 4) == 0) {
                std::vector<uint8_t> fmtBuf(chunkSize);
                file.read(reinterpret_cast<char*>(fmtBuf.data()), chunkSize);
                if (fmtBuf.size() >= 16) {
                    channels = *reinterpret_cast<uint16_t*>(&fmtBuf[2]);
                    sampleRate = *reinterpret_cast<uint32_t*>(&fmtBuf[4]);
                    bitsPerSample = *reinterpret_cast<uint16_t*>(&fmtBuf[14]);
                    haveFmt = true;
                }
                if (chunkSize % 2 != 0)
                    file.seekg(1, std::ios::cur);
            } else if (std::strncmp(chunkId, "data", 4) == 0) {
                dataChunk.resize(chunkSize);
                file.read(reinterpret_cast<char*>(dataChunk.data()), chunkSize);
                if (chunkSize % 2 != 0)
                    file.seekg(1, std::ios::cur);
            } else {
                file.seekg(chunkSize + (chunkSize % 2), std::ios::cur);
            }
        }

        if (!haveFmt || dataChunk.empty() || channels == 0 || sampleRate == 0)
            return false;
        if (bitsPerSample != 8 && bitsPerSample != 16)
            return false; // rare 24/32-bit assets are already converted ahead of time in the prep pipeline

        const size_t bytesPerSample = bitsPerSample / 8;
        const size_t frameCount = dataChunk.size() / (bytesPerSample * channels);

        std::vector<float> mono(frameCount);
        for (size_t i = 0; i < frameCount; ++i) {
            float sum = 0.0f;
            for (WORD c = 0; c < channels; ++c) {
                const size_t offset = (i * channels + c) * bytesPerSample;
                float sample;
                if (bitsPerSample == 16) {
                    const int16_t s = *reinterpret_cast<const int16_t*>(&dataChunk[offset]);
                    sample = s / 32768.0f;
                } else {
                    const uint8_t s = dataChunk[offset];
                    sample = (s - 128) / 128.0f;
                }
                sum += sample;
            }
            mono[i] = sum / (float)channels;
        }

        DownmixedToOutputSamples(mono, sampleRate, outSamples);
        return true;
    }

    // MP3 decoding via minimp3 (implementation unit: Mp3Decoder.cpp). Same failure semantics as
    // LoadAndConvertWav() (false = do not play) and the same downmix/resample tail.
    static bool LoadAndConvertMp3(const std::wstring& path, std::vector<int16_t>& outSamples)
    {
        mp3dec_t mp3d;
        // zero-initialized: info.buffer must be nullptr on any early failure so the free() below is
        // safe (free(nullptr) is a no-op)
        mp3dec_file_info_t info = {};
        if (mp3dec_load_w(&mp3d, path.c_str(), &info, nullptr, nullptr) != 0 ||
            !info.buffer || info.samples == 0 || info.channels <= 0)
        {
            if (info.buffer) free(info.buffer);
            return false;
        }

        const size_t channels = (size_t)info.channels;
        const size_t frameCount = info.samples / channels;

        std::vector<float> mono(frameCount);
        for (size_t i = 0; i < frameCount; ++i) {
            float sum = 0.0f;
            for (size_t c = 0; c < channels; ++c)
                sum += info.buffer[i * channels + c] / 32768.0f;
            mono[i] = sum / (float)channels;
        }
        free(info.buffer);

        DownmixedToOutputSamples(mono, (unsigned int)info.hz, outSamples);
        return true;
    }

    // Dispatches to the decoder by file extension. Everything downstream (caching, volume scaling,
    // mixing) is format-agnostic: both loaders produce the same std::vector<int16_t> at the mixer's
    // fixed sample rate.
    static bool LoadAndConvertAudioFile(const std::wstring& path, std::vector<int16_t>& outSamples)
    {
        if (path.size() >= 4) {
            std::wstring ext = path.substr(path.size() - 4);
            for (wchar_t& c : ext) c = (wchar_t)std::towlower(c);
            if (ext == L".mp3")
                return LoadAndConvertMp3(path, outSamples);
        }
        return LoadAndConvertWav(path, outSamples);
    }

    // Caches decoded SFX forever, keyed by path: cheap for the small, frequently replayed files
    // this pool handles. Music does not use this cache (see playMusic()): a multi-minute track is
    // tens of MB decoded and would stay in memory after a single play.
    std::shared_ptr<std::vector<int16_t>> getOrLoadPcm(const std::wstring& path)
    {
        const auto it = m_pcmCache.find(path);
        if (it != m_pcmCache.end())
            // may be nullptr: loading this file already failed, do not retry on every play()
            return it->second;

        auto samples = std::make_shared<std::vector<int16_t>>();
        if (!LoadAndConvertAudioFile(path, *samples) || samples->empty())
            samples.reset();

        m_pcmCache[path] = samples;
        return samples;
    }

    using WaveOutOpenFn = MMRESULT(WINAPI*)(LPHWAVEOUT, UINT_PTR, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
    using WaveOutCloseFn = MMRESULT(WINAPI*)(HWAVEOUT);
    using WaveOutPrepareHeaderFn = MMRESULT(WINAPI*)(HWAVEOUT, LPWAVEHDR, UINT);
    using WaveOutUnprepareHeaderFn = MMRESULT(WINAPI*)(HWAVEOUT, LPWAVEHDR, UINT);
    using WaveOutWriteFn = MMRESULT(WINAPI*)(HWAVEOUT, LPWAVEHDR, UINT);
    using WaveOutResetFn = MMRESULT(WINAPI*)(HWAVEOUT);

    bool m_available = false;
    HMODULE m_winmm = nullptr;
    WaveOutOpenFn m_waveOutOpen = nullptr;
    WaveOutCloseFn m_waveOutClose = nullptr;
    WaveOutPrepareHeaderFn m_waveOutPrepareHeader = nullptr;
    WaveOutUnprepareHeaderFn m_waveOutUnprepareHeader = nullptr;
    WaveOutWriteFn m_waveOutWrite = nullptr;
    WaveOutResetFn m_waveOutReset = nullptr;

    std::array<Voice, kVoiceCount> m_voices;
    std::uint64_t m_playCounter = 0;
    std::unordered_map<std::wstring, std::shared_ptr<std::vector<int16_t>>> m_pcmCache;

    MusicChannel m_music;
#else
    bool m_available = false;
#endif
};
