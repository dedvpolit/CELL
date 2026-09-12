#pragma once
#include <vector>
#include <string>
#include "UiGlyphs.h"

// ============================================================================
// TitleBreakup — анимация постепенного разрушения ASCII-заголовка "CELL"
// (клики -> осыпающиеся частицы -> перекос -> полное падение всей надписи).
// Состояние (TitleBreakupState) хранится отдельно от раскладки меню,
// поэтому кнопки и их hover-рамки не зависят от анимации заголовка.
// Вынесено из MainMenu.h при разбиении монолита на модули.
// ============================================================================

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

// Обновляет нелинейное движение осыпавшихся ASCII-частей и самой надписи.
// Скорость падения задаётся через ускорение (не линейна).
void UpdateTitleBreakup(TitleBreakupState& state, float deltaTime);

bool HasActiveTitleAnimation(const TitleBreakupState& state);

struct TitleCell {
    int x = 0;
    int y = 0;
    unsigned char glyph = GLYPH_HASH;
    bool removed = false;
};

// Пишет во ВНЕШНИЙ буфер (не аллоцирует каждый кадр) — используется в
// горячем пути DrawBrokenTitle(), вызываемом каждый кадр во время анимации.
void CollectTitleCells(
    std::vector<TitleCell>& outCells,
    const std::string& text, int originCol, int originRow, float scale,
    const TitleBreakupState& state);

// Удобная обёртка для вызывающих ВНЕ горячего пути (например, ApplyTitleClick()
// — вызывается один раз на клик, не каждый кадр), возвращает вектор.
std::vector<TitleCell> CollectTitleCells(
    const std::string& text, int originCol, int originRow, float scale,
    const TitleBreakupState& state);

void DrawRotatedCell(
    std::vector<unsigned char>& grid, int cols, int rows,
    int cellX, int cellY, unsigned char glyph,
    float pivotX, float pivotY, float angle, float offsetY);

// Рисует CELL с тем же ASCII-артом, но как "лист", который можно постепенно
// ронять, наклонять на одну сторону и в финале полностью сорвать вниз.
void DrawBrokenTitle(
    std::vector<unsigned char>& grid, int cols, int rows,
    const std::string& text, int originCol, int originRow,
    float scale, const TitleBreakupState& state);

// Одно нажатие по CELL срывает небольшой случайный набор клеток. Для
// первых четырёх нажатий число разрушенных символов разное; на третьем
// добавляется перекос на одну сторону, на пятом падает весь логотип.
void ApplyTitleClick(TitleBreakupState& state,
                      int cols, int rows,
                      const std::string& text,
                      int originCol, int originRow,
                      float scale,
                      int seed);

} // namespace MainMenu
