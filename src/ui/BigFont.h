#pragma once
#include <vector>
#include <string>

// Large block logo font: 5x7 dot-matrix letters used for the "CELL" title and the button text. Each
// letter is drawn with an upscaled mask, where every "pixel" of the pattern is filled with its own
// (seed-deterministic) dense glyph from UiGlyphs::kDenseFillGlyphs; see DrawBigGlyph() below.

namespace MainMenu {

struct BigGlyph { const char* rows[7]; };

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
const BigGlyph& BigW();

const BigGlyph& BigF();
const BigGlyph& BigH();
const BigGlyph& BigJ();
const BigGlyph& BigQ();
const BigGlyph& BigZ();

const BigGlyph& BigUnderscore();

const BigGlyph& BigDigit(int d); // d: 0..9 (falls back to BigE() out of range)

const BigGlyph& BigSpace();

const BigGlyph& GetBigGlyph(char c);

constexpr int kMaskUpsample = 2;
int ComputeFinalRes(float scale);

// Draws one large logo glyph with an upscaled mask: each original "pixel" of the pattern expands
// into finalRes x finalRes screen cells, each getting its own (seed-deterministic) dense glyph, so
// neighboring cells are almost always different while the letter's silhouette stays unchanged.
void DrawBigGlyph(std::vector<unsigned char>& grid, int cols, int rows,
                   const BigGlyph& glyph, int originCol, int originRow,
                   float scale, int seed);

int BigGlyphWidth(float scale);
int BigGlyphHeight(float scale);
int BigTextWidth(const std::string& text, float scale);

void DrawBigText(std::vector<unsigned char>& grid, int cols, int rows,
                  const std::string& text, int originCol, int originRow,
                  float scale, int seed);

} // namespace MainMenu
