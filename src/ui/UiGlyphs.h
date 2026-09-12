#pragma once
// ============================================================================
// UiGlyphs.h — общие индексы фиксированного UI-шрифта и мелкие
// низкоуровневые утилиты процедурного выбора символов, используемые всеми
// модулями меню (TextGrid, BigFont, Atmosphere, TitleBreakup, MenuLayouts).
//
// Вынесено из MainMenu.h при разбиении монолита на модули — раньше все эти
// константы/функции жили в одном namespace вперемешку с логикой раскладки.
// Здесь оставлено header-only (константы + однострочные inline-функции) —
// в отличие от содержательных .cpp-модулей ниже, это чистые данные и
// тривиальные хэш-функции, которым не нужна отдельная единица трансляции.
// ============================================================================

namespace MainMenu {

constexpr unsigned char GLYPH_SPACE      = 0;
constexpr unsigned char GLYPH_HASH       = 1;  // '#' — заполнение / жирная рамка выбранной кнопки
// GLYPH_FLOOR/GLYPH_TORCH/GLYPH_CIRCLE — те же индексы, что и в мини-карте
// подземелья (см. AsciiEffect.cpp: s_minimapGlyphs[]), это ОДИН общий шрифт.
// В MainMenu.h раньше не переиспользовались — используем их для
// атмосферных деталей (тлеющие факелы, пепел на полу), не заводя ни
// одного нового глифа в атласе.
constexpr unsigned char GLYPH_FLOOR      = 2;  // '.' — пол/пыль/пепел
constexpr unsigned char GLYPH_TORCH      = 3;  // '*' — факел
constexpr unsigned char GLYPH_CIRCLE     = 12; // 'O' — тлеющий уголёк/навершие
constexpr unsigned char GLYPH_HLINE      = 13; // '='
constexpr unsigned char GLYPH_VLINE      = 14; // '|'
constexpr unsigned char GLYPH_CORNER     = 15; // '+'
constexpr unsigned char GLYPH_DRIP_BIG   = 16; // крупная "капля" — рваный край рамки
constexpr unsigned char GLYPH_DRIP_SMALL = 17; // мелкая "капля"
constexpr unsigned char GLYPH_A = 18, GLYPH_C = 19, GLYPH_E = 20, GLYPH_I = 21,
                         GLYPH_L = 22, GLYPH_R = 23, GLYPH_S = 24, GLYPH_T = 25,
                         GLYPH_X = 26;

// Остальные буквы латиницы (индексы 34-50, см. AsciiEffect.cpp:
// s_minimapGlyphs[]) — добавлены под связный текст дневников, чтобы
// не трогать исходные 18-26 (те завязаны на существующую раскладку
// заголовка меню). Вместе 18-26 + 34-50 = полный алфавит A-Z.
constexpr unsigned char GLYPH_B = 34, GLYPH_D = 35, GLYPH_F = 36, GLYPH_G = 37,
                         GLYPH_H = 38, GLYPH_J = 39, GLYPH_K = 40, GLYPH_M = 41,
                         GLYPH_N = 42, GLYPH_O = 43, GLYPH_P = 44, GLYPH_Q = 45,
                         GLYPH_U = 46, GLYPH_V = 47, GLYPH_W = 48, GLYPH_Y = 49,
                         GLYPH_Z = 50;

// Цифры 0-9 (индексы 51-60) и базовая пунктуация (61-67) — см. те же
// s_minimapGlyphs[] в AsciiEffect.cpp.
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

// "Плотные" узоры-заполнители (см. AsciiEffect.cpp: s_minimapGlyphs[]
// индексы 27-33) — используются вперемешку с GLYPH_HASH внутри крупных
// ASCII-арт букв заголовка (см. DrawBigGlyph() ниже), чтобы буквы
// состояли из разных символов, а не выглядели одним сплошным '#'.
constexpr unsigned char kDenseFillGlyphs[] = {
    GLYPH_HASH, 27, 28, 29, 30, 31, 32, 33
};
constexpr int kDenseFillGlyphCount = sizeof(kDenseFillGlyphs) / sizeof(kDenseFillGlyphs[0]);

inline unsigned int Hash(int x, int seed) {
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)seed * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Детерминированный (по seed, не по кадру) выбор одного из "плотных"
// узоров-заполнителей (kDenseFillGlyphs выше) для клетки (col,row).
// Используется в DrawBigGlyph() (BigFont.h) и в DrawBox()/hover-подсветке
// (TextGrid.h) — чтобы crowded-заливка была "текстурной", а не сплошным '#'.
inline unsigned char PickDenseGlyph(int col, int row, int seed) {
    unsigned int h = Hash(col * 92821 + row, seed);
    return kDenseFillGlyphs[h % (unsigned int)kDenseFillGlyphCount];
}

} // namespace MainMenu
