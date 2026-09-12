#pragma once
#include <vector>
#include <string>
#include "UiGlyphs.h"

// ============================================================================
// TextGrid — низкоуровневые примитивы записи символов в CPU-сетку глифов
// (см. AsciiEffect::setUIOverlay() за форматом кодирования 0=пусто/(idx+1)).
// Вынесено из MainMenu.h при разбиении монолита на модули.
// ============================================================================

namespace MainMenu {

// Отображает символ латиницы на индекс фиксированного UI-шрифта (см.
// UiGlyphs.h). Незнакомые символы и пробел -> GLYPH_SPACE (пусто).
unsigned char CharToGlyph(char c);

// Пишет один глиф в grid(col,row) с encoding +1, см. AsciiEffect::setUIOverlay().
// Тихо игнорирует координаты за пределами сетки, чтобы раскладке текста не
// нужно было самой проверять границы на каждый символ/пиксель.
void PutGlyph(std::vector<unsigned char>& grid, int cols, int rows,
              int col, int row, unsigned char glyphIndex);

// Пишет строку символов подряд по горизонтали, начиная с (col,row).
// Пробел = не трогать клетку (фон/рамка снизу останутся видны).
void PutText(std::vector<unsigned char>& grid, int cols, int rows,
             int col, int row, const std::string& text);

// Шанс "тлеющего" акцентного символа поверх обычной линии рамки в
// DrawBox() при наведении курсора (filled=true) — t=0 внутренний слой,
// t>0 внешние слои. Используется только внутри DrawBox().
unsigned char PickHoverAccentChance(int t);

// Рамка в стиле dark fantasy — рваная линия из "капель", а не сплошная
// заливка (см. рамку полосы стамины в assets/shaders/ascii_post.frag,
// собранную по тому же принципу, но в шейдере, а не на CPU).
// thickness — толщина рамки в клетках. filled=true — наведён курсор
// мыши: поверх обычной рваной линии местами проступают "плотные"
// акцентные символы (см. PickDenseGlyph() в UiGlyphs.h).
void DrawBox(std::vector<unsigned char>& grid, int cols, int rows,
             int x0, int y0, int x1, int y1, int thickness, bool filled, int seed);

} // namespace MainMenu
