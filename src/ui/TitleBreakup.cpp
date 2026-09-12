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

// Обновляет нелинейное движение осыпавшихся ASCII-частей и самой надписи.
// Скорость падения задаётся через ускорение, поэтому она не линейна:
// каждый следующий кадр частицы/логотип падают быстрее предыдущего.
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

// Item 4 (review): writes into a caller-provided buffer instead of
// allocating and returning a fresh std::vector<TitleCell> — this is the
// hot path called every frame while the title is mid-animation (see
// DrawBrokenTitle() below), so the allocation pattern matters here the
// same way it did in DungeonScene::render() (Item 1). outCells.clear()
// keeps whatever capacity the caller's buffer already has; as long as the
// title text/scale stay the same (they always do — it's always "CELL" at
// the same scale) the cell count is stable frame to frame, so after the
// first call this never reallocates.
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

// Convenience wrapper for callers outside the per-frame hot path (e.g.
// ApplyTitleClick() below, which only runs once per mouse click, not once
// per frame) that would rather just get a vector back. Not used by
// DrawBrokenTitle() — that one uses the out-param overload above with a
// reused scratch buffer instead.
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

// Рисует CELL с тем же ASCII-артом, но уже как "лист", который можно
// постепенно ронять, наклонять на одну сторону и в финале полностью
// сорвать вниз. Для 5-го клика используется единое падение всего слова,
// а отдельные уже сорвавшиеся символы продолжают жить своей физикой.
void DrawBrokenTitle(
    std::vector<unsigned char>& grid, int cols, int rows,
    const std::string& text, int originCol, int originRow,
    float scale, const TitleBreakupState& state)
{
    const int titleWidth = BigTextWidth(text, scale);
    const int titleHeight = BigGlyphHeight(scale);

    // Пока CELL висит на одной стороне и когда оно уже сорвалось вниз,
    // используем ОДНУ И ТУ ЖЕ точку вращения. Это принципиально важно:
    // на пятом клике надпись не должна перепрыгивать в новое положение,
    // а должна продолжить падение ровно из того места, где висела.
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

        // Физическое движение с ускорением: в начале надпись почти стоит,
        // затем ускоряется и к концу падает значительно быстрее.
        wholeOffsetY = state.fallInitialSpeed * state.fallTime;
        wholeOffsetY += 0.5f * 2.0f *
            (state.fallDistance - state.fallInitialSpeed * state.fallDuration) /
            (state.fallDuration * state.fallDuration) *
            state.fallTime * state.fallTime;

        // На момент полного срыва оставляем ТОЧНО ТОТ ЖЕ наклон,
        // с которого надпись начала падать. В самом низу добавляем
        // только небольшой дополнительный крен — без смены позиции.
        const float landingTilt = 0.10f *
            (safeT * safeT * (3.0f - 2.0f * safeT));
        wholeAngle = state.fallStartTilt + landingTilt;
    }

    // Item 4 (review): reused static scratch buffer instead of a fresh
    // heap allocation every frame — see the CollectTitleCells() out-param
    // overload above. DrawBrokenTitle() is called every frame while the
    // title is mid-animation (see main.cpp's menuLayoutDirty handling), so
    // this is a real per-frame allocation eliminated, not a one-off.
    static std::vector<TitleCell> s_titleCellsScratch;
    CollectTitleCells(s_titleCellsScratch, text, originCol, originRow, scale, state);
    const std::vector<TitleCell>& cells = s_titleCellsScratch;

    for (const TitleCell& cell : cells) {
        if (cell.removed) continue;

        unsigned char glyph = cell.glyph;

        // С каждым кликом поверхность CELL выглядит всё более
        // потёртой: часть плотных ASCII-знаков исчезает, а часть
        // заменяется на мелкие рваные следы. Распределение
        // детерминированное, поэтому потёртости не мерцают между кадрами.
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

    // Отдельные осколки, которые уже сорвались с надписи.
    for (const TitleParticle& p : state.particles) {
        if (!p.active || p.age < p.delay) continue;
        PutGlyph(grid, cols, rows,
                 (int)std::lround(p.x),
                 (int)std::lround(p.y),
                 p.glyph);
    }

    // На 3-м клике один верхний угол остаётся условной точкой подвеса.
    // Это не "обязательный гвоздь": всего лишь визуальный акцент на том,
    // что надпись перекосилась и держится за одну сторону.
    if (state.tiltEnabled && !state.fullFalling) {
        const int hookX = (int)std::lround(pivotX);
        const int hookY = (int)std::lround(pivotY - 1.0f);
        PutGlyph(grid, cols, rows, hookX, hookY, GLYPH_CORNER);
    }

    (void)titleHeight;
}

// Одно нажатие по CELL срывает только небольшой случайный набор клеток.
// Для первых четырёх нажатий число разрушенных символов разное, чтобы
// надпись не разрушалась одинаково от запуска к запуску. На третьем
// нажатии добавляется перекос на одну сторону, на пятом — падает весь
// оставшийся логотип.
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
        // С каждым новым нажатием осыпается гарантированно больше
        // ASCII-клеток, но внутри одного шага количество всё равно
        // немного гуляет, чтобы разрушение не выглядело механическим.
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

            // Положительный угол в экранных координатах опускает
            // правую сторону CELL вниз, а не поднимает её вверх.
            state.tiltTarget = 0.28f;
        }
    } else {
        // Пятый клик: сначала надпись уже изрядно испорчена,
        // а теперь остаток целиком срывается вниз.
        state.fullFalling = true;
        state.tiltEnabled = true;
        if (!state.hangerSide) {
            state.hangerSide = 1;
        }

        // Никакого перескока в новое положение: пятая стадия начинается
        // с фактического угла и той же точки подвеса, на которой CELL
        // висел до этого.
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
