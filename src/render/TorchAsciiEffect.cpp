#include "TorchAsciiEffect.h"
#include "ShaderLoader.h"
#include "ShaderProgram.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Тот же формат, что и GlyphDef в AsciiEffect.cpp (8 байт, один на
// строку глифа, бит = пиксель) — независимая копия здесь, чтобы не
// тянуть зависимость между этими двумя файлами ради 4 констант.
struct GlyphDef { unsigned char rows[8]; };

// Те же самые битовые узоры, что и в уже проверенном рабочем референсе
// этого эффекта (см. большой комментарий в TorchAsciiEffect.h) — индекс
// в этом массиве это и есть направление (mode-1) из torch_ascii_edges.frag.
const GlyphDef kEdgeGlyphs[4] = {
    {{0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}}, // 0: '|'
    {{0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}}, // 1: '-'
    {{0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x00}}, // 2: '/'
    {{0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x00}}, // 3: '\'
};

} // namespace

bool TorchAsciiEffect::create()
{
    // ---- Полноэкранный квад (тот же приём, что и AsciiEffect::createQuad()) ----
    float verts[] = {
        -1.f, -1.f,  0.f, 0.f,
         1.f, -1.f,  1.f, 0.f,
         1.f,  1.f,  1.f, 1.f,

        -1.f, -1.f,  0.f, 0.f,
         1.f,  1.f,  1.f, 1.f,
        -1.f,  1.f,  0.f, 1.f,
    };
    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    const std::string vertSrc = ShaderLoader::LoadSource("assets/shaders/ascii_post.vert");

    auto buildProgram = [&](const char* fragPath, const char* label) -> GLuint {
        const std::string fragSrc = ShaderLoader::LoadSource(fragPath);
        GLuint vs = ShaderProgram::CompileShader(GL_VERTEX_SHADER, vertSrc.c_str(), label);
        GLuint fs = ShaderProgram::CompileShader(GL_FRAGMENT_SHADER, fragSrc.c_str(), label);
        return ShaderProgram::LinkProgram(vs, fs, label);
    };

    m_cellProgram  = buildProgram("assets/shaders/torch_ascii_edges.frag", "TorchAsciiCell");
    m_finalProgram = buildProgram("assets/shaders/torch_ascii_final.frag", "TorchAsciiFinal");

    if (!m_cellProgram || !m_finalProgram)
        return false;

    m_uCellSceneTex  = glGetUniformLocation(m_cellProgram, "sceneTex");
    m_uCellMaskTex   = glGetUniformLocation(m_cellProgram, "maskTex");
    m_uCellDepthTex  = glGetUniformLocation(m_cellProgram, "sceneDepthTex");
    m_uCellScreenRes = glGetUniformLocation(m_cellProgram, "screenResolution");
    m_uCellCellSize  = glGetUniformLocation(m_cellProgram, "cellSize");
    m_uCellNear      = glGetUniformLocation(m_cellProgram, "camNear");
    m_uCellFar       = glGetUniformLocation(m_cellProgram, "camFar");
    m_uCellThreshold = glGetUniformLocation(m_cellProgram, "edgeDepthThreshold");

    m_uFinalMaskTex      = glGetUniformLocation(m_finalProgram, "maskTex");
    m_uFinalCellInfoTex  = glGetUniformLocation(m_finalProgram, "cellInfoTex");
    m_uFinalEdgeFontTex  = glGetUniformLocation(m_finalProgram, "edgeFontTex");
    m_uFinalScreenRes    = glGetUniformLocation(m_finalProgram, "screenResolution");
    m_uFinalCellSize     = glGetUniformLocation(m_finalProgram, "cellSize");
    m_uFinalGlyphCount   = glGetUniformLocation(m_finalProgram, "edgeGlyphCount");
    m_uFinalAsciiColor   = glGetUniformLocation(m_finalProgram, "asciiColor");
    m_uFinalColorEnabled = glGetUniformLocation(m_finalProgram, "colorEnabled");
    m_uFinalFadeAlpha    = glGetUniformLocation(m_finalProgram, "fadeAlpha");

    return true;
}

void TorchAsciiEffect::generateEdgeFontAtlas(int cellSize)
{
    if (m_edgeFontTex)
    {
        glDeleteTextures(1, &m_edgeFontTex);
        m_edgeFontTex = 0;
    }

    const int glyphSize = std::max(1, cellSize);
    const int atlasW = glyphSize * kEdgeGlyphCount;
    const int atlasH = glyphSize;
    std::vector<unsigned char> pixels((size_t)atlasW * atlasH, 0);

    for (int level = 0; level < kEdgeGlyphCount; level++)
    {
        const GlyphDef& g = kEdgeGlyphs[level];
        for (int y = 0; y < glyphSize; y++)
        {
            int srcRow = (y * 8) / glyphSize;
            unsigned char rowBits = g.rows[srcRow];
            for (int x = 0; x < glyphSize; x++)
            {
                int srcCol = (x * 8) / glyphSize;
                bool on = (rowBits >> (7 - srcCol)) & 1;
                pixels[(size_t)y * atlasW + (level * glyphSize + x)] = on ? 255 : 0;
            }
        }
    }

    glGenTextures(1, &m_edgeFontTex);
    glBindTexture(GL_TEXTURE_2D, m_edgeFontTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasW, atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TorchAsciiEffect::destroy()
{
    if (m_cellInfoFBO) { glDeleteFramebuffers(1, &m_cellInfoFBO); m_cellInfoFBO = 0; }
    if (m_cellInfoTex) { glDeleteTextures(1, &m_cellInfoTex); m_cellInfoTex = 0; }
    if (m_edgeFontTex) { glDeleteTextures(1, &m_edgeFontTex); m_edgeFontTex = 0; }
    if (m_cellProgram)  { glDeleteProgram(m_cellProgram); m_cellProgram = 0; }
    if (m_finalProgram) { glDeleteProgram(m_finalProgram); m_finalProgram = 0; }
    if (m_quadVBO) { glDeleteBuffers(1, &m_quadVBO); m_quadVBO = 0; }
    if (m_quadVAO) { glDeleteVertexArrays(1, &m_quadVAO); m_quadVAO = 0; }
    m_texW = m_texH = m_cellSize = 0;
}

void TorchAsciiEffect::ensureSize(int windowWidth, int windowHeight, int cellSize)
{
    const int clampedCellSize = cellSize > 0 ? cellSize : 8;

    if (windowWidth == m_texW && windowHeight == m_texH &&
        clampedCellSize == m_cellSize && m_cellInfoFBO != 0)
        return; // уже нужного размера

    const bool cellSizeChanged = (clampedCellSize != m_cellSize) || (m_edgeFontTex == 0);

    m_texW = windowWidth;
    m_texH = windowHeight;
    m_cellSize = clampedCellSize;

    if (cellSizeChanged)
        generateEdgeFontAtlas(m_cellSize); // атлас генерируется под конкретный cellSize в пикселях

    const int cellsW = std::max(1, m_texW / m_cellSize);
    const int cellsH = std::max(1, m_texH / m_cellSize);

    if (m_cellInfoFBO) glDeleteFramebuffers(1, &m_cellInfoFBO);
    if (m_cellInfoTex) glDeleteTextures(1, &m_cellInfoTex);

    glGenTextures(1, &m_cellInfoTex);
    glBindTexture(GL_TEXTURE_2D, m_cellInfoTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, cellsW, cellsH, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &m_cellInfoFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_cellInfoFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_cellInfoTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "TorchAsciiEffect: cellInfo FBO incomplete!\n");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void TorchAsciiEffect::render(GLuint sceneColorTex, GLuint sceneMaskTex, GLuint sceneDepthTex,
                               float camNear, float camFar,
                               int windowWidth, int windowHeight, int cellSize,
                               bool colorEnabled, float fadeAlpha)
{
    if (!m_cellProgram || windowWidth <= 0 || windowHeight <= 0)
        return;
    // БАГФИКС ("штрихи факела видны поверх чёрного экрана во время
    // фейда") — экран уже полностью чёрный, штрихам всё равно не через
    // что "просвечивать". Дублирует discard в шейдере (см.
    // torch_ascii_final.frag) — здесь же экономим саму работу GPU
    // (оба прохода), а не только финальный фрагмент.
    if (fadeAlpha >= 1.0f)
        return;

    ensureSize(windowWidth, windowHeight, cellSize);

    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(m_quadVAO);

    // ---- Проход 1: кривизна глубины, сразу на уровне ASCII-ячейки ----
    const int cellsW = std::max(1, m_texW / m_cellSize);
    const int cellsH = std::max(1, m_texH / m_cellSize);

    glBindFramebuffer(GL_FRAMEBUFFER, m_cellInfoFBO);
    glViewport(0, 0, cellsW, cellsH);
    glUseProgram(m_cellProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneColorTex);
    glUniform1i(m_uCellSceneTex, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sceneMaskTex);
    glUniform1i(m_uCellMaskTex, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
    glUniform1i(m_uCellDepthTex, 2);

    glUniform2f(m_uCellScreenRes, (float)m_texW, (float)m_texH);
    glUniform1f(m_uCellCellSize, (float)m_cellSize);
    glUniform1f(m_uCellNear, camNear);
    glUniform1f(m_uCellFar, camFar);
    // Подобрано и подтверждено на реальном железе (было 0.0015, потом
    // 0.35 — обе догадки без визуальной проверки; это финальное
    // значение уже проверено глазами и работает нормально).
    glUniform1f(m_uCellThreshold, 0.0085f);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    // ---- Проход 2: подстановка символа, композит поверх готового кадра ----
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowWidth, windowHeight);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_finalProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneMaskTex);
    glUniform1i(m_uFinalMaskTex, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_cellInfoTex);
    glUniform1i(m_uFinalCellInfoTex, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_edgeFontTex);
    glUniform1i(m_uFinalEdgeFontTex, 2);

    glUniform2f(m_uFinalScreenRes, (float)windowWidth, (float)windowHeight);
    glUniform1f(m_uFinalCellSize, (float)m_cellSize);
    glUniform1f(m_uFinalGlyphCount, (float)kEdgeGlyphCount);
    // Тёплый "огненный" оттенок штрихов факела в цветном режиме —
    // подобрать на глаз в палитре игры.
    glUniform3f(m_uFinalAsciiColor, 1.0f, 0.75f, 0.30f);
    glUniform1f(m_uFinalColorEnabled, colorEnabled ? 1.0f : 0.0f);
    glUniform1f(m_uFinalFadeAlpha, fadeAlpha);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}
