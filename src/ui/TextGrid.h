#pragma once
#include <vector>
#include <string>
#include "UiGlyphs.h"

// Low-level primitives for writing characters into the CPU glyph grid (see
// AsciiEffect::setUIOverlay() for the 0 = empty / (idx + 1) encoding).

namespace MainMenu {

unsigned char CharToGlyph(char c);

// Writes one glyph into grid(col,row) with the +1 encoding. Silently ignores out-of-bounds
// coordinates, so text layout does not need to bounds-check every character/pixel itself. Note: it
// checks cols/rows, not grid.size(), so the caller must size grid to cols*rows.
void PutGlyph(std::vector<unsigned char>& grid, int cols, int rows,
              int col, int row, unsigned char glyphIndex);

void PutText(std::vector<unsigned char>& grid, int cols, int rows,
             int col, int row, const std::string& text);

// Chance of a "smoldering" accent glyph over the regular frame line in DrawBox() on hover (filled =
// true): t = 0 is the inner layer, t > 0 the outer layers. Only used inside DrawBox().
unsigned char PickHoverAccentChance(int t);

// A dark-fantasy-style frame: a ragged line of "drips" rather than a solid fill (the stamina bar's
// frame in ascii_post.frag is built on the same principle, in the shader instead of on the CPU).
// thickness is the frame width in cells. filled = true means the mouse is hovering: "dense" accent
// glyphs show through the ragged line here and there.
void DrawBox(std::vector<unsigned char>& grid, int cols, int rows,
             int x0, int y0, int x1, int y1, int thickness, bool filled, int seed);

} // namespace MainMenu
