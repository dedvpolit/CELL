#include "TextGrid.h"
#include <algorithm>

namespace MainMenu {

unsigned char CharToGlyph(char c) {
    // The atlas has capitals only.
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
        default:  return GLYPH_SPACE; // space and anything unknown: empty
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
    return (t == 0) ? 5u : 9u; // 40% of cells on the outermost layer, 22% on the others
}

namespace {

// Wear of one frame edge, cell by cell: seeded gaps of 1-3 cells, and the cells beside a gap are
// chipped to a thinner glyph. Cells within solidEnds of either end stay intact so the box still
// reads as a box; edges shorter than minLength have no gaps at all.
enum class Wear { Solid, Chipped, Gap };

struct EdgeWear {
    int length;
    int solidEnds;
    int minLength;
    unsigned int seed;

    static constexpr unsigned int kGapEvery = 6;
    static constexpr int kMaxGapLength = 3;

    bool isGap(int i) const {
        if (length < minLength || i < solidEnds || i >= length - solidEnds) return false;
        for (int start = i - (kMaxGapLength - 1); start <= i; ++start) {
            if (start < solidEnds) continue;
            const unsigned int h = Hash(start, (int)seed);
            if (h % kGapEvery != 0) continue;
            const int gapLength = 1 + (int)((h >> 8) % kMaxGapLength);
            if (start + gapLength > length - solidEnds) continue;
            if (i < start + gapLength) return true;
        }
        return false;
    }

    Wear at(int i) const {
        if (isGap(i)) return Wear::Gap;
        if (isGap(i - 1) || isGap(i + 1)) return Wear::Chipped;
        return Wear::Solid;
    }
};

} // namespace

void DrawBox(std::vector<unsigned char>& grid, int cols, int rows,
             int x0, int y0, int x1, int y1, int thickness, bool filled, int seed) {
    thickness = std::max(1, thickness);

    // Gaps depend only on the seed, never on `filled`, so hovering does not reshuffle them.
    for (int t = 0; t < thickness; ++t) {
        for (int side = 0; side < 2; ++side) {
            const int y = (side == 0) ? y0 + t : y1 - t;
            const EdgeWear wear{ x1 - x0 - 1, 2, 6, Hash(side * 7919 + t * 104729, seed + 31) };
            for (int x = x0; x <= x1; ++x) {
                unsigned char g;
                if (x == x0 || x == x1) {
                    g = (t == 0) ? GLYPH_CORNER : GLYPH_VLINE;
                } else {
                    const Wear w = wear.at(x - x0 - 1);
                    if (w == Wear::Gap) continue;
                    const unsigned int h = Hash(x + side * 977 + t * 131, seed);
                    if (w == Wear::Chipped)                                  g = GLYPH_DASH;
                    else if (filled && (h % PickHoverAccentChance(t)) < 2u) g = PickDenseGlyph(x, y, seed + 500);
                    else if (h % 11 == 0)                                    g = GLYPH_DRIP_BIG;
                    else if (h % 11 == 1)                                    g = GLYPH_DRIP_SMALL;
                    else                                                     g = GLYPH_HLINE;
                }
                PutGlyph(grid, cols, rows, x, y, g);
            }
        }
    }

    const int sideFirst = y0 + thickness;
    const int sideLast = y1 - thickness;
    for (int t = 0; t < thickness; ++t) {
        for (int side = 0; side < 2; ++side) {
            const int x = (side == 0) ? x0 + t : x1 - t;
            const EdgeWear wear{ sideLast - sideFirst + 1, 1, 4, Hash(side * 6151 + t * 49157, seed + 77) };
            for (int y = sideFirst; y <= sideLast; ++y) {
                const Wear w = wear.at(y - sideFirst);
                if (w == Wear::Gap) continue;
                const unsigned int h = Hash(y + side * 613 + t * 257, seed + 97);
                unsigned char g;
                if (w == Wear::Chipped)                                  g = GLYPH_COLON;
                else if (filled && (h % PickHoverAccentChance(t)) < 2u) g = PickDenseGlyph(x, y, seed + 500);
                else if (h % 13 == 0)                                    g = GLYPH_DRIP_BIG;
                else if (h % 13 == 1)                                    g = GLYPH_DRIP_SMALL;
                else                                                     g = GLYPH_VLINE;
                PutGlyph(grid, cols, rows, x, y, g);
            }
        }
    }
}

} // namespace MainMenu
