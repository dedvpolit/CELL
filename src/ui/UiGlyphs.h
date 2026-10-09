#pragma once
// Shared glyph indices and small glyph helpers for the menu modules

namespace MainMenu {

constexpr unsigned char GLYPH_SPACE      = 0;
constexpr unsigned char GLYPH_HASH       = 1;  // '#': fill / bold frame for the selected button
// Same indices as the dungeon minimap (one shared font)
constexpr unsigned char GLYPH_FLOOR      = 2;  // '.': floor/dust/ash
constexpr unsigned char GLYPH_TORCH      = 3;  // '*': torch
constexpr unsigned char GLYPH_CIRCLE     = 12; // 'O': smoldering ember/finial
constexpr unsigned char GLYPH_HLINE      = 13; // '='
constexpr unsigned char GLYPH_VLINE      = 14; // '|'
constexpr unsigned char GLYPH_CORNER     = 15; // '+'
constexpr unsigned char GLYPH_DRIP_BIG   = 16; // large "drip": ragged frame edge
constexpr unsigned char GLYPH_DRIP_SMALL = 17; // small "drip"
// The stone icon reuses dense pattern 29
constexpr unsigned char GLYPH_STONE_ICON = 29;

constexpr unsigned char GLYPH_TORCH_ICON = 68; // torch icon (trident)
constexpr unsigned char GLYPH_DIARY_ICON = 69; // an open book
constexpr unsigned char GLYPH_A = 18, GLYPH_C = 19, GLYPH_E = 20, GLYPH_I = 21,
                         GLYPH_L = 22, GLYPH_R = 23, GLYPH_S = 24, GLYPH_T = 25,
                         GLYPH_X = 26;

// Letters 34-50 complete the alphabet with 18-26
constexpr unsigned char GLYPH_B = 34, GLYPH_D = 35, GLYPH_F = 36, GLYPH_G = 37,
                         GLYPH_H = 38, GLYPH_J = 39, GLYPH_K = 40, GLYPH_M = 41,
                         GLYPH_N = 42, GLYPH_O = 43, GLYPH_P = 44, GLYPH_Q = 45,
                         GLYPH_U = 46, GLYPH_V = 47, GLYPH_W = 48, GLYPH_Y = 49,
                         GLYPH_Z = 50;

constexpr unsigned char GLYPH_0 = 51, GLYPH_1 = 52, GLYPH_2 = 53, GLYPH_3 = 54,
                         GLYPH_4 = 55, GLYPH_5 = 56, GLYPH_6 = 57, GLYPH_7 = 58,
                         GLYPH_8 = 59, GLYPH_9 = 60;
constexpr unsigned char GLYPH_PERIOD  = 61; // '.'
constexpr unsigned char GLYPH_COMMA   = 62; // ','
constexpr unsigned char GLYPH_APOS    = 63; // '\''
constexpr unsigned char GLYPH_DASH    = 64; // '-'
constexpr unsigned char GLYPH_COLON   = 65; // ':'
constexpr unsigned char GLYPH_QMARK   = 66; // '?'
constexpr unsigned char GLYPH_BANG    = 67; // '!'

// Dense patterns 27-33 mixed into big title letters
constexpr unsigned char kDenseFillGlyphs[] = {
    GLYPH_HASH, 27, 28, 29, 30, 31, 32, 33
};
constexpr int kDenseFillGlyphCount = sizeof(kDenseFillGlyphs) / sizeof(kDenseFillGlyphs[0]);

inline unsigned int Hash(int x, int seed) {
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)seed * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Seeded dense glyph for (col, row)
inline unsigned char PickDenseGlyph(int col, int row, int seed) {
    unsigned int h = Hash(col * 92821 + row, seed);
    return kDenseFillGlyphs[h % (unsigned int)kDenseFillGlyphCount];
}

} // namespace MainMenu
