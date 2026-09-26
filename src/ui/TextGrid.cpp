#include "TextGrid.h"
#include <algorithm>

namespace MainMenu {

unsigned char CharToGlyph(char c) {
    // Letters arrive as they are (e.g. from diary text) and are uppercased, since the atlas has
    // only capital letters (one shared block 8x8 font for all UI needs).
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');

    switch (c) {
        case 'A': return GLYPH_A;
        case 'B': return GLYPH_B;
        case 'C': return GLYPH_C;
        case 'D': return GLYPH_D;
        case 'E': return GLYPH_E;
        case 'F': return GLYPH_F;
        case 'G': return GLYPH_G;
        case 'H': return GLYPH_H;
        case 'I': return GLYPH_I;
        case 'J': return GLYPH_J;
        case 'K': return GLYPH_K;
        case 'L': return GLYPH_L;
        case 'M': return GLYPH_M;
        case 'N': return GLYPH_N;
        case 'O': return GLYPH_O;
        case 'P': return GLYPH_P;
        case 'Q': return GLYPH_Q;
        case 'R': return GLYPH_R;
        case 'S': return GLYPH_S;
        case 'T': return GLYPH_T;
        case 'U': return GLYPH_U;
        case 'V': return GLYPH_V;
        case 'W': return GLYPH_W;
        case 'X': return GLYPH_X;
        case 'Y': return GLYPH_Y;
        case 'Z': return GLYPH_Z;
        case '0': return GLYPH_0;
        case '1': return GLYPH_1;
        case '2': return GLYPH_2;
        case '3': return GLYPH_3;
        case '4': return GLYPH_4;
        case '5': return GLYPH_5;
        case '6': return GLYPH_6;
        case '7': return GLYPH_7;
        case '8': return GLYPH_8;
        case '9': return GLYPH_9;
        case '.': return GLYPH_PERIOD;
        case ',': return GLYPH_COMMA;
        case '\'': return GLYPH_APOS;
        case '-': return GLYPH_DASH;
        case ':': return GLYPH_COLON;
        case '?': return GLYPH_QMARK;
        case '!': return GLYPH_BANG;
        default:  return GLYPH_SPACE; // space and anything unknown — empty
    }
}

void PutGlyph(std::vector<unsigned char>& grid, int cols, int rows,
                      int col, int row, unsigned char glyphIndex) {
    if (col < 0 || col >= cols || row < 0 || row >= rows) return;
    grid[(size_t)row * cols + col] = (unsigned char)(glyphIndex + 1);
}

void PutText(std::vector<unsigned char>& grid, int cols, int rows,
                     int col, int row, const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == ' ') continue; // space = leave the cell untouched (background/frame below stays)
        PutGlyph(grid, cols, rows, col + (int)i, row, CharToGlyph(text[i]));
    }
}

unsigned char PickHoverAccentChance(int t) {
    return (t == 0) ? 5u : 9u; // ~2/5 (40%) for the inner layer, ~2/9 (22%) for outer ones
}

void DrawBox(std::vector<unsigned char>& grid, int cols, int rows,
                     int x0, int y0, int x1, int y1, int thickness, bool filled, int seed) {
    thickness = std::max(1, thickness);

    for (int t = 0; t < thickness; ++t) {
        const int yTop = y0 + t;
        const int yBot = y1 - t;
        for (int x = x0; x <= x1; ++x) {
            for (int side = 0; side < 2; ++side) {
                const int y = (side == 0) ? yTop : yBot;
                unsigned char g;
                if (x == x0 || x == x1) {
                    g = (t == 0) ? GLYPH_CORNER : GLYPH_VLINE; // inner corner layers — a vertical "post"
                } else {
                    unsigned int h = Hash(x + side * 977 + t * 131, seed);
                    if (filled && (h % PickHoverAccentChance(t)) < 2u) {
                        g = PickDenseGlyph(x, y, seed + 500); // a rare "smoldering" accent over the regular frame
                    } else if (h % 11 == 0)      g = GLYPH_DRIP_BIG;
                    else if (h % 11 == 1) g = GLYPH_DRIP_SMALL;
                    else                   g = GLYPH_HLINE;
                }
                PutGlyph(grid, cols, rows, x, y, g);
            }
        }
    }

    for (int t = 0; t < thickness; ++t) {
        const int xLeft = x0 + t;
        const int xRight = x1 - t;
        for (int y = y0 + thickness; y <= y1 - thickness; ++y) {
            for (int side = 0; side < 2; ++side) {
                const int x = (side == 0) ? xLeft : xRight;
                unsigned char g;
                unsigned int h = Hash(y + side * 613 + t * 257, seed + 97);
                if (filled && (h % PickHoverAccentChance(t)) < 2u) {
                    g = PickDenseGlyph(x, y, seed + 500);
                } else if (h % 13 == 0)      g = GLYPH_DRIP_BIG;
                else if (h % 13 == 1) g = GLYPH_DRIP_SMALL;
                else                   g = GLYPH_VLINE;
                PutGlyph(grid, cols, rows, x, y, g);
            }
        }
    }
}

} // namespace MainMenu
