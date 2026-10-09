#include "AcerolaAscii.h"
#include "ShaderLoader.h"
#include "ShaderProgram.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const char* kVertexSrc = R"(#version 430 core
layout(location = 0) in vec2 aPos;
out vec2 vUV;
void main() {
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// 8x8 bitmaps, bit 7 = leftmost pixel, row 0 = top. Order must match acerola_ascii.comp: strokes
// for directions 0..3, then the fill ramp by strictly increasing ink (kFillLevels entries). The
// fill has no line-shaped glyphs, so strokes stay recognizable as edges
struct Glyph { unsigned char rows[8]; };
const Glyph kGlyphs[] = {
    {{0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}}, // '|'  direction 0
    {{0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}}, // '-'  direction 1
    {{0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x00}}, // '\'  direction 2
    {{0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x00}}, // '/'  direction 3
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}}, // ' '  ink 0
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00}}, // 1-pixel dot  ink 1
    {{0x00,0x00,0x10,0x00,0x00,0x10,0x00,0x00}}, // ':'  ink 2 (single-pixel dots: 2x2 dots read as '|' once scaled)
    {{0x00,0x00,0x10,0x00,0x00,0x10,0x20,0x00}}, // ';'  ink 3
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18}}, // '.'  ink 4
    {{0x00,0x42,0x42,0x24,0x24,0x18,0x00,0x00}}, // 'v'  ink 10
    {{0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00}}, // '*'  ink 14
    {{0x00,0x6C,0x76,0x60,0x60,0x60,0x00,0x00}}, // 'r'  ink 15
    {{0x3C,0x66,0x06,0x1C,0x18,0x00,0x18,0x00}}, // '?'  ink 17
    {{0x00,0x3C,0x66,0x60,0x66,0x3C,0x00,0x00}}, // 'c'  ink 18
    {{0x00,0x3C,0x66,0x66,0x66,0x3C,0x00,0x00}}, // 'o'  ink 20
    {{0x00,0x6C,0x76,0x66,0x66,0x66,0x00,0x00}}, // 'n'  ink 21
    {{0x00,0x66,0x3C,0x18,0x3C,0x66,0xC3,0x00}}, // 'X'  ink 22
    {{0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}}, // 'P'  ink 24
    {{0x00,0x3C,0x66,0x3C,0x38,0x67,0x3E,0x00}}, // '&'  ink 25
    {{0x00,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}}, // 'H'  ink 26
    {{0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}}, // 'O'  ink 28
    {{0x00,0xC3,0xC3,0xDB,0xFF,0x66,0x66,0x00}}, // 'W'  ink 30
    {{0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x62,0x3C}}, // '@'  ink 31
    {{0x00,0xC3,0xE7,0xFF,0xDB,0xC3,0xC3,0x00}}, // 'M'  ink 32
    {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}}, // full block  ink 64
};
constexpr int kGlyphCount = sizeof(kGlyphs) / sizeof(kGlyphs[0]);

GLuint makeTex(int w, int h, GLenum internalFmt, GLenum fmt, GLenum type) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

GLuint makeFBO(GLuint tex) {
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("AcerolaAscii: framebuffer incomplete");
    return fbo;
}

GLuint linkFragment(GLuint vs, const char* path, const char* label) {
    const std::string src = ShaderLoader::LoadSource(path);
    GLuint fs = ShaderProgram::CompileShader(GL_FRAGMENT_SHADER, src.c_str(), label);
    return ShaderProgram::LinkProgram(vs, fs, label);
}

GLuint linkCompute(const char* path, const char* label) {
    const std::string src = ShaderLoader::LoadSource(path);
    GLuint cs = ShaderProgram::CompileShader(GL_COMPUTE_SHADER, src.c_str(), label);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, cs);
    glLinkProgram(prog);
    glDeleteShader(cs);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        glDeleteProgram(prog);
        throw std::runtime_error(std::string(label) + " program failed to link:\n" + log);
    }
    return prog;
}

} // namespace

void AcerolaAscii::init() {
    const float quad[] = { -1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1 };
    glGenVertexArrays(1, &m_quadVAO);
    glBindVertexArray(m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    m_vertexShader = ShaderProgram::CompileShader(GL_VERTEX_SHADER, kVertexSrc, "AcerolaAscii");

    auto loc = [](GLuint p, const char* n) { return glGetUniformLocation(p, n); };

    m_blurH.prog = linkFragment(m_vertexShader, "assets/shaders/acerola_blur_h.frag", "AcerolaAscii/BlurH");
    m_blurH.sceneTex = loc(m_blurH.prog, "sceneTex");

    m_blurV.prog = linkFragment(m_vertexShader, "assets/shaders/acerola_blur_v_dog.frag", "AcerolaAscii/BlurVDog");
    m_blurV.blurTex = loc(m_blurV.prog, "blurTex");
    m_blurV.texel = loc(m_blurV.prog, "texel");
    m_blurV.tau = loc(m_blurV.prog, "tau");
    m_blurV.threshold = loc(m_blurV.prog, "threshold");

    m_edges.prog = linkFragment(m_vertexShader, "assets/shaders/acerola_edges.frag", "AcerolaAscii/Edges");
    m_edges.depthTex = loc(m_edges.prog, "depthTex");
    m_edges.sceneTex = loc(m_edges.prog, "sceneTex");
    m_edges.dogTex = loc(m_edges.prog, "dogTex");
    m_edges.texel = loc(m_edges.prog, "texel");
    m_edges.nearPlane = loc(m_edges.prog, "nearPlane");
    m_edges.farPlane = loc(m_edges.prog, "farPlane");
    m_edges.depthThreshold = loc(m_edges.prog, "depthThreshold");
    m_edges.normalThreshold = loc(m_edges.prog, "normalThreshold");

    m_sobel.prog = linkFragment(m_vertexShader, "assets/shaders/acerola_sobel.frag", "AcerolaAscii/Sobel");
    m_sobel.edgesTex = loc(m_sobel.prog, "edgesTex");

    m_comp.prog = linkCompute("assets/shaders/acerola_ascii.comp", "AcerolaAscii/Compute");
    m_comp.sceneTex = loc(m_comp.prog, "sceneTex");
    m_comp.directionTex = loc(m_comp.prog, "directionTex");
    m_comp.cellSize = loc(m_comp.prog, "cellSize");
    m_comp.outSize = loc(m_comp.prog, "outSize");
    m_comp.sceneSize = loc(m_comp.prog, "sceneSize");
    m_comp.mipLevel = loc(m_comp.prog, "mipLevel");
    m_comp.lensStrength = loc(m_comp.prog, "lensStrength");
    m_comp.edgeThreshold = loc(m_comp.prog, "edgeThreshold");
    m_comp.colorEnabled = loc(m_comp.prog, "colorEnabled");
    m_comp.glitchActive = loc(m_comp.prog, "glitchActive");
    m_comp.glitchUV = loc(m_comp.prog, "glitchUV");
    m_comp.glitchRadiusCells = loc(m_comp.prog, "glitchRadiusCells");

    m_initialized = true;
    buildGlyphAtlas();
}

void AcerolaAscii::buildGlyphAtlas() {
    if (m_glyphTex) glDeleteTextures(1, &m_glyphTex);
    const int n = m_cellSize;
    const int atlasW = n * kGlyphCount;
    std::vector<unsigned char> pixels((size_t)atlasW * n, 0);
    for (int g = 0; g < kGlyphCount; ++g) {
        for (int y = 0; y < n; ++y) {
            const unsigned char bits = kGlyphs[g].rows[(y * 8) / n];
            for (int x = 0; x < n; ++x)
                pixels[(size_t)y * atlasW + g * n + x] = ((bits >> (7 - (x * 8) / n)) & 1) ? 255 : 0;
        }
    }
    m_glyphTex = makeTex(atlasW, n, GL_R8, GL_RED, GL_UNSIGNED_BYTE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, atlasW, n, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
}

void AcerolaAscii::setCellSize(int cellSize) {
    if (cellSize == m_cellSize && m_glyphTex) return;
    m_cellSize = cellSize;
    if (!m_initialized) return;
    buildGlyphAtlas();
    destroyTargets();
}

void AcerolaAscii::createTargets(int sceneW, int sceneH, int outW, int outH) {
    m_sceneW = sceneW; m_sceneH = sceneH; m_outW = outW; m_outH = outH;
    m_cellsX = (outW + m_cellSize - 1) / m_cellSize;
    m_cellsY = (outH + m_cellSize - 1) / m_cellSize;

    m_blurTex = makeTex(sceneW, sceneH, GL_RG16F, GL_RG, GL_FLOAT);
    m_dogTex = makeTex(sceneW, sceneH, GL_R8, GL_RED, GL_UNSIGNED_BYTE);
    m_edgesTex = makeTex(sceneW, sceneH, GL_R8, GL_RED, GL_UNSIGNED_BYTE);
    m_directionTex = makeTex(sceneW, sceneH, GL_R8, GL_RED, GL_UNSIGNED_BYTE);
    m_outTex = makeTex(m_cellsX, m_cellsY, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);

    m_blurFBO = makeFBO(m_blurTex);
    m_dogFBO = makeFBO(m_dogTex);
    m_edgesFBO = makeFBO(m_edgesTex);
    m_directionFBO = makeFBO(m_directionTex);
}

void AcerolaAscii::destroyTargets() {
    if (!m_outTex) return;
    const GLuint texs[] = { m_blurTex, m_dogTex, m_edgesTex, m_directionTex, m_outTex };
    glDeleteTextures(5, texs);
    const GLuint fbos[] = { m_blurFBO, m_dogFBO, m_edgesFBO, m_directionFBO };
    glDeleteFramebuffers(4, fbos);
    m_outTex = 0;
    m_sceneW = m_sceneH = m_outW = m_outH = 0;
}

void AcerolaAscii::shutdown() {
    if (!m_initialized) return;
    destroyTargets();
    glDeleteTextures(1, &m_glyphTex);
    const GLuint progs[] = { m_blurH.prog, m_blurV.prog, m_edges.prog, m_sobel.prog, m_comp.prog };
    for (GLuint p : progs) glDeleteProgram(p);
    glDeleteShader(m_vertexShader);
    glDeleteVertexArrays(1, &m_quadVAO);
    glDeleteBuffers(1, &m_quadVBO);
    m_glyphTex = 0;
    m_initialized = false;
}

GLuint AcerolaAscii::run(GLuint sceneColorTex, GLuint sceneDepthTex, int sceneW, int sceneH,
                         int outW, int outH, const FrameParams& p) {
    if (outW <= 0 || outH <= 0 || sceneW <= 0 || sceneH <= 0) return 0;
    if (!m_outTex || sceneW != m_sceneW || sceneH != m_sceneH || outW != m_outW || outH != m_outH) {
        destroyTargets();
        createTargets(sceneW, sceneH, outW, outH);
    }

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glBindVertexArray(m_quadVAO);
    glViewport(0, 0, m_sceneW, m_sceneH);

    auto bind = [](int unit, GLuint tex) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, tex);
    };
    auto pass = [](GLuint fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };

    bind(0, sceneColorTex);
    glUseProgram(m_blurH.prog);
    glUniform1i(m_blurH.sceneTex, 0);
    pass(m_blurFBO);

    bind(0, m_blurTex);
    glUseProgram(m_blurV.prog);
    glUniform1i(m_blurV.blurTex, 0);
    glUniform2f(m_blurV.texel, 1.0f / m_sceneW, 1.0f / m_sceneH);
    glUniform1f(m_blurV.tau, 1.0f);
    glUniform1f(m_blurV.threshold, 0.005f);
    pass(m_dogFBO);

    bind(0, sceneDepthTex);
    bind(1, sceneColorTex);
    bind(2, m_dogTex);
    glUseProgram(m_edges.prog);
    glUniform1i(m_edges.depthTex, 0);
    glUniform1i(m_edges.sceneTex, 1);
    glUniform1i(m_edges.dogTex, 2);
    glUniform2f(m_edges.texel, 1.0f / m_sceneW, 1.0f / m_sceneH);
    glUniform1f(m_edges.nearPlane, p.nearPlane);
    glUniform1f(m_edges.farPlane, p.farPlane);
    glUniform1f(m_edges.depthThreshold, 0.12f);
    glUniform1f(m_edges.normalThreshold, 0.18f);
    pass(m_edgesFBO);

    bind(0, m_edgesTex);
    glUseProgram(m_sobel.prog);
    glUniform1i(m_sobel.edgesTex, 0);
    pass(m_directionFBO);

    bind(0, sceneColorTex);
    bind(1, m_directionTex);
    glUseProgram(m_comp.prog);
    glUniform1i(m_comp.sceneTex, 0);
    glUniform1i(m_comp.directionTex, 1);
    glUniform1i(m_comp.cellSize, m_cellSize);
    glUniform2i(m_comp.outSize, m_outW, m_outH);
    glUniform2i(m_comp.sceneSize, m_sceneW, m_sceneH);
    glUniform1f(m_comp.mipLevel, std::log2(std::max(1.0f, m_cellSize * (float)m_sceneW / (float)m_outW)));
    glUniform1f(m_comp.lensStrength, p.lensStrength);
    glUniform1f(m_comp.edgeThreshold, edgeThreshold);
    glUniform1f(m_comp.colorEnabled, p.colorEnabled ? 1.0f : 0.0f);
    glUniform1f(m_comp.glitchActive, p.glitchActive ? 1.0f : 0.0f);
    glUniform2f(m_comp.glitchUV, p.glitchU, p.glitchV);
    glUniform1f(m_comp.glitchRadiusCells, p.glitchRadiusCells);
    glBindImageTexture(0, m_outTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glDispatchCompute((GLuint)m_cellsX, (GLuint)m_cellsY, 1);
    // ascii_post.frag samples the result;
    // the next frame's dispatch overwrites it
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindVertexArray(0);
    return m_outTex;
}
