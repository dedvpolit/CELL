#include "TitleBreakup.h"
#include "TextGrid.h"
#include "BigFont.h"
#include "UiGlyphs.h"
#include <algorithm>
#include <cmath>

namespace MainMenu {


void ResetTitleBreakup(TitleBreakupState& state) {
    state = TitleBreakupState{};
    state.particles.clear();
    state.removed.clear();
    state.wearLevel = 0;
}

float LerpAngle(float current, float target, float speed, float dt) {
    const float t = 1.0f - std::exp(-speed * dt);
    return current + (target - current) * t;
}

// Updates the nonlinear motion of crumbled ASCII pieces and of the word itself. Fall speed is
// driven by acceleration, so each frame the particles/logo fall faster than the last.
void UpdateTitleBreakup(TitleBreakupState& state, float deltaTime) {
    deltaTime = std::clamp(deltaTime, 0.0f, 0.05f);

    if (state.tiltEnabled) {
        state.tilt = LerpAngle(state.tilt, state.tiltTarget, 8.0f, deltaTime);
    }

    for (TitleParticle& p : state.particles) {
        if (!p.active) continue;
        p.age += deltaTime;
        if (p.age < p.delay) continue;

        const float localDt = deltaTime;
        p.vy += 18.0f * localDt;
        p.vy = std::min(p.vy, 16.0f);
        p.vx *= std::exp(-1.8f * localDt);
        p.x += p.vx * localDt;
        p.y += p.vy * localDt;

        if (p.age > 1.15f) {
            p.active = false;
        }
    }

    if (state.fullFalling) {
        state.fallTime = std::min(state.fallTime + deltaTime, state.fallDuration);

        if (state.fallTime >= state.fallDuration) {
            const float sign = state.hangerSide >= 0 ? 1.0f : -1.0f;
            state.tilt = state.tiltTarget + sign * 0.10f;
        }
    }
}

bool HasActiveTitleAnimation(const TitleBreakupState& state) {
    if (state.fullFalling && state.fallTime < state.fallDuration) return true;
    for (const TitleParticle& p : state.particles) {
        if (p.active) return true;
    }
    return state.tiltEnabled && std::abs(state.tilt - state.tiltTarget) > 0.002f;
}

// Writes into a caller-provided buffer: this is the per-frame hot path during the animation, and
// clear() keeps the buffer's capacity, so it does not allocate after the first call.
void CollectTitleCells(
    std::vector<TitleCell>& outCells,
    const std::string& text, int originCol, int originRow, float scale,
    const TitleBreakupState& state)
{
    const int finalRes = ComputeFinalRes(scale);
    const int letterW = BigGlyphWidth(scale);
    const int gap = finalRes;

    outCells.clear();
    int col = originCol;

    int flatIndex = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const BigGlyph& glyph = GetBigGlyph(text[i]);
        for (int r = 0; r < 7; ++r) {
            for (int c = 0; c < 5; ++c) {
                if (glyph.rows[r][c] != '#') continue;
                for (int sy = 0; sy < finalRes; ++sy) {
                    for (int sx = 0; sx < finalRes; ++sx) {
                        TitleCell cell;
                        cell.x = col + c * finalRes + sx;
                        cell.y = originRow + r * finalRes + sy;
                        cell.glyph = PickDenseGlyph(cell.x, cell.y, 1917 + (int)i * 7919);
                        cell.removed = flatIndex < (int)state.removed.size()
                            ? state.removed[(size_t)flatIndex] != 0
                            : false;
                        outCells.push_back(cell);
                        ++flatIndex;
                    }
                }
            }
        }
        col += letterW + gap;
    }
}

// Convenience wrapper for callers outside the per-frame hot path (e.g. ApplyTitleClick() below,
// which runs once per mouse click) that just want a vector back. DrawBrokenTitle() does not use it:
// it uses the out-param overload above with a reused scratch buffer.
std::vector<TitleCell> CollectTitleCells(
    const std::string& text, int originCol, int originRow, float scale,
    const TitleBreakupState& state)
{
    std::vector<TitleCell> cells;
    CollectTitleCells(cells, text, originCol, originRow, scale, state);
    return cells;
}

void DrawRotatedCell(
    std::vector<unsigned char>& grid, int cols, int rows,
    int cellX, int cellY, unsigned char glyph,
    float pivotX, float pivotY, float angle, float offsetY)
{
    const float c = std::cos(angle);
    const float s = std::sin(angle);

    const float dx = (float)cellX - pivotX;
    const float dy = (float)cellY - pivotY;

    const float rx = pivotX + dx * c - dy * s;
    const float ry = pivotY + dx * s + dy * c + offsetY;

    const int gc = (int)std::lround(rx);
    const int gr = (int)std::lround(ry);
    PutGlyph(grid, cols, rows, gc, gr, glyph);
}

// Draws CELL with the same ASCII art, but as a "sheet" that can be gradually dropped, tilted to one
// side and finally torn off entirely. The 5th click uses a single fall for the whole word, while
// individual already detached characters keep their own physics.
void DrawBrokenTitle(
    std::vector<unsigned char>& grid, int cols, int rows,
    const std::string& text, int originCol, int originRow,
    float scale, const TitleBreakupState& state)
{
    const int titleWidth = BigTextWidth(text, scale);
    const int titleHeight = BigGlyphHeight(scale);

    // The same pivot point is used both while CELL hangs on one side and once it has fallen. This
    // is critical: on the fifth click the word must not jump to a new position, it must continue
    // falling exactly from where it was hanging.
    const float pivotX = state.fullFalling
        ? state.fallPivotX
        : originCol + (state.hangerSide >= 0 ? titleWidth - 1 : 0);
    const float pivotY = state.fullFalling
        ? state.fallPivotY
        : (float)originRow + 1.0f;

    float wholeOffsetY = 0.0f;
    float wholeAngle = state.tilt;

    if (state.fullFalling) {
        const float t = state.fallDuration > 0.0f
            ? state.fallTime / state.fallDuration
            : 1.0f;
        const float safeT = std::clamp(t, 0.0f, 1.0f);

        wholeOffsetY = state.fallInitialSpeed * state.fallTime;
        wholeOffsetY += 0.5f * 2.0f *
            (state.fallDistance - state.fallInitialSpeed * state.fallDuration) /
            (state.fallDuration * state.fallDuration) *
            state.fallTime * state.fallTime;

        // At the moment of full detachment, keep exactly the same tilt the word started falling
        // from. Only a small extra lean is added at the bottom, with no position change.
        const float landingTilt = 0.10f *
            (safeT * safeT * (3.0f - 2.0f * safeT));
        wholeAngle = state.fallStartTilt + landingTilt;
    }

    // A reused static scratch buffer instead of a fresh heap allocation every frame (see the
    // CollectTitleCells() out-param overload above). DrawBrokenTitle() is called every frame while
    // the title is mid-animation (the menu layout is marked dirty every frame, see
    // Application.cpp), so this eliminates a real per-frame allocation.
    static std::vector<TitleCell> s_titleCellsScratch;
    CollectTitleCells(s_titleCellsScratch, text, originCol, originRow, scale, state);
    const std::vector<TitleCell>& cells = s_titleCellsScratch;

    for (const TitleCell& cell : cells) {
        if (cell.removed) continue;

        unsigned char glyph = cell.glyph;

        // With each click CELL's surface looks more worn: some dense ASCII glyphs disappear, others
        // are replaced with small ragged marks. The distribution is deterministic, so the wear does
        // not flicker between frames.
        if (state.wearLevel > 0) {
            const unsigned int wearHash =
                Hash(cell.x * 92821 + cell.y * 68917, 1703);
            const unsigned int wearRoll = wearHash % 100u;

            const unsigned int blankChance =
                (unsigned int)std::min(18, state.wearLevel * 4);
            const unsigned int scuffChance =
                (unsigned int)std::min(12, state.wearLevel * 3);

            if (wearRoll < blankChance) {
                continue;
            }

            if (wearRoll < blankChance + scuffChance) {
                glyph = (wearHash & 1u)
                    ? GLYPH_DRIP_SMALL
                    : GLYPH_DRIP_BIG;
            }
        }

        DrawRotatedCell(grid, cols, rows, cell.x, cell.y, glyph,
                        pivotX, pivotY, wholeAngle, wholeOffsetY);
    }

    for (const TitleParticle& p : state.particles) {
        if (!p.active || p.age < p.delay) continue;
        PutGlyph(grid, cols, rows,
                 (int)std::lround(p.x),
                 (int)std::lround(p.y),
                 p.glyph);
    }

    if (state.tiltEnabled && !state.fullFalling) {
        const int hookX = (int)std::lround(pivotX);
        const int hookY = (int)std::lround(pivotY - 1.0f);
        PutGlyph(grid, cols, rows, hookX, hookY, GLYPH_CORNER);
    }

    (void)titleHeight;
}

// One click on CELL tears off only a small random set of cells. The first four clicks each break
// off a different number of characters, so the word does not break apart the same way every run.
// The third click adds a tilt to one side, the fifth drops the whole remaining logo.
void ApplyTitleClick(TitleBreakupState& state,
                            int cols, int rows,
                            const std::string& text,
                            int originCol, int originRow,
                            float scale,
                            int seed)
{
    if (state.fullFalling || state.clickCount >= 5) return;

    const std::vector<TitleCell> cells =
        CollectTitleCells(text, originCol, originRow, scale, state);

    if (state.removed.empty()) {
        state.removed.assign(cells.size(), 0);
    } else if (state.removed.size() != cells.size()) {
        std::vector<unsigned char> resized(cells.size(), 0);
        const size_t copyCount = std::min(resized.size(), state.removed.size());
        std::copy(state.removed.begin(), state.removed.begin() + (ptrdiff_t)copyCount,
                  resized.begin());
        state.removed.swap(resized);
    }

    ++state.clickCount;
    state.wearLevel = std::min(4, state.clickCount);

    if (state.clickCount < 5) {
        // Each new click guarantees more crumbling ASCII cells, but the count still varies a bit
        // within one step, so the breakup does not look mechanical.
        const int baseDropCount = 4 + state.clickCount * 3;
        const int dropCount = baseDropCount +
            (int)(Hash(seed + state.clickCount * 271, 4401) % 3u);

        std::vector<int> candidates;
        candidates.reserve(cells.size());
        for (size_t i = 0; i < cells.size(); ++i) {
            if (!state.removed[i]) candidates.push_back((int)i);
        }

        for (int n = 0; n < dropCount && !candidates.empty(); ++n) {
            const unsigned int h = Hash(seed + state.clickCount * 997 + n * 37, 8123);
            const size_t pick = h % candidates.size();
            const int cellIndex = candidates[pick];
            candidates.erase(candidates.begin() + (ptrdiff_t)pick);

            state.removed[(size_t)cellIndex] = 1;

            const TitleCell& cell = cells[(size_t)cellIndex];

            TitleParticle particle;
            particle.x = (float)cell.x;
            particle.y = (float)cell.y;
            particle.vx = ((float)(h % 7u) - 3.0f) * 0.45f;
            particle.vy = 0.5f + (float)((h >> 8) % 4u) * 0.35f;
            particle.delay = 0.02f + (float)((h >> 16) % 10u) * 0.008f;
            particle.age = 0.0f;
            particle.glyph = cell.glyph;
            particle.active = true;
            state.particles.push_back(particle);
        }

        if (state.clickCount == 3) {
            state.tiltEnabled = true;
            state.hangerSide =
                (Hash(seed, 7711) & 1u) ? 1 : -1;

            state.tiltTarget = 0.28f;
        }
    } else {
        state.fullFalling = true;
        state.tiltEnabled = true;
        if (!state.hangerSide) {
            state.hangerSide = 1;
        }

        // No jump to a new position: the fifth stage starts from the actual angle and the same
        // hanging point CELL was at before.
        state.fallStartTilt = state.tilt;
        const int titleWidth = BigTextWidth(text, scale);
        state.fallPivotX = originCol + (state.hangerSide >= 0 ? titleWidth - 1 : 0);
        state.fallPivotY = (float)originRow + 1.0f;
        state.tiltTarget = state.fallStartTilt;
        state.fallTime = 0.0f;
        state.fallInitialSpeed = 4.0f;
        state.fallDuration = 1.30f;

        const int titleHeight = BigGlyphHeight(scale);
        state.fallDistance =
            (float)std::max(8, rows - originRow + titleHeight + 4);
    }

    (void)cols;
}

} // namespace MainMenu
