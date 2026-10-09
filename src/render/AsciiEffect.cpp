#include "AsciiEffect.h"
#include "ShaderLoader.h"
#include "ShaderProgram.h"
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

void AsciiEffect::createFBO(int w, int h) {
    m_fboW = w; m_fboH = h;

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_sceneTex);
    glBindTexture(GL_TEXTURE_2D, m_sceneTex);
    // Alpha 0 marks wall torches, which AcerolaAscii keeps out of edge detection. The mip chain
    // (rebuilt in end() for the ASCII view) gives AcerolaAscii the average color of each glyph
    // cell. It is allocated here so the texture is complete in views that never rebuild it.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneTex, 0);

    glGenTextures(1, &m_depthTex);
    glBindTexture(GL_TEXTURE_2D, m_depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "AsciiEffect: FBO incomplete!\n");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void AsciiEffect::destroyFBO() {
    if (m_sceneTex)     glDeleteTextures(1, &m_sceneTex);
    if (m_depthTex)     glDeleteTextures(1, &m_depthTex);
    if (m_fbo)          glDeleteFramebuffers(1, &m_fbo);
    m_sceneTex = m_depthTex = m_fbo = 0;
}

// 8x8 bitmap glyphs.
struct GlyphDef { unsigned char rows[8]; };

// Fixed glyph set for UI text, HUD and the compass; shaders refer to some indices directly.
static const GlyphDef s_uiGlyphs[] = {
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}}, // 0:  empty
    {{0x24,0x24,0x7E,0x24,0x7E,0x24,0x24,0x00}}, // 1:  wall       '#'
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18}}, // 2:  floor      '.'
    {{0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00}}, // 3:  torch      '*'
    {{0x18,0x3C,0x7E,0x18,0x18,0x18,0x18,0x00}}, // 4:  player N   '^'
    {{0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x00}}, // 5:  player NE  '/'
    {{0x08,0x0C,0xFE,0x0C,0x08,0x00,0x00,0x00}}, // 6:  player E   '>'
    {{0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x00}}, // 7:  player SE  '\'
    {{0x18,0x18,0x18,0x18,0x7E,0x3C,0x18,0x00}}, // 8:  player S   'v'
    {{0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x00}}, // 9:  player SW  '/' (same glyph as NE)
    {{0x10,0x30,0x7F,0x30,0x10,0x00,0x00,0x00}}, // 10: player W   '<'
    {{0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x00}}, // 11: player NW  '\' (same glyph as SE)
    {{0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}}, // 12: circle frame 'O'

    // Stamina bar frame (see AsciiEffect::end() / setStamina())
    {{0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0x00}}, // 13: horizontal border '='
    {{0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18}}, // 14: vertical border   '|'
    {{0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}}, // 15: frame corner      '+'
    {{0x00,0x18,0x3C,0x3C,0x18,0x18,0x08,0x00}}, // 16: big drip (blood streak)
    {{0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00}}, // 17: small drip (blood streak)

    // Menu letters; indices are stable.
    {{0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00}}, // 18: 'A'
    {{0x00,0x3C,0x66,0x60,0x66,0x3C,0x00,0x00}}, // 19: 'C'
    {{0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00}}, // 20: 'E'
    {{0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00}}, // 21: 'I'
    {{0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}}, // 22: 'L'
    {{0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00}}, // 23: 'R'
    {{0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}}, // 24: 'S'
    {{0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}}, // 25: 'T'
    {{0xC3,0x66,0x3C,0x18,0x3C,0x66,0xC3,0x00}}, // 26: 'X'

    // Equally dense patterns for the ASCII-art title, so big letters are not solid blocks.
    {{0xFF,0x99,0xFF,0x99,0xFF,0x99,0xFF,0x99}}, // 27
    {{0xFF,0xE7,0xFF,0xE7,0xFF,0xE7,0xFF,0xE7}}, // 28
    {{0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x62,0x3C}}, // 29 ('@'-like)
    {{0x66,0xFF,0xDB,0xFF,0xFF,0xDB,0xFF,0x66}}, // 30
    {{0xF0,0x0F,0xF0,0x0F,0xF0,0x0F,0xF0,0x0F}}, // 31 (diagonal stripes)
    {{0x0F,0xF0,0x0F,0xF0,0x0F,0xF0,0x0F,0xF0}}, // 32 (diagonal stripes, inverted)
    {{0xEF,0xDB,0xBD,0x7E,0x7E,0xBD,0xDB,0xEF}}, // 33

    // Remaining letters for running text.
    {{0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}}, // 34: 'B'
    {{0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}}, // 35: 'D'
    {{0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00}}, // 36: 'F'
    {{0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00}}, // 37: 'G'
    {{0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}}, // 38: 'H'
    {{0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00}}, // 39: 'J'
    {{0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}}, // 40: 'K'
    {{0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}}, // 41: 'M'
    {{0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00}}, // 42: 'N'
    {{0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}}, // 43: 'O'
    {{0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}}, // 44: 'P'
    {{0x3C,0x66,0x66,0x66,0x66,0x6C,0x36,0x00}}, // 45: 'Q'
    {{0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}}, // 46: 'U'
    {{0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}}, // 47: 'V'
    {{0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}}, // 48: 'W'
    {{0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}}, // 49: 'Y'
    {{0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}}, // 50: 'Z'

    {{0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}}, // 51: '0'
    {{0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}}, // 52: '1'
    {{0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00}}, // 53: '2'
    {{0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}}, // 54: '3'
    {{0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00}}, // 55: '4'
    {{0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}}, // 56: '5'
    {{0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}}, // 57: '6'
    {{0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}}, // 58: '7'
    {{0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}}, // 59: '8'
    {{0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}}, // 60: '9'

    {{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}}, // 61: '.'
    {{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}}, // 62: ','
    {{0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}}, // 63: '\'' (apostrophe)
    {{0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}}, // 64: '-'
    {{0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}}, // 65: ':'
    {{0x3C,0x66,0x0C,0x18,0x18,0x00,0x18,0x00}}, // 66: '?'
    {{0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00}}, // 67: '!'

    // HUD item icons have their own indices. 68: torch.
    {{0x54,0x54,0x54,0x7C,0x10,0x10,0x10,0x38}}, // 68: torch icon (trident)

    // 69: diary.
    {{0x00,0x7E,0x5A,0x5A,0x5A,0x5A,0x7E,0x00}}, // 69: diary (open book)
};
static const int s_uiGlyphCount = sizeof(s_uiGlyphs) / sizeof(s_uiGlyphs[0]);

// Index map of s_uiGlyphs[]; the shader uses some indices as literals, so keep them in sync.
//   0 empty, 1 fill ('#'), 13 hline, 14 vline, 15 corner, 16/17 big/small drip (stamina frame)
//   18-26 and 34-50: letters A-Z (GLYPH_* in ui/UiGlyphs.h); 27-33: dense fillers for big text
//   51-60 digits, 61-67 punctuation; 68 torch icon, 69 diary icon

// Baked at the native 8x8 and sampled by UV with NEAREST at any cell size. A second resampling step
// (bake at one cell size, draw at another) would drop one-pixel strokes such as '-'.
void AsciiEffect::generateUiFontAtlas() {
    m_uiGlyphCount = s_uiGlyphCount;

    const int glyphSize = 8;
    const int atlasW = glyphSize * m_uiGlyphCount;
    const int atlasH = glyphSize;
    std::vector<unsigned char> pixels(atlasW * atlasH, 0);

    for (int level = 0; level < m_uiGlyphCount; level++) {
        const GlyphDef& g = s_uiGlyphs[level];
        for (int y = 0; y < glyphSize; y++)
            for (int x = 0; x < glyphSize; x++)
                pixels[y * atlasW + level * glyphSize + x] = ((g.rows[y] >> (7 - x)) & 1) ? 255 : 0;
    }

    // Re-specified in place: the compass keeps this texture name from startup.
    if (!m_uiFontTex) glGenTextures(1, &m_uiFontTex);
    glBindTexture(GL_TEXTURE_2D, m_uiFontTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasW, atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void AsciiEffect::createQuad() {
    float verts[] = {
        // pos        // uv
        -1.f, -1.f,   0.f, 0.f,
         1.f, -1.f,   1.f, 0.f,
         1.f,  1.f,   1.f, 1.f,

        -1.f, -1.f,   0.f, 0.f,
         1.f,  1.f,   1.f, 1.f,
        -1.f,  1.f,   0.f, 1.f,
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
}

bool AsciiEffect::init(int sceneWidth, int sceneHeight, int cellSize) {
    m_cellSize = cellSize;
    createFBO(sceneWidth, sceneHeight);
    generateUiFontAtlas();
    createQuad();

    const std::string vertSrc = ShaderLoader::LoadSource("assets/shaders/ascii_post.vert");
    const std::string fragSrc = ShaderLoader::LoadSource("assets/shaders/ascii_post.frag");
    GLuint vs = ShaderProgram::CompileShader(GL_VERTEX_SHADER, vertSrc.c_str(), "AsciiEffect");
    GLuint fs = ShaderProgram::CompileShader(GL_FRAGMENT_SHADER, fragSrc.c_str(), "AsciiEffect");
    m_program = ShaderProgram::LinkProgram(vs, fs, "AsciiEffect");
    cacheUniformLocations();
    m_acerola.init();
    m_acerola.setCellSize(m_cellSize);
    m_uniAcerolaCellTex = glGetUniformLocation(m_program, "sceneCellTex");
    m_uniAcerolaGlyphTex = glGetUniformLocation(m_program, "sceneGlyphTex");
    m_uniRenderView = glGetUniformLocation(m_program, "renderView");
    m_uniDebugSceneTex = glGetUniformLocation(m_program, "debugSceneTex");
    m_uniDebugEdgesTex = glGetUniformLocation(m_program, "debugEdgesTex");
    return m_program != 0;
}

void AsciiEffect::cacheUniformLocations() {
    if (!m_program) return;
    m_uniScreenResolution    = glGetUniformLocation(m_program, "screenResolution");
    m_uniCellSize            = glGetUniformLocation(m_program, "cellSize");
    m_uniUiFontTex      = glGetUniformLocation(m_program, "uiFontTex");
    m_uniUiGlyphCount   = glGetUniformLocation(m_program, "uiGlyphCount");
    m_uniStaminaFrac         = glGetUniformLocation(m_program, "staminaFrac");
    m_uniStaminaAlpha        = glGetUniformLocation(m_program, "staminaAlpha");
    m_uniHealthFrac          = glGetUniformLocation(m_program, "healthFrac");
    m_uniUiTex               = glGetUniformLocation(m_program, "uiTex");
    m_uniUiCols              = glGetUniformLocation(m_program, "uiCols");
    m_uniUiRows              = glGetUniformLocation(m_program, "uiRows");
    m_uniUiEnabled           = glGetUniformLocation(m_program, "uiEnabled");
    m_uniFadeAlpha           = glGetUniformLocation(m_program, "fadeAlpha");
    m_uniColorEnabled        = glGetUniformLocation(m_program, "colorEnabled");
    m_uniLensEffectEnabled   = glGetUniformLocation(m_program, "lensEffectEnabled");
    m_uniTime                = glGetUniformLocation(m_program, "uTime");
}

void AsciiEffect::shutdown() {
    m_acerola.shutdown();
    destroyFBO();
    if (m_uiFontTex) glDeleteTextures(1, &m_uiFontTex);
    if (m_uiOverlayTex) glDeleteTextures(1, &m_uiOverlayTex);
    if (m_program) glDeleteProgram(m_program);
    if (m_quadVBO) glDeleteBuffers(1, &m_quadVBO);
    if (m_quadVAO) glDeleteVertexArrays(1, &m_quadVAO);
}

void AsciiEffect::setWallGlitch(bool active, float u, float v, float radiusCells)
{
    m_glitchActive = active;
    m_glitchU = u;
    m_glitchV = v;
    m_glitchRadiusCells = radiusCells;
}

void AsciiEffect::setStamina(float fraction01, bool enabled) {
    m_staminaFrac = fraction01;
    // m_staminaAlpha eases toward the target in end().
    m_staminaEnabledTarget = enabled;
}

void AsciiEffect::setHealth(float fraction01) {
    m_healthFrac = fraction01;
}

void AsciiEffect::setUserCellSize(int cellSize) {
    cellSize = std::clamp(cellSize, kMinCellSize, kMaxCellSize);
    if (cellSize == m_cellSize) return;

    m_cellSize = cellSize;
    m_acerola.setCellSize(m_cellSize);
}

void AsciiEffect::setUIOverlay(bool enabled, const std::vector<unsigned char>& grid, int cols, int rows) {
    m_uiOverlayEnabled = enabled;

    if (!enabled) {
        return;
    }

    m_uiOverlayCols = cols;
    m_uiOverlayRows = rows;

    if (cols <= 0 || rows <= 0 || (int)grid.size() < cols * rows) {
        m_uiOverlayEnabled = false;
        return;
    }

    if (!m_uiOverlayTex) {
        glGenTextures(1, &m_uiOverlayTex);
        glBindTexture(GL_TEXTURE_2D, m_uiOverlayTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, m_uiOverlayTex);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Same size: update in place instead of reallocating every frame while the title animates.
    if (cols == m_uiOverlayTexW && rows == m_uiOverlayTexH) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RED, GL_UNSIGNED_BYTE, grid.data());
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, cols, rows, 0, GL_RED, GL_UNSIGNED_BYTE, grid.data());
        m_uiOverlayTexW = cols;
        m_uiOverlayTexH = rows;
    }
}

void AsciiEffect::setFadeAlpha(float alpha01) {
    m_fadeAlpha = std::clamp(alpha01, 0.0f, 1.0f);
}

void AsciiEffect::begin() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_fboW, m_fboH);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void AsciiEffect::end(int windowWidth, int windowHeight) {
    // Minimized window: nothing to draw.
    if (windowWidth <= 0 || windowHeight <= 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_windowFramebuffer);
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_windowFramebuffer);
    glViewport(0, 0, windowWidth, windowHeight);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);

    // Views without glyphs skip the ASCII pipeline, including the mip chain only it reads.
    const bool needsAscii = m_renderView == RenderView::Ascii || m_renderView == RenderView::Edges;
    if (needsAscii) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sceneTex);
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    AcerolaAscii::FrameParams params;
    params.nearPlane = m_nearPlane;
    params.farPlane = m_farPlane;
    params.colorEnabled = m_colorEnabled;
    params.lensStrength = m_lensEffectEnabled ? 0.05f : 0.0f;
    params.glitchActive = m_glitchActive;
    params.glitchU = m_glitchU;
    params.glitchV = m_glitchV;
    params.glitchRadiusCells = m_glitchRadiusCells;
    const GLuint sceneAscii = needsAscii
        ? m_acerola.run(m_sceneTex, m_depthTex, m_fboW, m_fboH, windowWidth, windowHeight, params)
        : 0;
    glBindFramebuffer(GL_FRAMEBUFFER, m_windowFramebuffer);
    glViewport(0, 0, windowWidth, windowHeight);

    glUseProgram(m_program);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sceneAscii);
    glUniform1i(m_uniAcerolaCellTex, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_acerola.glyphAtlas());
    glUniform1i(m_uniAcerolaGlyphTex, 2);

    glUniform1i(m_uniRenderView, (int)m_renderView);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_sceneTex);
    glUniform1i(m_uniDebugSceneTex, 5);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, m_acerola.edgesTexture());
    glUniform1i(m_uniDebugEdgesTex, 6);

    glUniform2f(m_uniScreenResolution, (float)windowWidth, (float)windowHeight);
    glUniform1f(m_uniCellSize, (float)m_cellSize);

    // UI font for text, stamina bar and blood drips.
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_uiFontTex);
    glUniform1i(m_uniUiFontTex, 3);
    glUniform1f(m_uniUiGlyphCount, (float)m_uiGlyphCount);

    // Stamina bar fades on real elapsed time.
    {
        auto now = std::chrono::steady_clock::now();
        float dt = 0.0f;
        if (m_staminaFadeTimeValid) {
            dt = std::chrono::duration<float>(now - m_staminaFadeLastTime).count();
            dt = std::min(dt, 0.1f);
        }
        m_staminaFadeLastTime = now;
        m_staminaFadeTimeValid = true;

        const float kFadeSpeed = 4.0f; // higher = faster fade (~0.25s "time constant")
        const float target = m_staminaEnabledTarget ? 1.0f : 0.0f;
        const float t = 1.0f - std::exp(-kFadeSpeed * dt);
        m_staminaAlpha += (target - m_staminaAlpha) * t;
        m_staminaAlpha = std::clamp(m_staminaAlpha, 0.0f, 1.0f);
    }

    glUniform1f(m_uniStaminaFrac, m_staminaFrac);
    glUniform1f(m_uniStaminaAlpha, m_staminaAlpha);
    glUniform1f(m_uniHealthFrac, m_healthFrac);

    if (m_uiOverlayEnabled && m_uiOverlayTex != 0) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, m_uiOverlayTex);
        glUniform1i(m_uniUiTex, 4);
        glUniform1f(m_uniUiCols, (float)m_uiOverlayCols);
        glUniform1f(m_uniUiRows, (float)m_uiOverlayRows);
        glUniform1f(m_uniUiEnabled, 1.0f);
    } else {
        glUniform1f(m_uniUiEnabled, 0.0f);
    }

    glUniform1f(m_uniFadeAlpha, m_fadeAlpha);
    glUniform1f(m_uniColorEnabled, m_colorEnabled ? 1.0f : 0.0f);
    glUniform1f(m_uniLensEffectEnabled, m_lensEffectEnabled ? 1.0f : 0.0f);
    glUniform1f(m_uniTime, m_time);

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
