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

// ---- Добавлено для меню паузы (PAUSED / RESUME / MENU) ----
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

// ---- Добавлено для экрана настроек (SETTINGS / SENSITIVITY / BACK) ----
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

// ---- Добавлено для кнопки "NEW GAME" (главное меню) ----
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

// ---- Добавлено для полного алфавита A-Z (ввод имени сохранения, фраза
// подтверждения перезаписи — см. комментарий у объявления в BigFont.h) ----
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

// Подчёркивание — плейсхолдер незаполненной буквы при вводе имени
// сохранения (см. AppState::SAVE_NAME_ENTRY в Application.cpp,
// ui/MenuLayouts.cpp: BuildNameEntryMenu()) — "AB___" для уже введённых
// "AB" из максимум 5 символов.
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

// Полностью пустой узор (ни одного '#') — см. комментарий у объявления
// в BigFont.h: подставляется вместо буквы для пробела в многословных
// надписях кнопок, чтобы DrawBigGlyph() честно ничего не рисовала на
// этом месте, а не заливала его буквой E (старое поведение default-ветки
// GetBigGlyph() для ЛЮБОГО незнакомого символа, включая пробел).
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

// ---- Цифры 0-9 — см. комментарий у объявления BigDigit() в BigFont.h ----
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
    if (d < 0 || d > 9) return BigE(); // не должно случаться при контролируемом вводе
    return GetBigDigitGlyph(d);
}

// Достаёт крупный ASCII-арт глиф по букве — используется заголовком
// "CELL" и текстом кнопок главного меню ("START"/"EXIT"/"SETTINGS"),
// меню паузы ("RESUME"/"MENU") и экрана настроек ("SETTINGS"/
// "SENSITIVITY"/"BACK"), см. DrawBigText() ниже и BuildButtonMenu() ниже.
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
        default:  return BigE(); // не должно случаться при контролируемом вводе
    }
}

// Рисует один крупный логотип-глиф с ПОВЫШЕННЫМ разрешением маски и
// разнообразием символов внутри — раньше каждый "пиксель" исходного 5x7
// паттерна заливался ОДНИМ И ТЕМ ЖЕ повторяющимся '#' на весь scale x
// scale блок (буква выглядела сплошным блоком одинаковых символов).
// Теперь каждый исходный пиксель раскладывается в finalRes x finalRes
// экранных клеток (finalRes = round(scale * kMaskUpsample) — т.е. маска
// апскейлится минимум вдвое относительно исходных 5x7), и КАЖДАЯ такая
// клетка получает СВОЙ, независимо (но детерминированно, по seed —
// без "мерцания" между кадрами) выбранный плотный символ из
// kDenseFillGlyphs — соседние клетки почти всегда разные, силуэт буквы
// при этом не меняется (пиксели 5x7-паттерна всё те же).
//
// scale — float (не int), чтобы можно было независимо задавать РАЗНЫЙ
// физический размер для заголовка и текста кнопок (например 1.5 для
// "CELL" и 0.5 для "START"/"EXIT") — раньше оба были int и часто
// округлялись до одного и того же значения (кнопки выглядели как
// заголовок по размеру).

int ComputeFinalRes(float scale) {
    return std::max(1, (int)std::lround((double)scale * kMaskUpsample));
}

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

// Крупный ASCII-арт текст (несколько букв подряд через GetBigGlyph()) —
// используется и для заголовка "CELL", и для текста кнопок "START"/"EXIT",
// чтобы весь видимый текст меню состоял из апскейленной маски с
// разнообразными плотными символами, а не только заголовок.
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
