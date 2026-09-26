#include "BigFont.h"
#include "TextGrid.h"
#include "UiGlyphs.h"
#include <algorithm>
#include <cmath>

namespace MainMenu {


const BigGlyph& BigC() {
    static const BigGlyph g = {{
        ".###.",
        "#...#",
        "#....",
        "#....",
        "#....",
        "#...#",
        ".###."
    }};
    return g;
}
const BigGlyph& BigE() {
    static const BigGlyph g = {{
        "#####",
        "#....",
        "#....",
        "####.",
        "#....",
        "#....",
        "#####"
    }};
    return g;
}
const BigGlyph& BigL() {
    static const BigGlyph g = {{
        "#....",
        "#....",
        "#....",
        "#....",
        "#....",
        "#....",
        "#####"
    }};
    return g;
}
const BigGlyph& BigS() {
    static const BigGlyph g = {{
        ".####",
        "#....",
        "#....",
        ".###.",
        "....#",
        "....#",
        "####."
    }};
    return g;
}
const BigGlyph& BigT() {
    static const BigGlyph g = {{
        "#####",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#.."
    }};
    return g;
}
const BigGlyph& BigA() {
    static const BigGlyph g = {{
        ".###.",
        "#...#",
        "#...#",
        "#####",
        "#...#",
        "#...#",
        "#...#"
    }};
    return g;
}
const BigGlyph& BigR() {
    static const BigGlyph g = {{
        "####.",
        "#...#",
        "#...#",
        "####.",
        "#.#..",
        "#..#.",
        "#...#"
    }};
    return g;
}
const BigGlyph& BigX() {
    static const BigGlyph g = {{
        "#...#",
        ".#.#.",
        "..#..",
        "..#..",
        "..#..",
        ".#.#.",
        "#...#"
    }};
    return g;
}
const BigGlyph& BigI() {
    static const BigGlyph g = {{
        "#####",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "#####"
    }};
    return g;
}

const BigGlyph& BigP() {
    static const BigGlyph g = {{
        "####.",
        "#...#",
        "#...#",
        "####.",
        "#....",
        "#....",
        "#...."
    }};
    return g;
}
const BigGlyph& BigU() {
    static const BigGlyph g = {{
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        ".###."
    }};
    return g;
}
const BigGlyph& BigD() {
    static const BigGlyph g = {{
        "####.",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "####."
    }};
    return g;
}
const BigGlyph& BigM() {
    static const BigGlyph g = {{
        "#...#",
        "##.##",
        "#.#.#",
        "#...#",
        "#...#",
        "#...#",
        "#...#"
    }};
    return g;
}
const BigGlyph& BigN() {
    static const BigGlyph g = {{
        "#...#",
        "##..#",
        "#.#.#",
        "#.#.#",
        "#..##",
        "#...#",
        "#...#"
    }};
    return g;
}

const BigGlyph& BigG() {
    static const BigGlyph g = {{
        ".###.",
        "#....",
        "#....",
        "#.##.",
        "#...#",
        "#...#",
        ".###."
    }};
    return g;
}
const BigGlyph& BigB() {
    static const BigGlyph g = {{
        "####.",
        "#...#",
        "#...#",
        "####.",
        "#...#",
        "#...#",
        "####."
    }};
    return g;
}
const BigGlyph& BigK() {
    static const BigGlyph g = {{
        "#...#",
        "#..#.",
        "#.#..",
        "##...",
        "#.#..",
        "#..#.",
        "#...#"
    }};
    return g;
}
const BigGlyph& BigV() {
    static const BigGlyph g = {{
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        ".#.#.",
        "..#.."
    }};
    return g;
}
const BigGlyph& BigY() {
    static const BigGlyph g = {{
        "#...#",
        "#...#",
        ".#.#.",
        "..#..",
        "..#..",
        "..#..",
        "..#.."
    }};
    return g;
}

const BigGlyph& BigO() {
    static const BigGlyph g = {{
        ".###.",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        ".###."
    }};
    return g;
}

const BigGlyph& BigW() {
    static const BigGlyph g = {{
        "#...#",
        "#...#",
        "#...#",
        "#.#.#",
        "#.#.#",
        "##.##",
        "#...#"
    }};
    return g;
}

const BigGlyph& BigF() {
    static const BigGlyph g = {{
        "#####",
        "#....",
        "#....",
        "####.",
        "#....",
        "#....",
        "#...."
    }};
    return g;
}

const BigGlyph& BigH() {
    static const BigGlyph g = {{
        "#...#",
        "#...#",
        "#...#",
        "#####",
        "#...#",
        "#...#",
        "#...#"
    }};
    return g;
}

const BigGlyph& BigJ() {
    static const BigGlyph g = {{
        "..###",
        "....#",
        "....#",
        "....#",
        "#...#",
        "#...#",
        ".###."
    }};
    return g;
}

const BigGlyph& BigQ() {
    static const BigGlyph g = {{
        ".###.",
        "#...#",
        "#...#",
        "#...#",
        "#.#.#",
        "#..#.",
        ".##.#"
    }};
    return g;
}

const BigGlyph& BigZ() {
    static const BigGlyph g = {{
        "#####",
        "....#",
        "...#.",
        "..#..",
        ".#...",
        "#....",
        "#####"
    }};
    return g;
}

const BigGlyph& BigUnderscore() {
    static const BigGlyph g = {{
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        "#####"
    }};
    return g;
}

const BigGlyph& BigSpace() {
    static const BigGlyph g = {{
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        "....."
    }};
    return g;
}

const BigGlyph& GetBigDigitGlyph(int d) {
    static const BigGlyph digits[10] = {{{ // 0
        ".###.",
        "#...#",
        "#..##",
        "#.#.#",
        "##..#",
        "#...#",
        ".###."
    }}, {{ // 1
        "..#..",
        ".##..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        ".###."
    }}, {{ // 2
        ".###.",
        "#...#",
        "....#",
        "...#.",
        "..#..",
        ".#...",
        "#####"
    }}, {{ // 3
        ".###.",
        "#...#",
        "....#",
        "..##.",
        "....#",
        "#...#",
        ".###."
    }}, {{ // 4
        "...#.",
        "..##.",
        ".#.#.",
        "#..#.",
        "#####",
        "...#.",
        "...#."
    }}, {{ // 5
        "#####",
        "#....",
        "####.",
        "....#",
        "....#",
        "#...#",
        ".###."
    }}, {{ // 6
        "..##.",
        ".#...",
        "#....",
        "####.",
        "#...#",
        "#...#",
        ".###."
    }}, {{ // 7
        "#####",
        "....#",
        "...#.",
        "..#..",
        ".#...",
        ".#...",
        ".#..."
    }}, {{ // 8
        ".###.",
        "#...#",
        "#...#",
        ".###.",
        "#...#",
        "#...#",
        ".###."
    }}, {{ // 9
        ".###.",
        "#...#",
        "#...#",
        ".####",
        "....#",
        "...#.",
        ".##.."
    }}};
    return digits[d];
}

const BigGlyph& BigDigit(int d) {
    if (d < 0 || d > 9) return BigE(); // shouldn't happen with controlled input
    return GetBigDigitGlyph(d);
}

const BigGlyph& GetBigGlyph(char c) {
    switch (c) {
        case 'C': return BigC();
        case 'E': return BigE();
        case 'L': return BigL();
        case 'S': return BigS();
        case 'T': return BigT();
        case 'A': return BigA();
        case 'R': return BigR();
        case 'X': return BigX();
        case 'I': return BigI();
        case 'P': return BigP();
        case 'U': return BigU();
        case 'D': return BigD();
        case 'M': return BigM();
        case 'N': return BigN();
        case 'G': return BigG();
        case 'B': return BigB();
        case 'K': return BigK();
        case 'V': return BigV();
        case 'Y': return BigY();
        case 'O': return BigO();
        case 'W': return BigW();
        case 'F': return BigF();
        case 'H': return BigH();
        case 'J': return BigJ();
        case 'Q': return BigQ();
        case 'Z': return BigZ();
        case '_': return BigUnderscore();
        case ' ': return BigSpace();
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return BigDigit(c - '0');
        default:  return BigE(); // shouldn't happen with controlled input
    }
}

// How many screen cells one pixel of the original 5x7 pattern covers per axis: round(scale *
// kMaskUpsample), never below 1. scale is a float, not an int, so the title and the button text can
// have independently different physical sizes (e.g. 1.5 for "CELL" and 0.5 for buttons).

int ComputeFinalRes(float scale) {
    return std::max(1, (int)std::lround((double)scale * kMaskUpsample));
}

// Draws one large logo glyph with an upscaled mask: each original pixel of the 5x7 pattern expands
// into finalRes x finalRes screen cells, each getting its own dense glyph chosen deterministically
// from the seed (no flicker between frames). Neighboring cells are almost always different, while
// the letter's silhouette stays unchanged.
void DrawBigGlyph(std::vector<unsigned char>& grid, int cols, int rows,
                          const BigGlyph& glyph, int originCol, int originRow, float scale, int seed) {
    const int finalRes = ComputeFinalRes(scale);
    for (int r = 0; r < 7; ++r) {
        for (int c = 0; c < 5; ++c) {
            if (glyph.rows[r][c] != '#') continue;
            for (int sy = 0; sy < finalRes; ++sy) {
                for (int sx = 0; sx < finalRes; ++sx) {
                    int gc = originCol + c * finalRes + sx;
                    int gr = originRow + r * finalRes + sy;
                    PutGlyph(grid, cols, rows, gc, gr, PickDenseGlyph(gc, gr, seed));
                }
            }
        }
    }
}

int BigGlyphWidth(float scale)  { return 5 * ComputeFinalRes(scale); }
int BigGlyphHeight(float scale) { return 7 * ComputeFinalRes(scale); }

int BigTextWidth(const std::string& text, float scale) {
    if (text.empty()) return 0;
    const int letterW = BigGlyphWidth(scale);
    const int gap = ComputeFinalRes(scale);
    return (int)text.size() * letterW + std::max(0, (int)text.size() - 1) * gap;
}

void DrawBigText(std::vector<unsigned char>& grid, int cols, int rows,
                         const std::string& text, int originCol, int originRow,
                         float scale, int seed) {
    const int letterW = BigGlyphWidth(scale);
    const int gap = ComputeFinalRes(scale);
    int col = originCol;
    for (size_t i = 0; i < text.size(); ++i) {
        DrawBigGlyph(grid, cols, rows, GetBigGlyph(text[i]), col, originRow, scale, seed + (int)i * 7919);
        col += letterW + gap;
    }
}

} // namespace MainMenu
