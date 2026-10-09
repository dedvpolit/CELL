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

// Declarations only; the implementation is compiled in Mp3Decoder.cpp
#include "minimp3_ex.h"
#include "Mp3Stream.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

// Process-wide pool of voices on WinMM (waveOut).
// PlaySoundW has one channel per process and drops overlapping sounds
// kVoiceCount devices are opened once; play() takes a free voice or steals the least important one
class AudioMixer {
public:
    // Identifies one play() call for stop()
    // startOrder guards against the slot having been reused by another sound
    struct VoiceHandle {
        int index = -1;
        std::uint64_t startOrder = 0;
    };

    static AudioMixer& instance()
    {
        static AudioMixer mixer;
        return mixer;
    }

    // Idempotent: every EnemyAudio and FootstepAudio calls it
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

        // Music has its own device so voice stealing can never evict it. Failing to open it only
        // disables music.
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
        m_music.stream.close();
        m_music = MusicChannel{};

        if (m_winmm)
            FreeLibrary(m_winmm);
        m_winmm = nullptr;
        m_available = false;
        // The PCM cache survives: the next init() skips decoding.
#endif
    }

    // volume (0..1) is computed by the caller
    // When the pool is full the lowest-priority voice is evicted (ties: oldest);
    // a sound below everything playing is dropped
    // Priorities:
    // 0 frequent
    // 1 atmospheric
    // 2 important one-offs
    VoiceHandle play(const std::wstring& absolutePath, float volume, int priority)
    {
#ifdef _WIN32
        if (!m_available || absolutePath.empty())
            return VoiceHandle{};

        volume *= m_masterVolume;

        if (volume < 0.0f) volume = 0.0f;
        if (volume > 1.0f) volume = 1.0f;
        // Inaudible; do not spend a voice on it.
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
            // All voices busy: evict the least important (oldest on ties).
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
                return VoiceHandle{}; // no voice ever opened at all: shouldn't happen

            if (priority < worstPriority)
                // Less important than everything playing: drop it.
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

        // Volume is baked into this voice's own buffer; WinMM is not reading it (the voice is free
        // or was just reset).
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

    // A stale handle is a no-op.
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

    // Affects the SFX pool and music
    // Applied on the next play() / music chunk, never to audio already queued
    void setMasterVolume(float volume01)
    {
        m_masterVolume = std::clamp(volume01, 0.0f, 1.0f);
    }
    float masterVolume() const { return m_masterVolume; }

    // Music only, on top of the master volume
    void setMusicVolume(float volume01)
    {
        m_musicVolume = std::clamp(volume01, 0.0f, 1.0f);
    }
    float musicVolume() const { return m_musicVolume; }

    // Streamed music:
    // two alternating ~100 ms chunks, each scaled at the current volume, so the
    // diary duck fades smoothly. MP3 is decoded chunk by chunk (a decoded track would be tens of MB)
    // One track, no looping; GameplayMusic decides what plays

    // Replaces the current track; false if the channel is unavailable or the file cannot be opened
    bool playMusic(const std::wstring& absolutePath)
    {
#ifdef _WIN32
        if (!m_available || !m_music.opened || absolutePath.empty())
            return false;

        // Stop first: the new source replaces the one that may be playing.
        stopMusicInternal();

        if (HasExtension(absolutePath, L".mp3"))
        {
            if (!m_music.stream.open(absolutePath, kSampleRate))
                return false;
        }
        else
        {
            if (!LoadAndConvertAudioFile(absolutePath, m_music.pcm) || m_music.pcm.empty())
                return false;
        }

        m_music.cursorSample = 0;
        m_music.sourceEnded = false;
        m_music.active = true;

        // Queue both chunks now so playback starts immediately.
        fillAndQueueMusicChunk(0);
        fillAndQueueMusicChunk(1);
        return true;
#else
        (void)absolutePath;
        return false;
#endif
    }

    // Immediate stop
    void stopMusic()
    {
#ifdef _WIN32
        stopMusicInternal();
#endif
    }

    // False once the track has played out;
    // GameplayMusic polls it to schedule the next one
    bool isMusicPlaying() const
    {
#ifdef _WIN32
        return m_music.active;
#else
        return false;
#endif
    }

    // Target of the duck ramp; update() moves toward it at kMusicDuckRampPerSecond
    void setMusicDuckTarget(float duck01)
    {
        m_musicDuckTarget = std::clamp(duck01, 0.0f, 1.0f);
    }

    // Advances the duck ramp and refills played chunks
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
                if (!m_music.sourceEnded)
                    fillAndQueueMusicChunk(i);
            }
            if (m_music.chunkQueued[i])
                anyStillActive = true;
        }

        if (!anyStillActive && m_music.sourceEnded)
            m_music.active = false;
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

    // All enemies' long-echo footsteps plus moans, screams and the player
    static constexpr int kVoiceCount = 24;
    static constexpr unsigned long kSampleRate = 48000;

    float m_masterVolume = 1.0f;
    float m_musicVolume = 1.0f;

    float m_musicDuckTarget = 1.0f;
    float m_musicDuckCurrent = 1.0f;
    // A 0.2 step settles in 2.5 s
    static constexpr float kMusicDuckRampPerSecond = 0.08f;

#ifdef _WIN32
    struct Voice {
        HWAVEOUT handle = nullptr;
        WAVEHDR header{};
        bool opened = false;
        bool busy = false;
        int priority = 0;
        std::uint64_t startOrder = 0;
        // Volume-scaled copy of the cached PCM.
        std::vector<int16_t> scaledBuffer;
    };

    // scaledBuffer keeps its capacity to avoid reallocating on every footstep
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
        // MP3 tracks are decoded on the fly; other formats are decoded whole into pcm.
        Mp3Stream stream;
        std::vector<int16_t> pcm;
        size_t cursorSample = 0;
        bool sourceEnded = true; // no more samples to queue
        bool active = false;     // a track is playing (may be inaudible if ducked to 0)
    };

    static constexpr size_t kMusicChunkSamples = kSampleRate / 10;

    // Fills chunk idx at master x music x duck volume and queues it;
    // a short last chunk is padded with silence
    void fillAndQueueMusicChunk(int idx)
    {
        MusicChannel& m = m_music;
        if (!m.opened || m.sourceEnded)
            return;

        const float volume = std::clamp(m_masterVolume * m_musicVolume * m_musicDuckCurrent, 0.0f, 1.0f);

        std::vector<int16_t>& buf = m.chunkBuffers[idx];
        buf.assign(kMusicChunkSamples, 0);

        size_t written = 0;
        if (m.stream.isOpen())
        {
            written = m.stream.read(buf.data(), kMusicChunkSamples, volume);
        }
        else
        {
            const size_t remaining = (m.cursorSample < m.pcm.size()) ? (m.pcm.size() - m.cursorSample) : 0;
            written = std::min(remaining, kMusicChunkSamples);
            for (size_t i = 0; i < written; ++i)
                buf[i] = (int16_t)std::clamp((float)m.pcm[m.cursorSample + i] * volume, -32768.0f, 32767.0f);
            m.cursorSample += written;
        }
        if (written < kMusicChunkSamples)
        {
            m.sourceEnded = true;
            m.stream.close();
            if (written == 0)
                return;
        }

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

    // Resets the channel to idle; the PCM buffer is kept
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
        m_music.stream.close();
        m_music.cursorSample = 0;
        m_music.sourceEnded = true;
        m_music.active = false;
    }

    // Mono float [-1, 1] at srcSampleRate -> int16 at kSampleRate, linear resampling.
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

    // Canonical PCM WAV (8/16-bit, mono/stereo, any rate)
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
            return false; // 24/32-bit WAV is not supported

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

    // MP3 via minimp3; same contract as LoadAndConvertWav().
    static bool LoadAndConvertMp3(const std::wstring& path, std::vector<int16_t>& outSamples)
    {
        mp3dec_t mp3d;
        // Zeroed so free(info.buffer) is safe on early failure.
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

    static bool HasExtension(const std::wstring& path, const wchar_t* ext)
    {
        const size_t n = std::wcslen(ext);
        if (path.size() < n)
            return false;
        for (size_t i = 0; i < n; ++i)
            if (std::towlower(path[path.size() - n + i]) != std::towlower(ext[i]))
                return false;
        return true;
    }

    // Picks the decoder by extension; both produce int16 at kSampleRate
    static bool LoadAndConvertAudioFile(const std::wstring& path, std::vector<int16_t>& outSamples)
    {
        if (HasExtension(path, L".mp3"))
            return LoadAndConvertMp3(path, outSamples);
        return LoadAndConvertWav(path, outSamples);
    }

    // Decoded SFX are cached for the process lifetime. Music never goes through here.
    std::shared_ptr<std::vector<int16_t>> getOrLoadPcm(const std::wstring& path)
    {
        const auto it = m_pcmCache.find(path);
        if (it != m_pcmCache.end())
            // nullptr: loading failed before; do not retry.
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
