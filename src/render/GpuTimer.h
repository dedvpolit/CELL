#pragma once
#include <GL/glew.h>

// GPU time per frame section, read back a few frames late so the CPU never waits on the GPU.
// Sections must not nest (GL_TIME_ELAPSED queries cannot overlap).
class GpuTimer {
public:
    enum Section { Scene, Ascii, Crt, SectionCount };

    void init()
    {
        glGenQueries(kFrames * SectionCount, &m_queries[0][0]);
        m_initialized = true;
    }

    void shutdown()
    {
        if (m_initialized)
            glDeleteQueries(kFrames * SectionCount, &m_queries[0][0]);
        m_initialized = false;
    }

    void begin(Section s)
    {
        if (!m_initialized)
            return;
        // Collect the result this slot held kFrames frames ago before reusing it.
        if (m_pending[m_frame][s])
        {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(m_queries[m_frame][s], GL_QUERY_RESULT, &ns);
            m_sumMs[s] += (double)ns * 1e-6;
            ++m_samples[s];
        }
        glBeginQuery(GL_TIME_ELAPSED, m_queries[m_frame][s]);
        m_pending[m_frame][s] = true;
    }

    void end()
    {
        if (m_initialized)
            glEndQuery(GL_TIME_ELAPSED);
    }

    void endFrame() { m_frame = (m_frame + 1) % kFrames; }

    // Average since the last reset; 0 until results arrive.
    double averageMs(Section s) const { return m_samples[s] ? m_sumMs[s] / m_samples[s] : 0.0; }

    void resetAverages()
    {
        for (int s = 0; s < SectionCount; ++s)
        {
            m_sumMs[s] = 0.0;
            m_samples[s] = 0;
        }
    }

private:
    static constexpr int kFrames = 4;
    GLuint m_queries[kFrames][SectionCount] = {};
    bool m_pending[kFrames][SectionCount] = {};
    double m_sumMs[SectionCount] = {};
    int m_samples[SectionCount] = {};
    int m_frame = 0;
    bool m_initialized = false;
};
