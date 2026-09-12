#pragma once
#include <vector>
#include <string>

// ============================================================================
// BigFont — крупный блочный логотип: 5x7 dot-matrix буквы, используемые для
// заголовка "CELL" и текста кнопок ("START"/"EXIT"). Каждая буква рисуется
// с апскейленной маской, где каждый "пиксель" паттерна заполняется своим
// (детерминированным по seed) плотным символом из UiGlyphs::kDenseFillGlyphs
// — см. DrawBigGlyph() ниже за подробностями.
// Вынесено из MainMenu.h при разбиении монолита на модули.
// ============================================================================

namespace MainMenu {

struct BigGlyph { const char* rows[7]; };

// Буквы алфавита, реально используемые в словах "CELL"/"START"/"EXIT"/
// меню паузы и настроек. Полный алфавит не нужен.
const BigGlyph& BigC();
const BigGlyph& BigE();
const BigGlyph& BigL();
const BigGlyph& BigS();
const BigGlyph& BigT();
const BigGlyph& BigA();
const BigGlyph& BigR();
const BigGlyph& BigX();
const BigGlyph& BigI();
const BigGlyph& BigP();
const BigGlyph& BigU();
const BigGlyph& BigD();
const BigGlyph& BigM();
const BigGlyph& BigN();
const BigGlyph& BigG();
const BigGlyph& BigB();
const BigGlyph& BigK();
const BigGlyph& BigV();
const BigGlyph& BigY();
const BigGlyph& BigO();
const BigGlyph& BigW(); // добавлено для кнопки "NEW GAME"

// Буквы, добавленные ради полного алфавита A-Z (нужно для ввода игроком
// произвольного имени сохранения, см. save/SaveSystem.h: SaveData::name,
// и для фразы предупреждения "ARE YOU SURE..." в экране подтверждения
// перезаписи, см. ui/MenuLayouts.cpp: BuildConfirmMenu()).
const BigGlyph& BigF();
const BigGlyph& BigH();
const BigGlyph& BigJ();
const BigGlyph& BigQ();
const BigGlyph& BigZ();

// Подчёркивание — см. комментарий у определения в BigFont.cpp.
const BigGlyph& BigUnderscore();

// Цифры 0-9 — добавлены для номеров слотов сохранения ("SLOT 1"/"SLOT 2"/
// "SLOT 3", см. ui/MenuLayouts.cpp: BuildContinueMenu()/BuildSaveMenu()):
// исходно в BigFont не было ни одной цифры (только буквы, использовавшиеся
// в CELL/START/SETTINGS/EXIT и т.п.), а различать слоты БУКВАМИ (SLOT A/B/C)
// не так очевидно игроку, как обычной нумерацией.
const BigGlyph& BigDigit(int d); // d: 0..9 (падает на BigE() вне диапазона)

// Пустой (полностью незалитый) узор — используется для пробела внутри
// многословных надписей кнопок (например "NEW GAME", "SLOT A"), чтобы
// GetBigGlyph() не подставляла на его место букву E по умолчанию.
const BigGlyph& BigSpace();

// Возвращает узор буквы c (не должно случаться при контролируемом вводе -
// падает обратно на BigE() для незнакомых символов). ' ' -> BigSpace().
const BigGlyph& GetBigGlyph(char c);

// Апскейл маски относительно исходных 5x7: finalRes = round(scale * kMaskUpsample).
constexpr int kMaskUpsample = 2;
int ComputeFinalRes(float scale);

// Рисует один крупный логотип-глиф с повышенным разрешением маски: каждый
// исходный "пиксель" паттерна раскладывается в finalRes x finalRes
// экранных клеток, каждая из которых получает свой (детерминированный по
// seed) плотный символ — соседние клетки почти всегда разные, силуэт
// буквы при этом не меняется.
void DrawBigGlyph(std::vector<unsigned char>& grid, int cols, int rows,
                   const BigGlyph& glyph, int originCol, int originRow,
                   float scale, int seed);

int BigGlyphWidth(float scale);
int BigGlyphHeight(float scale);
int BigTextWidth(const std::string& text, float scale);

// Крупный ASCII-арт текст (несколько букв подряд через GetBigGlyph()).
void DrawBigText(std::vector<unsigned char>& grid, int cols, int rows,
                  const std::string& text, int originCol, int originRow,
                  float scale, int seed);

} // namespace MainMenu
