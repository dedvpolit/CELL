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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    // MIPMAP_LINEAR: the post pass reads this texture with textureLod() at the mip matching the
    // cell size instead of averaging 3x3 per pixel, so a real mip chain is built once per frame in
    // end().
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

// 8x8 bitmap font of varying "ink density": ASCII, digits, Latin letters and a few dense glyph-like
// patterns for the brightest areas. The array order does not matter: the glyphs are sorted by
// density automatically.
struct GlyphDef { unsigned char rows[8]; };

static const GlyphDef s_glyphs[] = {
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}}, // ' '
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18}}, // '.'
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x0C}}, // ','
    {{0x18,0x18,0x10,0x00,0x00,0x00,0x00,0x00}}, // '`'
    {{0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}}, // ':'
    {{0x00,0x18,0x18,0x00,0x18,0x18,0x0C,0x00}}, // ';'
    {{0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}}, // '-'
    {{0x00,0x66,0x66,0x00,0x00,0x00,0x00,0x00}}, // '"'
    {{0x18,0x24,0x42,0x00,0x00,0x00,0x00,0x00}}, // '^'
    {{0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}}, // '='
    {{0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}}, // '+'
    {{0x00,0x06,0x18,0x60,0x60,0x18,0x06,0x00}}, // '<'
    {{0x00,0x60,0x18,0x06,0x06,0x18,0x60,0x00}}, // '>'
    {{0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x00}}, // '/'
    {{0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x00}}, // '\'
    {{0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00}}, // '*'
    {{0x08,0x18,0x28,0x08,0x08,0x08,0x3E,0x00}}, // '1'
    {{0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}}, // '|'
    {{0x00,0x42,0x42,0x24,0x24,0x18,0x00,0x00}}, // 'v'
    {{0x00,0x3C,0x66,0x60,0x66,0x3C,0x00,0x00}}, // 'c'
    {{0x00,0x3C,0x66,0x66,0x66,0x3C,0x00,0x00}}, // 'o'
    {{0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00,0x00}}, // 's'
    {{0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00,0x00}}, // 'z'
    {{0x00,0x66,0x3C,0x18,0x3C,0x66,0x00,0x00}}, // 'x'
    {{0x00,0x6C,0x76,0x66,0x66,0x66,0x00,0x00}}, // 'n'
    {{0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00,0x00}}, // 'a'
    {{0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00,0x00}}, // 'e'
    {{0x00,0x6C,0x76,0x60,0x60,0x60,0x00,0x00}}, // 'r'
    {{0x00,0x24,0x7E,0x24,0x24,0x7E,0x24,0x00}}, // '#'
    {{0x00,0x62,0x64,0x08,0x10,0x26,0x46,0x00}}, // '%'
    {{0x00,0x3C,0x66,0x3C,0x38,0x67,0x3E,0x00}}, // '&'
    {{0x00,0x3C,0x66,0x3C,0x66,0x66,0x3C,0x00}}, // '8'
    {{0x00,0x66,0x3C,0x18,0x3C,0x66,0xC3,0x00}}, // 'X'
    {{0x00,0xC3,0xC3,0xDB,0xFF,0x66,0x66,0x00}}, // 'W'
    {{0x00,0xC3,0xE7,0xFF,0xDB,0xC3,0xC3,0x00}}, // 'M'
    {{0x00,0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0x00}}, // 'N'
    {{0x00,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}}, // 'H'
    {{0x00,0x66,0x6C,0x78,0x78,0x6C,0x66,0x00}}, // 'K'
    {{0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x62,0x3C}}, // '@'
    {{0xFF,0x99,0xFF,0x99,0xFF,0x99,0xFF,0x99}}, // dense "glyph-like" pattern
    {{0xFF,0xE7,0xFF,0xE7,0xFF,0xE7,0xFF,0xE7}}, // even denser
    {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}}, // solid block
    {{0x00,0x18,0x3C,0x3C,0x18,0x00,0x00,0x00}}, // diamond
    {{0x00,0x66,0x00,0x18,0x00,0x66,0x00,0x00}}, // sparse dots
    {{0x3C,0x42,0x99,0xA5,0xA5,0x99,0x42,0x3C}}, // "glyph-like" pattern 1
    {{0x66,0xFF,0xDB,0xFF,0xFF,0xDB,0xFF,0x66}}, // "glyph-like" pattern 2
    {{0x00,0x6E,0x11,0x11,0x11,0x11,0x6E,0x00}}, // 'D'-like
    {{0x7E,0x81,0xA5,0x81,0xA5,0x99,0x81,0x7E}}, // complex pattern (face/mask)
    {{0xF0,0x0F,0xF0,0x0F,0xF0,0x0F,0xF0,0x0F}}, // diagonal stripes
    {{0x0F,0xF0,0x0F,0xF0,0x0F,0xF0,0x0F,0xF0}}, // diagonal stripes (inverted)

        // Very sparse (darkest areas)
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00}}, // single dot
    {{0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00}}, // dot higher up
    {{0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00}}, // dot to the side
    {{0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00}}, // dot in the corner

    // Strokes resembling Cyrillic/CJK characters
    {{0x00,0x7E,0x18,0x18,0x18,0x18,0x7E,0x00}}, // 'Sh'-like
    {{0x00,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}}, // 'D'-like (Cyrillic)
    {{0x00,0x18,0x3C,0x66,0x66,0x3C,0x18,0x00}}, // diamond glyph
    {{0x18,0x18,0x7E,0x18,0x18,0x00,0x7E,0x00}}, // '木'-like (tree)
    {{0x24,0x24,0xFF,0x24,0xFF,0x24,0x24,0x00}}, // '井'-like (well/grid)
    {{0x00,0x3C,0x24,0x24,0x24,0x24,0x3C,0x00}}, // 'P'-frame (Cyrillic)
    {{0x66,0x66,0x24,0x18,0x24,0x66,0x66,0x00}}, // 'Zh'-like
    {{0x7E,0x40,0x40,0x7C,0x40,0x40,0x7E,0x00}}, // 'E'-like, bold
    {{0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x18}}, // 'Q'/'Ω'-like

    // Very dense (brightest areas, close to the camera)
    {{0xFF,0x81,0xBD,0xA5,0xA5,0xBD,0x81,0xFF}}, // complex dense pattern
    {{0xEF,0xDB,0xBD,0x7E,0x7E,0xBD,0xDB,0xEF}}, // nearly solid with a diamond
    {{0xFF,0xFF,0xE7,0xC3,0xC3,0xE7,0xFF,0xFF}}, // nearly a block with a slit
    {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE}}, // maximally dense

    // Cyrillic shapes that do not duplicate Latin letters
    {{0x00,0x66,0x66,0x66,0x3E,0x06,0x06,0x00}}, // 'Ts' (simplified)
    {{0x66,0x66,0x24,0x18,0x24,0x66,0x66,0x00}}, // 'Zh'
    {{0x3C,0x66,0x0C,0x18,0x0C,0x66,0x3C,0x00}}, // 'Z' (Cyrillic)
    {{0x66,0x66,0x6E,0x76,0x66,0x66,0x66,0x00}}, // 'I' (Cyrillic)
    {{0x00,0x66,0x66,0x66,0x66,0x66,0x3E,0x06}}, // 'Shch' simplified
    {{0x7E,0x66,0x66,0x66,0x66,0x66,0x66,0x00}}, // 'P' (Cyrillic)
    {{0x18,0x3C,0x66,0x7E,0x66,0x66,0x66,0x00}}, // 'F'-approximation
    {{0x66,0x66,0x66,0x3C,0x18,0x3C,0x66,0x00}}, // 'Yu'-approximation
    {{0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x18}}, // 'Ya'-approximation




};
static const int s_glyphCount = sizeof(s_glyphs) / sizeof(s_glyphs[0]);

// Fixed glyph set shared by the UI text, HUD and compass minimap; unlike s_glyphs[] it is not
// sorted by ink density: each meaning has a fixed index the shader uses (glyphIdx).
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

    // Menu letters (title "CELL", buttons, ...): a plain block 8x8 font in the same style as the
    // rest of the atlas. The indices are stable: the UI text layout depends on them.
    {{0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00}}, // 18: 'A'
    {{0x00,0x3C,0x66,0x60,0x66,0x3C,0x00,0x00}}, // 19: 'C'
    {{0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00}}, // 20: 'E'
    {{0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00}}, // 21: 'I'
    {{0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}}, // 22: 'L'
    {{0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00}}, // 23: 'R'
    {{0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}}, // 24: 'S'
    {{0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}}, // 25: 'T'
    {{0xC3,0x66,0x3C,0x18,0x3C,0x66,0xC3,0x00}}, // 26: 'X'

    // "Dense" variants for the ASCII-art title (see MainMenu.h): not letters, just equally dark
    // patterns reused from s_glyphs[] and mixed, so the big title letters consist of varied
    // characters instead of looking like a solid block.
    {{0xFF,0x99,0xFF,0x99,0xFF,0x99,0xFF,0x99}}, // 27
    {{0xFF,0xE7,0xFF,0xE7,0xFF,0xE7,0xFF,0xE7}}, // 28
    {{0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x62,0x3C}}, // 29 ('@'-like)
    {{0x66,0xFF,0xDB,0xFF,0xFF,0xDB,0xFF,0x66}}, // 30
    {{0xF0,0x0F,0xF0,0x0F,0xF0,0x0F,0xF0,0x0F}}, // 31 (diagonal stripes)
    {{0x0F,0xF0,0x0F,0xF0,0x0F,0xF0,0x0F,0xF0}}, // 32 (diagonal stripes, inverted)
    {{0xEF,0xDB,0xBD,0x7E,0x7E,0xBD,0xDB,0xEF}}, // 33

    // Remaining Latin letters (for running text, see diaries): the same plain block 8x8 style as
    // above, with no dense decorative variants.
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

    // HUD item icons (MenuLayouts::DrawHudIcons()) have dedicated indices instead of reusing
    // GLYPH_TORCH (3), which the compass and menu background also use. 68: torch (three flame
    // prongs on one handle).
    {{0x54,0x54,0x54,0x7C,0x10,0x10,0x10,0x38}}, // 68: torch 'Ψ'

    // 69: diary, an open book (top/bottom pages with a visible spine in the middle)
    {{0x00,0x7E,0x5A,0x5A,0x5A,0x5A,0x7E,0x00}}, // 69: diary (open book)
};
static const int s_uiGlyphCount = sizeof(s_uiGlyphs) / sizeof(s_uiGlyphs[0]);

// Index map of s_uiGlyphs[]; the shader uses some indices as literals, so keep them in sync.
//   0 empty, 1 fill ('#'), 13 hline, 14 vline, 15 corner, 16/17 big/small drip (stamina frame)
//   18-26 and 34-50: letters A-Z (GLYPH_* in ui/UiGlyphs.h); 27-33: dense fillers for big text
//   51-60 digits, 61-67 punctuation; 68 torch icon, 69 diary icon

void AsciiEffect::generateUiFontAtlas() {
    m_uiGlyphCount = s_uiGlyphCount;

    const int glyphSize = m_cellSize;
    const int atlasW = glyphSize * m_uiGlyphCount;
    const int atlasH = glyphSize;
    std::vector<unsigned char> pixels(atlasW * atlasH, 0);

    for (int level = 0; level < m_uiGlyphCount; level++) {
        const GlyphDef& g = s_uiGlyphs[level];
        for (int y = 0; y < glyphSize; y++) {
            int srcRow = (y * 8) / glyphSize;
            unsigned char rowBits = g.rows[srcRow];
            for (int x = 0; x < glyphSize; x++) {
                int srcCol = (x * 8) / glyphSize;
                bool on = (rowBits >> (7 - srcCol)) & 1;
                pixels[y * atlasW + (level * glyphSize + x)] = on ? 255 : 0;
            }
        }
    }

    glGenTextures(1, &m_uiFontTex);
    glBindTexture(GL_TEXTURE_2D, m_uiFontTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasW, atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void AsciiEffect::generateFontAtlas() {
    // Compute the ink density (number of lit pixels) of each glyph and sort from emptiest to
    // densest: that forms a proper brightness ramp, like a real ASCII-art converter.
    std::vector<int> order(s_glyphCount);
    for (int i = 0; i < s_glyphCount; i++) order[i] = i;

    auto density = [](const GlyphDef& g) {
        int count = 0;
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 8; b++)
                if ((g.rows[r] >> b) & 1) count++;
        return count;
    };

    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return density(s_glyphs[a]) < density(s_glyphs[b]);
    });

    m_rampLength = s_glyphCount;

    const int glyphSize = m_cellSize;
    const int atlasW = glyphSize * m_rampLength;
    const int atlasH = glyphSize;
    std::vector<unsigned char> pixels(atlasW * atlasH, 0);

    for (int level = 0; level < m_rampLength; level++) {
        const GlyphDef& g = s_glyphs[order[level]];
        for (int y = 0; y < glyphSize; y++) {
            int srcRow = (y * 8) / glyphSize;
            unsigned char rowBits = g.rows[srcRow];
            for (int x = 0; x < glyphSize; x++) {
                int srcCol = (x * 8) / glyphSize;
                bool on = (rowBits >> (7 - srcCol)) & 1;
                pixels[y * atlasW + (level * glyphSize + x)] = on ? 255 : 0;
            }
        }
    }

    glGenTextures(1, &m_fontTex);
    glBindTexture(GL_TEXTURE_2D, m_fontTex);
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
    m_normalCellSize = cellSize;
    createFBO(sceneWidth, sceneHeight);
    generateFontAtlas();
    generateUiFontAtlas();
    createQuad();

    const std::string vertSrc = ShaderLoader::LoadSource("assets/shaders/ascii_post.vert");
    const std::string fragSrc = ShaderLoader::LoadSource("assets/shaders/ascii_post.frag");
    GLuint vs = ShaderProgram::CompileShader(GL_VERTEX_SHADER, vertSrc.c_str(), "AsciiEffect");
    GLuint fs = ShaderProgram::CompileShader(GL_FRAGMENT_SHADER, fragSrc.c_str(), "AsciiEffect");
    m_program = ShaderProgram::LinkProgram(vs, fs, "AsciiEffect");
    cacheUniformLocations();
    return m_program != 0;
}

void AsciiEffect::cacheUniformLocations() {
    if (!m_program) return;
    m_uniSceneTex           = glGetUniformLocation(m_program, "sceneTex");
    m_uniFontTex             = glGetUniformLocation(m_program, "fontTex");
    m_uniScreenResolution    = glGetUniformLocation(m_program, "screenResolution");
    m_uniCellSize            = glGetUniformLocation(m_program, "cellSize");
    m_uniRampLength          = glGetUniformLocation(m_program, "rampLength");
    m_uniUiFontTex      = glGetUniformLocation(m_program, "uiFontTex");
    m_uniUiGlyphCount   = glGetUniformLocation(m_program, "uiGlyphCount");
    m_uniStaminaFrac         = glGetUniformLocation(m_program, "staminaFrac");
    m_uniGlitchActive        = glGetUniformLocation(m_program, "uGlitchActive");
    m_uniGlitchUV            = glGetUniformLocation(m_program, "uGlitchUV");
    m_uniGlitchRadiusCells   = glGetUniformLocation(m_program, "uGlitchRadiusCells");
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

void AsciiEffect::resize(int sceneWidth, int sceneHeight) {
    destroyFBO();
    createFBO(sceneWidth, sceneHeight);
}

void AsciiEffect::shutdown() {
    destroyFBO();
    if (m_fontTex) glDeleteTextures(1, &m_fontTex);
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
    // Only the target is stored: the actual visibility (m_staminaAlpha) catches up to it in end()
    // based on real elapsed time instead of switching instantly.
    m_staminaEnabledTarget = enabled;
}

void AsciiEffect::setHealth(float fraction01) {
    m_healthFrac = fraction01;
}

void AsciiEffect::setCinematicMode(bool enabled, int cinematicCellSize) {
    const int newCellSize = enabled ? cinematicCellSize : m_normalCellSize;

    if (enabled == m_cinematicMode && newCellSize == m_cellSize) return; // already in the right mode
    m_cinematicMode = enabled;

    if (newCellSize == m_cellSize) return;

    m_cellSize = newCellSize;

    // Both font atlases bake glyphs for a specific pixel cell size (see generateFontAtlas()/
    // generateUiFontAtlas()), so they must be rebuilt when m_cellSize changes.
    if (m_fontTex) { glDeleteTextures(1, &m_fontTex); m_fontTex = 0; }
    if (m_uiFontTex) { glDeleteTextures(1, &m_uiFontTex); m_uiFontTex = 0; }

    generateFontAtlas();
    generateUiFontAtlas();
}

void AsciiEffect::setUserCellSize(int cellSize) {
    cellSize = std::clamp(cellSize, kMinCellSize, kMaxCellSize);
    m_normalCellSize = cellSize;

    // Cinematic mode is active: do not touch m_cellSize now, only remember the new normal value
    // (m_normalCellSize). It applies when cinematic mode turns off.
    if (m_cinematicMode) return;

    if (cellSize == m_cellSize) return;

    m_cellSize = cellSize;

    if (m_fontTex) { glDeleteTextures(1, &m_fontTex); m_fontTex = 0; }
    if (m_uiFontTex) { glDeleteTextures(1, &m_uiFontTex); m_uiFontTex = 0; }

    generateFontAtlas();
    generateUiFontAtlas();
}

void AsciiEffect::setUIOverlay(bool enabled, const std::vector<unsigned char>& grid, int cols, int rows) {
    m_uiOverlayEnabled = enabled;

    if (!enabled) {
        // The data is not touched: it is simpler and more robust to always re-upload when enabled.
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

    // The CELL title animates, so the grid changes every frame while the menu is open. If the
    // texture size is unchanged, reuse the GPU memory (glTexSubImage2D) instead of reallocating it
    // (glTexImage2D) every frame; reallocate only on a real size change.
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
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowWidth, windowHeight);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneTex);
    // Build the mip chain of the finished 3D pass once per frame so the post shader can sample a
    // downsampled color via textureLod() (see ascii_post.frag).
    glGenerateMipmap(GL_TEXTURE_2D);
    glUniform1i(m_uniSceneTex, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_fontTex);
    glUniform1i(m_uniFontTex, 1);

    glUniform2f(m_uniScreenResolution, (float)windowWidth, (float)windowHeight);
    glUniform1f(m_uniCellSize, (float)m_cellSize);
    glUniform1f(m_uniRampLength, (float)m_rampLength);

    // Shared UI font (UI text, stamina bar, blood drips): they all need the atlas and the glyph
    // count, so bind them unconditionally.
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_uiFontTex);
    glUniform1i(m_uniUiFontTex, 3);
    glUniform1f(m_uniUiGlyphCount, (float)m_uiGlyphCount);

    // Stamina bar with a smooth fade: m_staminaAlpha catches up to the target
    // (m_staminaEnabledTarget) based on real elapsed time instead of switching instantly, so
    // entering/leaving noclip does not jerk the bar.
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
    glUniform1f(m_uniGlitchActive, m_glitchActive ? 1.0f : 0.0f);
    glUniform2f(m_uniGlitchUV, m_glitchU, m_glitchV);
    glUniform1f(m_uniGlitchRadiusCells, m_glitchRadiusCells);
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
