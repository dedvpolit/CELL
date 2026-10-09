#pragma once
#include <vector>
#include <string>
#include "UiGlyphs.h"

// Low-level primitives for writing characters into the CPU glyph grid
//(see AsciiEffect::setUIOverlay() for the 0 = empty / (idx + 1) encoding)

namespace MainMenu {

unsigned char CharToGlyph(char c);

// Writes glyph + 1; out-of-bounds writes are ignored
// The caller guarantees grid.size() == cols * rows
void PutGlyph(std::vector<unsigned char>& grid, int cols, int rows,
              int col, int row, unsigned char glyphIndex);

void PutText(std::vector<unsigned char>& grid, int cols, int rows,
             int col, int row, const std::string& text);

// Chance of an accent glyph on hovered frames, by layer
unsigned char PickHoverAccentChance(int t);

// Worn frame with drips and gaps, thickness in cells
// The look depends only on the seed, a frame does not change on hover;
// `filled` adds the hover accents
void DrawBox(std::vector<unsigned char>& grid, int cols, int rows,
             int x0, int y0, int x1, int y1, int thickness, bool filled, int seed);

} // namespace MainMenu
