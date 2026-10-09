#pragma once
#include "minimp3_ex.h" // implementation compiled in Mp3Decoder.cpp
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Streaming MP3 decode to mono int16 at a fixed output rate
class Mp3Stream {
public:
    Mp3Stream() = default;
    ~Mp3Stream() { close(); }
    Mp3Stream(const Mp3Stream&) = delete;
    Mp3Stream& operator=(const Mp3Stream&) = delete;
    Mp3Stream(Mp3Stream&& other) noexcept { *this = std::move(other); }
    Mp3Stream& operator=(Mp3Stream&& other) noexcept
    {
        if (this != &other)
        {
            close();
            m_dec = std::move(other.m_dec);
            m_open = other.m_open;
            m_sourceEnded = other.m_sourceEnded;
            m_channels = other.m_channels;
            m_step = other.m_step;
            m_pos = other.m_pos;
            m_source = std::move(other.m_source);
            m_decodeBuffer = std::move(other.m_decodeBuffer);
            other.m_open = false;
            other.m_sourceEnded = true;
        }
        return *this;
    }

#ifdef _WIN32
    bool open(const std::wstring& path, unsigned int outRate)
    {
        close();
        m_dec = std::make_unique<mp3dec_ex_t>();
        // DO_NOT_SCAN: no full pass over the file to compute its duration.
        if (mp3dec_ex_open_w(m_dec.get(), path.c_str(), MP3D_SEEK_TO_BYTE | MP3D_DO_NOT_SCAN) != 0)
            return fail();
        return start(outRate);
    }
#endif
    bool open(const std::string& path, unsigned int outRate)
    {
        close();
        m_dec = std::make_unique<mp3dec_ex_t>();
        if (mp3dec_ex_open(m_dec.get(), path.c_str(), MP3D_SEEK_TO_BYTE | MP3D_DO_NOT_SCAN) != 0)
            return fail();
        return start(outRate);
    }

    void close()
    {
        if (m_dec && m_open)
            mp3dec_ex_close(m_dec.get());
        m_dec.reset();
        m_open = false;
        m_sourceEnded = true;
        m_source.clear();
        m_pos = 0.0;
    }

    bool isOpen() const { return m_open; }

    // Writes up to count samples scaled by volume;
    // fewer only at the end of the track
    size_t read(int16_t* out, size_t count, float volume)
    {
        if (!m_open)
            return 0;
        size_t written = 0;
        for (; written < count; ++written)
        {
            size_t i0 = (size_t)m_pos;
            if (i0 + 1 >= m_source.size() && !pull())
            {
                if (i0 >= m_source.size())
                    break;
            }
            i0 = (size_t)m_pos;
            const size_t i1 = std::min(i0 + 1, m_source.size() - 1);
            const float frac = (float)(m_pos - (double)i0);
            float v = (m_source[i0] + (m_source[i1] - m_source[i0]) * frac) * volume * 32767.0f;
            v = std::clamp(v, -32768.0f, 32767.0f);
            out[written] = (int16_t)v;
            m_pos += m_step;
        }

        // Drop consumed source samples; the vector keeps its capacity
        const size_t consumed = std::min((size_t)m_pos, m_source.size());
        m_source.erase(m_source.begin(), m_source.begin() + (std::ptrdiff_t)consumed);
        m_pos -= (double)consumed;
        return written;
    }

private:
    static constexpr size_t kDecodeBlockFrames = 2048;

    bool fail()
    {
        m_dec.reset();
        m_open = false;
        return false;
    }

    bool start(unsigned int outRate)
    {
        m_open = true;
        m_channels = std::max(1, m_dec->info.channels);
        const int hz = m_dec->info.hz > 0 ? m_dec->info.hz : (int)outRate;
        m_step = (double)hz / (double)outRate;
        m_pos = 0.0;
        m_sourceEnded = false;
        m_source.clear();
        m_source.reserve(kDecodeBlockFrames * 2);
        m_decodeBuffer.resize(kDecodeBlockFrames * (size_t)m_channels);
        return true;
    }

    // Appends one decoded block to m_source as mono float;
    // false at the end of the stream
    bool pull()
    {
        if (m_sourceEnded)
            return false;
        const size_t samples = mp3dec_ex_read(m_dec.get(), m_decodeBuffer.data(), m_decodeBuffer.size());
        const size_t frames = samples / (size_t)m_channels;
        if (frames == 0)
        {
            m_sourceEnded = true;
            return false;
        }
        for (size_t f = 0; f < frames; ++f)
        {
            float sum = 0.0f;
            for (int c = 0; c < m_channels; ++c)
                sum += (float)m_decodeBuffer[f * (size_t)m_channels + (size_t)c];
            m_source.push_back(sum / (32768.0f * (float)m_channels));
        }
        return true;
    }

    std::unique_ptr<mp3dec_ex_t> m_dec;
    bool m_open = false;
    bool m_sourceEnded = true;
    int m_channels = 1;
    double m_step = 1.0;
    double m_pos = 0.0;                   // fractional read position in m_source
    std::vector<float> m_source;          // decoded mono samples not yet consumed
    std::vector<mp3d_sample_t> m_decodeBuffer;
};
