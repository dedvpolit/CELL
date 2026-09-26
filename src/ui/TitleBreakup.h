#pragma once
#include <vector>
#include <string>
#include "UiGlyphs.h"

// Animation of the ASCII title "CELL" gradually breaking apart (clicks -> crumbling particles ->
// tilt -> the whole word falling). The state (TitleBreakupState) is kept separate from the menu
// layout, so buttons and their hover frames do not depend on the title animation.

namespace MainMenu {

struct TitleParticle {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float delay = 0.0f;
    float age = 0.0f;
    unsigned char glyph = GLYPH_HASH;
    bool active = false;
};

struct TitleBreakupState {
    int clickCount = 0;
    int wearLevel = 0;
    bool tiltEnabled = false;
    bool fullFalling = false;
    float tilt = 0.0f;
    float tiltTarget = 0.0f;
    float fallTime = 0.0f;
    float fallDuration = 1.35f;
    float fallDistance = 0.0f;
    float fallInitialSpeed = 4.0f;
    float fallStartTilt = 0.0f;
    float fallPivotX = 0.0f;
    float fallPivotY = 0.0f;
    int hangerSide = 1;
    std::vector<TitleParticle> particles;
    std::vector<unsigned char> removed;
};

void ResetTitleBreakup(TitleBreakupState& state);

float LerpAngle(float current, float target, float speed, float dt);

void UpdateTitleBreakup(TitleBreakupState& state, float deltaTime);

bool HasActiveTitleAnimation(const TitleBreakupState& state);

struct TitleCell {
    int x = 0;
    int y = 0;
    unsigned char glyph = GLYPH_HASH;
    bool removed = false;
};

void CollectTitleCells(
    std::vector<TitleCell>& outCells,
    const std::string& text, int originCol, int originRow, float scale,
    const TitleBreakupState& state);

std::vector<TitleCell> CollectTitleCells(
    const std::string& text, int originCol, int originRow, float scale,
    const TitleBreakupState& state);

void DrawRotatedCell(
    std::vector<unsigned char>& grid, int cols, int rows,
    int cellX, int cellY, unsigned char glyph,
    float pivotX, float pivotY, float angle, float offsetY);

void DrawBrokenTitle(
    std::vector<unsigned char>& grid, int cols, int rows,
    const std::string& text, int originCol, int originRow,
    float scale, const TitleBreakupState& state);

void ApplyTitleClick(TitleBreakupState& state,
                      int cols, int rows,
                      const std::string& text,
                      int originCol, int originRow,
                      float scale,
                      int seed);

} // namespace MainMenu
