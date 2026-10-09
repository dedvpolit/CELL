#include "CrtEffect.h"
#include "ShaderLoader.h"
#include "ShaderProgram.h"
#include <algorithm>
#include <cmath>

namespace {

const char* kVertexSrc = R"(#version 430 core
layout(location = 0) in vec2 aPos;
out vec2 vUV;
void main() {
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// Bloom is computed at a quarter of the window size
constexpr int kBloomDivisor = 4;

// Seconds for the phosphor afterglow to fall to ~37%
constexpr float kPersistenceTau = 0.05f;

GLuint MakeTarget(int w, int h, GLuint& fbo)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    return tex;
}

GLuint LinkFragment(GLuint vs, const char* path, const char* label)
{
    const std::string src = ShaderLoader::LoadSource(path);
    const GLuint fs = ShaderProgram::CompileShader(GL_FRAGMENT_SHADER, src.c_str(), label);
    const GLuint prog = ShaderProgram::LinkProgram(vs, fs, label);
    glDeleteShader(fs);
    return prog;
}

} // namespace

void CrtEffect::init()
{
    const float quad[] = { -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1 };
    glGenVertexArrays(1, &m_quadVao);
    glBindVertexArray(m_quadVao);
    glGenBuffers(1, &m_quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    const GLuint vs = ShaderProgram::CompileShader(GL_VERTEX_SHADER, kVertexSrc, "CrtEffect");
    m_phosphor.prog = LinkFragment(vs, "assets/shaders/crt_phosphor.frag", "CrtEffect/Phosphor");
    m_phosphor.current = glGetUniformLocation(m_phosphor.prog, "currentTex");
    m_phosphor.previous = glGetUniformLocation(m_phosphor.prog, "previousTex");
    m_phosphor.decay = glGetUniformLocation(m_phosphor.prog, "decay");

    m_blur.prog = LinkFragment(vs, "assets/shaders/crt_blur.frag", "CrtEffect/Blur");
    m_blur.source = glGetUniformLocation(m_blur.prog, "sourceTex");
    m_blur.texelStep = glGetUniformLocation(m_blur.prog, "texelStep");

    m_final.prog = LinkFragment(vs, "assets/shaders/crt_final.frag", "CrtEffect/Final");
    m_final.phosphor = glGetUniformLocation(m_final.prog, "phosphorTex");
    m_final.bloom = glGetUniformLocation(m_final.prog, "bloomTex");
    m_final.resolution = glGetUniformLocation(m_final.prog, "resolution");
    m_final.time = glGetUniformLocation(m_final.prog, "time");
    glDeleteShader(vs);
}

void CrtEffect::shutdown()
{
    destroyTargets();
    const GLuint progs[] = { m_phosphor.prog, m_blur.prog, m_final.prog };
    for (GLuint p : progs)
        if (p) glDeleteProgram(p);
    if (m_quadVbo) glDeleteBuffers(1, &m_quadVbo);
    if (m_quadVao) glDeleteVertexArrays(1, &m_quadVao);
    m_quadVbo = m_quadVao = 0;
}

void CrtEffect::createTargets(int width, int height)
{
    m_width = width;
    m_height = height;
    m_captureTex = MakeTarget(width, height, m_captureFbo);
    const int bw = std::max(1, width / kBloomDivisor), bh = std::max(1, height / kBloomDivisor);
    for (int i = 0; i < 2; ++i)
        m_bloomTex[i] = MakeTarget(bw, bh, m_bloomFbo[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void CrtEffect::destroyTargets()
{
    destroyPhosphorTargets();
    if (!m_captureTex)
        return;
    const GLuint texs[] = { m_captureTex, m_bloomTex[0], m_bloomTex[1] };
    const GLuint fbos[] = { m_captureFbo, m_bloomFbo[0], m_bloomFbo[1] };
    glDeleteTextures(3, texs);
    glDeleteFramebuffers(3, fbos);
    m_captureTex = m_captureFbo = 0;
    m_bloomTex[0] = m_bloomTex[1] = m_bloomFbo[0] = m_bloomFbo[1] = 0;
    m_width = m_height = 0;
}

void CrtEffect::createPhosphorTargets()
{
    for (int i = 0; i < 2; ++i)
        m_phosphorTex[i] = MakeTarget(m_width, m_height, m_phosphorFbo[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_phosphorValid = false;
}

void CrtEffect::destroyPhosphorTargets()
{
    if (!m_phosphorTex[0])
        return;
    glDeleteTextures(2, m_phosphorTex);
    glDeleteFramebuffers(2, m_phosphorFbo);
    m_phosphorTex[0] = m_phosphorTex[1] = m_phosphorFbo[0] = m_phosphorFbo[1] = 0;
    m_phosphorValid = false;
}

GLuint CrtEffect::beginFrame(int width, int height)
{
    if (!m_enabled || width <= 0 || height <= 0)
    {
        // Freed when off, so the effect costs no memory unless used (meow-meow-meow)
        destroyTargets();
        return 0;
    }
    if (width != m_width || height != m_height)
    {
        destroyTargets();
        createTargets(width, height);
    }
    return m_captureFbo;
}

void CrtEffect::present(float time, float deltaTime)
{
    if (!m_enabled || !m_captureTex)
        return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(m_quadVao);
    auto bindTex = [](int unit, GLuint tex) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, tex);
    };

    // 1. Phosphor: the new frame, or the previous one fading out where it was brighter
    GLuint screenTex = m_captureTex;
    if (m_persistence)
    {
        if (!m_phosphorTex[0])
            createPhosphorTargets();
        const int next = 1 - m_phosphorCurrent;
        glBindFramebuffer(GL_FRAMEBUFFER, m_phosphorFbo[next]);
        glViewport(0, 0, m_width, m_height);
        glUseProgram(m_phosphor.prog);
        bindTex(0, m_captureTex);
        bindTex(1, m_phosphorTex[m_phosphorCurrent]);
        glUniform1i(m_phosphor.current, 0);
        glUniform1i(m_phosphor.previous, 1);
        glUniform1f(m_phosphor.decay, m_phosphorValid ? std::exp(-deltaTime / kPersistenceTau) : 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_phosphorCurrent = next;
        m_phosphorValid = true;
        screenTex = m_phosphorTex[m_phosphorCurrent];
    }
    else
    {
        destroyPhosphorTargets();
    }

    // 2. Glow: separable blur at quarter resolution
    const int bw = std::max(1, m_width / kBloomDivisor), bh = std::max(1, m_height / kBloomDivisor);
    glViewport(0, 0, bw, bh);
    glUseProgram(m_blur.prog);
    glUniform1i(m_blur.source, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbo[0]);
    bindTex(0, screenTex);
    glUniform2f(m_blur.texelStep, 1.0f / (float)bw, 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbo[1]);
    bindTex(0, m_bloomTex[0]);
    glUniform2f(m_blur.texelStep, 0.0f, 1.0f / (float)bh);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 3. Screen: curvature, glow, scanlines, noise
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    glUseProgram(m_final.prog);
    bindTex(0, screenTex);
    bindTex(1, m_bloomTex[1]);
    glUniform1i(m_final.phosphor, 0);
    glUniform1i(m_final.bloom, 1);
    glUniform2f(m_final.resolution, (float)m_width, (float)m_height);
    glUniform1f(m_final.time, time);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}
