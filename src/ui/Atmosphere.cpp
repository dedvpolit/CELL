#include "Atmosphere.h"
#include "TextGrid.h"
#include "UiGlyphs.h"
#include <algorithm>
#include <cmath>

namespace MainMenu {

void DrawWornEdgeV(std::vector<unsigned char>& grid, int cols, int rows,
                           int x, int y0, int y1, int seed, bool thin) {
    for (int y = y0; y <= y1; ++y) {
        const unsigned int h = Hash(y, seed);
        const unsigned int skipMod = thin ? 3u : 6u; // thin пропускает чаще — реже видна
        if (h % skipMod == 0u) continue;
        unsigned char g = GLYPH_VLINE;
        if (h % 13u == 0u)      g = GLYPH_DRIP_BIG;
        else if (h % 13u == 1u) g = GLYPH_DRIP_SMALL;
        PutGlyph(grid, cols, rows, x, y, g);
    }
}

void DrawWornEdgeH(std::vector<unsigned char>& grid, int cols, int rows,
                           int y, int x0, int x1, int seed, bool thin) {
    for (int x = x0; x <= x1; ++x) {
        const unsigned int h = Hash(x, seed);
        const unsigned int skipMod = thin ? 3u : 6u;
        if (h % skipMod == 0u) continue;
        unsigned char g = GLYPH_HLINE;
        if (h % 13u == 0u)      g = GLYPH_DRIP_BIG;
        else if (h % 13u == 1u) g = GLYPH_DRIP_SMALL;
        PutGlyph(grid, cols, rows, x, y, g);
    }
}

// Рисует кольцо-сигилу из структурных глифов (не текст, не '#') —
// приближённая окружность по алгоритму Брезенхэма, где каждая точка
// получает HLINE/VLINE/CORNER в зависимости от локального наклона дуги
// (плоские участки сверху/снизу — HLINE, слева/справа — VLINE, углы дуги —
// CORNER), плюс несколько "лучей"-засечек наружу и GLYPH_CIRCLE в центре.
// Не идеальная окружность — часть точек кольца случайно пропускается
// (рваный обод), а не все 8 лучей рисуются — асимметричный "потухший" знак.
void DrawRuneCircle(std::vector<unsigned char>& grid, int cols, int rows,
                            int cx, int cy, int radius, int seed) {
    int x = radius, y = 0, err = 0;
    while (x >= y) {
        const int pts[8][2] = {
            { cx + x, cy + y }, { cx + y, cy + x },
            { cx - y, cy + x }, { cx - x, cy + y },
            { cx - x, cy - y }, { cx - y, cy - x },
            { cx + y, cy - x }, { cx + x, cy - y },
        };
        for (int i = 0; i < 8; ++i) {
            const unsigned int h = Hash(pts[i][0] * 61 + pts[i][1] * 37, seed);
            if (h % 6u == 0u) continue; // рваный обод — не все точки на месте
            const int ddx = pts[i][0] - cx;
            const int ddy = pts[i][1] - cy;
            unsigned char g;
            if (std::abs(ddx) > std::abs(ddy) * 2)       g = GLYPH_HLINE;
            else if (std::abs(ddy) > std::abs(ddx) * 2)  g = GLYPH_VLINE;
            else                                          g = GLYPH_CORNER;
            PutGlyph(grid, cols, rows, pts[i][0], pts[i][1], g);
        }
        if (err <= 0) { ++y; err += 2 * y + 1; }
        if (err > 0)  { --x; err -= 2 * x + 1; }
    }

    // Лучи наружу — максимум 4 из 4 кардинальных направлений, вразнобой.
    const int dirs[4][2] = { {0,-1}, {0,1}, {-1,0}, {1,0} };
    for (int i = 0; i < 4; ++i) {
        if (Hash(seed + i * 271, 909) % 3u == 0u) continue; // часть лучей отсутствует
        const int rx = cx + dirs[i][0] * (radius + 2);
        const int ry = cy + dirs[i][1] * (radius + 2);
        PutGlyph(grid, cols, rows, rx, ry,
                 (dirs[i][0] != 0) ? GLYPH_HLINE : GLYPH_VLINE);
    }

    PutGlyph(grid, cols, rows, cx, cy, GLYPH_CIRCLE);
}

// Рваная трещина, идущая от угла экрана внутрь по диагонали. Не прямая
// линия — на каждом шаге с некоторой вероятностью "виляет" на соседнюю
// клетку (влево/вправо от основного направления), как настоящая трещина
// в камне. dirX/dirY — куда идёт трещина (обычно от угла к центру,
// например dirX=1,dirY=1 — из левого верхнего угла). maxLen ограничивает
// её длину, чтобы она не доходила до кнопок/заголовка.
void DrawCrack(std::vector<unsigned char>& grid, int cols, int rows,
                       int startX, int startY, int dirX, int dirY,
                       int maxLen, int seed) {
    float x = (float)startX, y = (float)startY;
    int wobble = 0;
    for (int i = 0; i < maxLen; ++i) {
        const unsigned int h = Hash(i * 97, seed);
        // Основной шаг — по диагонали; раз в несколько шагов добавляем
        // "виляние" на 1 клетку вбок, накопительно (wobble), чтобы
        // трещина плавно уходила в сторону, а не дрожала туда-сюда.
        if (h % 4u == 0u) wobble += ((h >> 3) & 1u) ? 1 : -1;
        wobble = std::clamp(wobble, -3, 3);

        x += (float)dirX;
        y += (float)dirY;
        const int gx = (int)std::lround(x) + (dirY != 0 ? wobble : 0);
        const int gy = (int)std::lround(y) + (dirX != 0 ? wobble : 0);

        unsigned char g = (h % 7u == 0u) ? GLYPH_DRIP_SMALL
                         : (h % 7u == 1u) ? GLYPH_DRIP_BIG
                         : GLYPH_HLINE;
        PutGlyph(grid, cols, rows, gx, gy, g);

        // Трещина "затухает" ближе к концу — не все клетки последней
        // трети рисуются, острие получается неровным, а не обрубленным.
        if (i > maxLen * 2 / 3 && h % 3u == 0u) continue;
    }
}


// угольков-осколков (GLYPH_CIRCLE/DRIP_SMALL) вокруг него, вразнобой,
// не симметрично — деталь для sconce/канделябра на стене.
void DrawTorch(std::vector<unsigned char>& grid, int cols, int rows,
                       int x, int y, int seed) {
    PutGlyph(grid, cols, rows, x, y, GLYPH_TORCH);
    for (int i = 0; i < 3; ++i) {
        const unsigned int h = Hash(seed + i * 17, x * 53 + y * 91);
        if (h % 5u == 0u) continue; // не всегда все три — иначе выглядит штампом
        const int dx = (int)(h % 3u) - 1;      // -1,0,1
        const int dy = 1 + (int)((h >> 4) % 2u); // капает вниз, не вверх
        PutGlyph(grid, cols, rows, x + dx, y + dy,
                 (h & 1u) ? GLYPH_DRIP_SMALL : GLYPH_CIRCLE);
    }
}

// ---- Вариации композиции и уникальные dark-fantasy мотивы ----
// Рисует короткую горизонтальную "перемычку" между верхом пилона и
// внешней рамкой — используется вариантами с pylonCrossbar=true, чтобы
// пилон визуально не "висел в воздухе", а был частью общей конструкции.
// Рисуется той же истёртой линией (DrawWornEdgeH), просто короче.
void DrawPylonCrossbar(std::vector<unsigned char>& grid, int cols, int rows,
                               int pylonX, int frameEdgeY, int towardCol, int seed) {
    const int x0 = std::min(pylonX, towardCol);
    const int x1 = std::max(pylonX, towardCol);
    if (x1 - x0 < 1) return;
    DrawWornEdgeH(grid, cols, rows, frameEdgeY, x0, x1, seed, true);
}

// Мелкий диагональный штрих у угла — альтернатива/дополнение к
// cornerOrGap() внутри DrawDarkFantasyAtmosphere: пара засечек, идущих
// внутрь от самого угла рамки, вместо (или вместе с) обвалившегося
// GLYPH_CORNER. dx/dy — направление внутрь экрана (+1/-1).
void DrawCornerTick(std::vector<unsigned char>& grid, int cols, int rows,
                            int x, int y, int dx, int dy, int seed) {
    if (Hash(x * 13 + y * 7, seed) % 4u == 0u) return; // не всегда — асимметрия
    PutGlyph(grid, cols, rows, x + dx, y, GLYPH_HLINE);
    PutGlyph(grid, cols, rows, x, y + dy, GLYPH_VLINE);
}

// Две малые руны, симметрично по бокам верхнего зазора (вместо одной
// центральной) — используется runeMode=4. cx/cy — центр между ними.
void DrawTwinRunes(std::vector<unsigned char>& grid, int cols, int rows,
                           int cx, int cy, int spacing, int radius, int seed) {
    DrawRuneCircle(grid, cols, rows, cx - spacing, cy, radius, seed);
    DrawRuneCircle(grid, cols, rows, cx + spacing, cy, radius, seed + 1);
}


// ---------------------------------------------------------------------------
// Дополнительные уникальные мотивы атмосферы.
// ВАЖНО: все эти функции работают ТОЛЬКО за пределами content-bbox.
// Они не заменяют guard-логику существующей композиции, а добавляют
// независимые декоративные слои. Благодаря этому новый дизайн не способен
// залезть на CELL/START/EXIT/RESUME/MENU даже на тесном разрешении.
// ---------------------------------------------------------------------------
bool AtmosphereFreeCell(int x, int y, int cols, int rows,
                               const AtmosphereBounds& content, int pad) {
    if (x < 0 || x >= cols || y < 0 || y >= rows) return false;
    return !(x >= content.x0 - pad && x <= content.x1 + pad &&
             y >= content.y0 - pad && y <= content.y1 + pad);
}

void SafeAtmosphereGlyph(std::vector<unsigned char>& grid, int cols, int rows,
                                int x, int y, unsigned char glyph,
                                const AtmosphereBounds& content, int pad) {
    if (AtmosphereFreeCell(x, y, cols, rows, content, pad)) {
        PutGlyph(grid, cols, rows, x, y, glyph);
    }
}

// Вертикальные "кровавые" потёки: в монохромном ASCII кровь передаётся
// не цветом, а характерными тяжёлыми каплями и редкими сгустками-кружками.
void DrawBloodDrips(std::vector<unsigned char>& grid, int cols, int rows,
                           const AtmosphereBounds& content, int seed,
                           bool fromTop, bool fromBottom, int count) {
    const int edge = 2;
    for (int i = 0; i < count; ++i) {
        unsigned int h = Hash(seed + i * 7919, seed + 17);
        const int x = edge + 2 + (int)(h % (unsigned int)std::max(1, cols - edge * 2 - 4));

        if (fromTop) {
            const int maxLen = std::max(2, std::min(10, content.y0 - edge - 3));
            if (maxLen >= 2) {
                const int len = 2 + (int)((h >> 8) % (unsigned int)std::max(1, maxLen - 1));
                int y = edge + 1 + (int)((h >> 16) % (unsigned int)std::max(1, maxLen - 1));
                for (int k = 0; k < len; ++k) {
                    const unsigned int s = Hash(k * 113 + i * 37, seed + 201);
                    SafeAtmosphereGlyph(grid, cols, rows, x, y + k,
                        (k == len - 1 || s % 7u == 0u) ? GLYPH_DRIP_BIG : GLYPH_DRIP_SMALL,
                        content, 3);
                }
                SafeAtmosphereGlyph(grid, cols, rows, x, y + len,
                                   GLYPH_CIRCLE, content, 3);
            }
        }

        if (fromBottom) {
            const int maxLen = std::max(2, std::min(9, (rows - edge - 2) - content.y1));
            if (maxLen >= 2) {
                const int len = 2 + (int)((h >> 20) % (unsigned int)std::max(1, maxLen - 1));
                int y = rows - edge - 2 - (int)((h >> 24) % (unsigned int)std::max(1, maxLen - 1));
                for (int k = 0; k < len; ++k) {
                    const unsigned int s = Hash(k * 127 + i * 43, seed + 211);
                    SafeAtmosphereGlyph(grid, cols, rows, x, y - k,
                        (k == len - 1 || s % 7u == 0u) ? GLYPH_DRIP_BIG : GLYPH_DRIP_SMALL,
                        content, 3);
                }
                SafeAtmosphereGlyph(grid, cols, rows, x, y - len,
                                   GLYPH_CIRCLE, content, 3);
            }
        }
    }
}

// Цепи: перемежение | + O + |. Хорошо читается как подвесы/канделябры,
// но не повторяет существующие прямые пилоны.
void DrawHangingChains(std::vector<unsigned char>& grid, int cols, int rows,
                              const AtmosphereBounds& content, int seed,
                              int count, bool bothSides) {
    const int edge = 2;
    const int leftRoom = std::max(0, content.x0 - edge - 5);
    const int rightRoom = std::max(0, cols - edge - 4 - content.x1);
    if (leftRoom < 3 && rightRoom < 3) return;

    for (int i = 0; i < count; ++i) {
        const unsigned int h = Hash(seed + i * 1009, seed + 33);
        bool left = (h & 1u) == 0u;
        if (!bothSides) left = (i & 1) == 0;
        if (left && leftRoom < 3) left = false;
        if (!left && rightRoom < 3) left = true;

        const int room = left ? leftRoom : rightRoom;
        const int x = left ? edge + 2 + (int)((h >> 7) % (unsigned int)std::max(1, room - 1))
                           : content.x1 + 4 + (int)((h >> 7) % (unsigned int)std::max(1, room - 1));
        const int top = edge + 2 + (int)((h >> 15) % 3u);
        const int maxLen = std::max(2, std::min(12, content.y0 - top - 3));
        if (maxLen < 2) continue;

        const int len = std::max(2, 3 + (int)((h >> 20) % (unsigned int)std::max(1, maxLen - 2)));
        for (int k = 0; k < len; ++k) {
            const int y = top + k;
            const unsigned char g = (k % 3 == 2) ? GLYPH_CIRCLE : GLYPH_VLINE;
            SafeAtmosphereGlyph(grid, cols, rows, x, y, g, content, 3);
        }
        SafeAtmosphereGlyph(grid, cols, rows, x, top + len, GLYPH_CORNER, content, 3);
    }
}

// Битые "ребра" вокруг пустого поля — костяная/обрушенная архитектура.
// Это намеренно не замкнутый прямоугольник, чтобы не превращаться ещё в одну рамку.
void DrawBrokenRibs(std::vector<unsigned char>& grid, int cols, int rows,
                           const AtmosphereBounds& content, int seed,
                           int ribCount, bool topHeavy) {
    const int edge = 2;
    const int gap = 3;
    const int topRoom = content.y0 - edge - gap;
    const int bottomRoom = rows - edge - 2 - content.y1;

    if (topRoom >= 4) {
        for (int i = 0; i < ribCount; ++i) {
            const unsigned int h = Hash(seed + i * 613, seed + 91);
            const int span = std::max(3, (content.x1 - content.x0) / std::max(2, ribCount));
            const int x0 = std::clamp(content.x0 + i * span / 2 - span / 3, edge + 1, cols - edge - 2);
            const int len = std::min(span + 2 + (int)(h % 4u), std::max(3, topRoom - 1));
            const int y = topRoom - 1 - (int)(h % (unsigned int)std::max(1, std::min(3, topRoom - 2)));
            for (int k = 0; k < len; ++k) {
                const int x = x0 + k;
                if (x >= cols - edge - 1) break;
                const unsigned char g = (k == 0 || k == len - 1)
                    ? GLYPH_CORNER
                    : ((h + (unsigned int)k) % 5u == 0u ? GLYPH_DRIP_SMALL : GLYPH_HLINE);
                SafeAtmosphereGlyph(grid, cols, rows, x, y, g, content, 3);
            }
            if (!topHeavy && topRoom >= 6) {
                SafeAtmosphereGlyph(grid, cols, rows, x0 + len / 2, y + 1,
                                    GLYPH_VLINE, content, 3);
            }
        }
    }

    if (bottomRoom >= 4 && !topHeavy) {
        for (int i = 0; i < std::max(2, ribCount - 1); ++i) {
            const unsigned int h = Hash(seed + 2200 + i * 733, seed + 97);
            const int span = std::max(3, (content.x1 - content.x0) / std::max(2, ribCount - 1));
            const int x0 = std::clamp(content.x0 + i * span - span / 4, edge + 1, cols - edge - 2);
            const int len = std::min(span + 3 + (int)(h % 3u), std::max(3, bottomRoom - 1));
            const int y = content.y1 + 2 + (int)(h % (unsigned int)std::max(1, std::min(3, bottomRoom - 2)));
            for (int k = 0; k < len; ++k) {
                const int x = x0 + k;
                if (x >= cols - edge - 1) break;
                SafeAtmosphereGlyph(grid, cols, rows, x, y,
                    (k % 4 == 0) ? GLYPH_CORNER : GLYPH_HLINE, content, 3);
            }
        }
    }
}

// Вертикальные "шпили/надгробия" в пустом боковом пространстве.
void DrawObelisks(std::vector<unsigned char>& grid, int cols, int rows,
                         const AtmosphereBounds& content, int seed,
                         int count, bool alternateSides) {
    const int edge = 2;
    for (int i = 0; i < count; ++i) {
        const unsigned int h = Hash(seed + i * 1451, seed + 121);
        const bool left = alternateSides ? ((i + (int)(h & 1u)) & 1) == 0 : ((h >> 3) & 1u) == 0;
        const int room = left ? content.x0 - edge - 4 : cols - edge - 4 - content.x1;
        if (room < 4) continue;

        const int x = left
            ? edge + 2 + (int)((h >> 9) % (unsigned int)std::max(1, room - 1))
            : content.x1 + 3 + (int)((h >> 9) % (unsigned int)std::max(1, room - 1));
        const int top = content.y0 + 1 + (int)((h >> 17) % 3u);
        int len = std::min(8, (rows - edge - 3) - top);
        len = std::max(2, len);
        if (top + len >= rows - edge - 1) continue;

        SafeAtmosphereGlyph(grid, cols, rows, x, top - 1, GLYPH_CIRCLE, content, 3);
        SafeAtmosphereGlyph(grid, cols, rows, x, top, GLYPH_CORNER, content, 3);
        for (int k = 1; k < len; ++k) {
            const unsigned char g = (k == len - 1) ? GLYPH_CORNER
                               : ((h + (unsigned int)k) % 6u == 0u ? GLYPH_DRIP_SMALL : GLYPH_VLINE);
            SafeAtmosphereGlyph(grid, cols, rows, x, top + k, g, content, 3);
        }
    }
}

// Ритуальные кресты без окружности: характерный знак можно получить только
// из +, | и =, поэтому здесь deliberately нет GLYPH_CIRCLE.
void DrawRitualCrosses(std::vector<unsigned char>& grid, int cols, int rows,
                              const AtmosphereBounds& content, int seed,
                              int count, bool onSides) {
    const int edge = 2;
    for (int i = 0; i < count; ++i) {
        const unsigned int h = Hash(seed + i * 1733, seed + 143);
        int x, y;
        if (onSides) {
            const bool left = ((h >> 2) & 1u) == 0u;
            const int room = left ? content.x0 - edge - 4 : cols - edge - 4 - content.x1;
            if (room < 3) continue;
            x = left ? edge + 2 + (int)((h >> 8) % (unsigned int)std::max(1, room - 1))
                     : content.x1 + 3 + (int)((h >> 8) % (unsigned int)std::max(1, room - 1));
            y = content.y0 + 2 + (int)((h >> 16) % (unsigned int)std::max(1, content.y1 - content.y0 - 2));
        } else {
            const int room = rows - edge - 2 - content.y1;
            if (room < 4) continue;
            x = content.x0 + 3 + (int)((h >> 8) % (unsigned int)std::max(1, content.x1 - content.x0 - 5));
            y = content.y1 + 2 + (int)((h >> 16) % (unsigned int)std::max(1, room - 2));
        }

        SafeAtmosphereGlyph(grid, cols, rows, x, y, GLYPH_CORNER, content, 3);
        SafeAtmosphereGlyph(grid, cols, rows, x - 1, y, GLYPH_HLINE, content, 3);
        SafeAtmosphereGlyph(grid, cols, rows, x + 1, y, GLYPH_HLINE, content, 3);
        SafeAtmosphereGlyph(grid, cols, rows, x, y - 1, GLYPH_VLINE, content, 3);
        SafeAtmosphereGlyph(grid, cols, rows, x, y + 1, GLYPH_VLINE, content, 3);
    }
}

// Рваная "занавесь" из потёков/цепей. В отличие от пилонов не привязана
// к одной вертикальной линии и создаёт плотный силуэт по краям кадра.
void DrawHangingCurtain(std::vector<unsigned char>& grid, int cols, int rows,
                               const AtmosphereBounds& content, int seed,
                               bool leftSide, bool rightSide) {
    const int edge = 2;
    auto side = [&](bool left, int offsetSeed) {
        const int room = left ? content.x0 - edge - 4 : cols - edge - 4 - content.x1;
        if (room < 3) return;
        const int baseX = left ? edge + 2 : content.x1 + 3;
        for (int i = 0; i < 7; ++i) {
            const unsigned int h = Hash(seed + offsetSeed + i * 421, seed + 177);
            const int x = baseX + (int)(h % (unsigned int)std::max(1, room - 1));
            const int len = 2 + (int)((h >> 8) % 7u);
            const int y0 = edge + 2 + (int)((h >> 16) % 3u);
            for (int k = 0; k < len; ++k) {
                const unsigned char g = (k == len - 1) ? GLYPH_DRIP_BIG
                    : ((h + (unsigned int)k) % 4u == 0u ? GLYPH_DRIP_SMALL : GLYPH_VLINE);
                SafeAtmosphereGlyph(grid, cols, rows, x, y0 + k, g, content, 3);
            }
        }
    };
    if (leftSide) side(true, 0);
    if (rightSide) side(false, 911);
}

// Разбросанный "рой" пепла/искр: больше свободных частиц, меньше архитектуры.
// Работает как отдельный рисунок и не влияет на пепельную виньетку.
void DrawEmberSwarm(std::vector<unsigned char>& grid, int cols, int rows,
                           const AtmosphereBounds& content, int seed,
                           int density, bool heavy) {
    const int edge = 2;
    const int maxAttempts = std::max(density * 4, density + 16);
    int placed = 0;
    for (int attempt = 0; attempt < maxAttempts && placed < density; ++attempt) {
        const unsigned int h = Hash(seed + attempt * 1871, seed + 203);
        const int x = edge + 1 + (int)(h % (unsigned int)std::max(1, cols - edge * 2 - 2));
        const int y = edge + 1 + (int)((h >> 9) % (unsigned int)std::max(1, rows - edge * 2 - 2));
        if (!AtmosphereFreeCell(x, y, cols, rows, content, 3)) continue;

        const unsigned int roll = (h >> 19) % (heavy ? 5u : 7u);
        const unsigned char g = (roll == 0u) ? GLYPH_CIRCLE
                              : (roll <= 2u ? GLYPH_DRIP_SMALL : GLYPH_FLOOR);
        PutGlyph(grid, cols, rows, x, y, g);
        ++placed;
    }
}

// Многоярусные "шрамы" — горизонтальные разломы в верхнем/нижнем поле.
// Выглядят совершенно иначе, чем обычные диагональные DrawCrack().
void DrawHorizontalScars(std::vector<unsigned char>& grid, int cols, int rows,
                                const AtmosphereBounds& content, int seed,
                                int count, bool bottomOnly) {
    const int edge = 2;
    auto drawBand = [&](int y, int localSeed) {
        if (y < edge + 1 || y > rows - edge - 2) return;
        int x0 = edge + 3;
        int x1 = cols - edge - 4;
        const int segmentCount = std::clamp(count, 1, 6);
        for (int seg = 0; seg < segmentCount; ++seg) {
            const unsigned int h = Hash(localSeed + seg * 347, seed + 251);
            const int span = std::max(4, (x1 - x0) / segmentCount);
            const int sx = x0 + seg * span + (int)(h % 3u);
            const int len = std::max(3, span - 2 + (int)(h % 4u));
            for (int k = 0; k < len; ++k) {
                const unsigned char g = ((h + (unsigned int)k) % 7u == 0u) ? GLYPH_DRIP_SMALL : GLYPH_HLINE;
                SafeAtmosphereGlyph(grid, cols, rows, sx + k, y, g, content, 3);
            }
        }
    };

    if (!bottomOnly) {
        const int topRoom = content.y0 - edge - 3;
        if (topRoom >= 2) {
            drawBand(edge + 1 + topRoom / 2, seed + 1);
            if (topRoom >= 6) drawBand(edge + 1, seed + 2);
        }
    }

    const int bottomRoom = rows - edge - 2 - content.y1;
    if (bottomRoom >= 2) {
        drawBand(content.y1 + 1 + bottomRoom / 2, seed + 3);
    }
}


// ---------------------------------------------------------------------------
// Общий слой БЕЗНАДЁЖНОСТИ.
//
// Это не одна и та же декорация во всех рецептах: каждый вариант получает
// собственную "угрозу" поверх своего базового мотива. Идея — не сделать
// меню просто "красивым dark fantasy", а создать ощущение места, откуда
// уже нечему возвращаться: кровь, наблюдающие пустые глаза, пепельный дождь,
// сломанные ритуальные знаки и редкие следы чьего-то присутствия.
// Все клетки по-прежнему проходят AtmosphereFreeCell(), поэтому слой не
// способен залезть на реальный content-bbox меню.
// ---------------------------------------------------------------------------
void DrawDeadEyes(std::vector<unsigned char>& grid, int cols, int rows,
                         const AtmosphereBounds& content, int seed, int count) {
    const int edge = 2;
    const int leftRoom = content.x0 - edge - 7;
    const int rightRoom = cols - edge - 4 - content.x1;
    if (leftRoom < 4 && rightRoom < 4) return;

    for (int i = 0; i < count; ++i) {
        const unsigned int h = Hash(seed + i * 1433, seed + 919);
        bool left = ((h >> 2) & 1u) == 0u;
        if (left && leftRoom < 4) left = false;
        if (!left && rightRoom < 4) left = true;
        const int room = left ? leftRoom : rightRoom;
        const int y = edge + 5 + (int)((h >> 9) % (unsigned int)std::max(1, rows - edge * 2 - 10));
        const int x = left
            ? edge + 3 + (int)((h >> 17) % (unsigned int)std::max(1, room - 2))
            : content.x1 + 4 + (int)((h >> 17) % (unsigned int)std::max(1, room - 2));

        SafeAtmosphereGlyph(grid, cols, rows, x, y, GLYPH_CIRCLE, content, 3);
        SafeAtmosphereGlyph(grid, cols, rows, x + (left ? 2 : -2), y, GLYPH_CIRCLE, content, 3);
        // Редкая "слеза" под глазами — чтобы они не выглядели декоративным
        // узором, а напоминали наблюдающее, мёртвое существо.
        if ((h & 7u) == 0u) {
            SafeAtmosphereGlyph(grid, cols, rows, x + (left ? 1 : -1), y + 1,
                                GLYPH_DRIP_SMALL, content, 3);
        }
    }
}

void DrawBloodTrail(std::vector<unsigned char>& grid, int cols, int rows,
                           const AtmosphereBounds& content, int seed, int branches) {
    const int edge = 2;
    const int topRoom = content.y0 - edge - 4;
    if (topRoom < 3) return;

    for (int i = 0; i < branches; ++i) {
        const unsigned int h = Hash(seed + i * 2711, seed + 1207);
        const bool fromLeft = ((h >> 3) & 1u) == 0u;
        int x = fromLeft
            ? edge + 2 + (int)((h >> 9) % (unsigned int)std::max(1, cols / 4))
            : cols - edge - 3 - (int)((h >> 9) % (unsigned int)std::max(1, cols / 4));
        int y = edge + 1 + (int)((h >> 18) % (unsigned int)std::max(1, topRoom - 1));
        const int len = std::min(6, std::max(3, topRoom - y));
        const int dir = fromLeft ? 1 : -1;
        for (int k = 0; k < len; ++k) {
            const unsigned int s = Hash(seed + i * 31 + k * 149, 7331);
            SafeAtmosphereGlyph(grid, cols, rows, x, y,
                (k == len - 1 || s % 5u == 0u) ? GLYPH_DRIP_BIG : GLYPH_DRIP_SMALL,
                content, 3);
            if (k > 0 && (k % 3 == 0)) x += dir;
            ++y;
        }
        SafeAtmosphereGlyph(grid, cols, rows, x, y, GLYPH_CIRCLE, content, 3);
    }
}

void DrawAshfall(std::vector<unsigned char>& grid, int cols, int rows,
                        const AtmosphereBounds& content, int seed, int density) {
    const int edge = 2;
    const int maxAttempts = std::max(density * 5, density + 24);
    int placed = 0;
    for (int a = 0; a < maxAttempts && placed < density; ++a) {
        const unsigned int h = Hash(seed + a * 3469, seed + 1889);
        const bool top = ((h >> 4) & 1u) == 0u;
        const int x = edge + 1 + (int)((h >> 8) % (unsigned int)std::max(1, cols - edge * 2 - 2));
        const int span = top ? std::max(1, content.y0 - edge - 4)
                             : std::max(1, rows - edge - 3 - content.y1);
        const int y = top
            ? edge + 2 + (int)((h >> 19) % (unsigned int)span)
            : content.y1 + 2 + (int)((h >> 19) % (unsigned int)span);
        if (!AtmosphereFreeCell(x, y, cols, rows, content, 3)) continue;
        const unsigned int r = (h >> 25) % 6u;
        SafeAtmosphereGlyph(grid, cols, rows, x, y,
                            r == 0u ? GLYPH_DRIP_SMALL : GLYPH_FLOOR,
                            content, 3);
        ++placed;
    }
}

void DrawDespairLayer(std::vector<unsigned char>& grid, int cols, int rows,
                             const AtmosphereBounds& content, int seed,
                             int variantIndex, int density) {
    const int mode = ((variantIndex % 8) + 8) % 8;
    const int d = std::clamp(density, 2, 12);

    switch (mode) {
        case 0: // Кровь сверху + пустые глаза.
            DrawBloodTrail(grid, cols, rows, content, seed + 8010, 2 + d / 5);
            DrawDeadEyes(grid, cols, rows, content, seed + 8020, 1 + d / 6);
            break;
        case 1: // Пепельный дождь + цепь/одиночный крест.
            DrawAshfall(grid, cols, rows, content, seed + 8110, 8 + d * 2);
            DrawHangingChains(grid, cols, rows, content, seed + 8120, 1 + d / 5, false);
            break;
        case 2: // Два наблюдателя и рваные знаки.
            DrawDeadEyes(grid, cols, rows, content, seed + 8210, 2 + d / 6);
            DrawRitualCrosses(grid, cols, rows, content, seed + 8220, 1 + d / 6, true);
            break;
        case 3: // След распоротой стены.
            DrawHorizontalScars(grid, cols, rows, content, seed + 8310, 2 + d / 4, false);
            DrawBloodDrips(grid, cols, rows, content, seed + 8320, true, false, 2 + d / 5);
            break;
        case 4: // Поле могил/обелисков и пепел.
            DrawObelisks(grid, cols, rows, content, seed + 8410, 1 + d / 4, true);
            DrawAshfall(grid, cols, rows, content, seed + 8420, 5 + d);
            break;
        case 5: // Повешенные цепи и кровь снизу.
            DrawHangingChains(grid, cols, rows, content, seed + 8510, 2 + d / 5, true);
            DrawBloodDrips(grid, cols, rows, content, seed + 8520, false, true, 2 + d / 5);
            break;
        case 6: // Ритуал, который уже никто не завершит.
            DrawRitualCrosses(grid, cols, rows, content, seed + 8610, 2 + d / 4, false);
            DrawAshfall(grid, cols, rows, content, seed + 8620, 7 + d);
            break;
        case 7: // Почти пустой кадр с редкими следами присутствия.
            DrawDeadEyes(grid, cols, rows, content, seed + 8710, 1);
            DrawBloodTrail(grid, cols, rows, content, seed + 8720, 1);
            DrawAshfall(grid, cols, rows, content, seed + 8730, 3 + d / 2);
            break;
    }
}

// Рецепт композиции — ЧТО рисовать и с какими параметрами. Все элементы
// по-прежнему проходят через ту же guard-логику (hasSideRoom/topGap/
// bottomGap/расстояние до content), рецепт лишь решает, что ПЫТАТЬСЯ
// нарисовать, а не отменяет проверки безопасности.
AtmosphereVariant GetAtmosphereVariant(int index) {
    static const AtmosphereVariant table[kAtmosphereVariantCount] = {
        /*00 Икона*/ { true, true, true, false, true, false, 0, 0, 0.90f, true, true, false, false, 1.00f, 0, 0, false },
        /*01 Окровавленный камень*/ { true, false, false, false, true, false, 5, 6, 1.00f, false, false, false, false, 1.00f, 1, 7, false },
        /*02 Цепная капелла*/ { false, false, false, false, false, false, 3, 6, 1.00f, false, true, false, true, 0.90f, 2, 4, true },
        /*03 Костяная арка*/ { false, false, false, false, false, false, 3, 6, 1.00f, false, false, false, true, 1.00f, 3, 5, false },
        /*04 Пожарный алтарь*/ { true, true, true, true, true, true, 1, 6, 1.00f, false, true, true, false, 1.00f, 4, 3, false },
        /*05 Разорванная завеса*/ { false, false, false, false, false, false, 4, 6, 1.00f, false, false, false, false, 1.35f, 5, 1, true },
        /*06 Пустая часовня*/ { false, false, false, false, false, false, 2, 6, 1.00f, false, false, false, false, 1.00f, 6, 4, true },
        /*07 Кладбищенская линия*/ { false, true, false, false, true, false, 3, 6, 1.00f, false, false, false, false, 1.00f, 7, 5, false },
        /*08 Боковая печать*/ { true, false, true, false, true, false, 5, 3, 0.65f, true, true, false, false, 1.00f, 8, 5, false },
        /*09 Искрящийся прах*/ { false, false, false, false, false, false, 3, 6, 1.00f, false, false, false, false, 1.55f, 9, 95, false },
        /*10 Плиты-подземелье*/ { false, false, false, false, false, false, 0, 6, 1.00f, false, false, false, true, 1.00f, 10, 3, false },
        /*11 Палач*/ { true, false, true, false, true, false, 5, 6, 1.00f, false, true, false, false, 1.00f, 11, 8, false },
        /*12 Кованые врата*/ { true, true, true, true, false, false, 3, 6, 1.00f, false, false, false, false, 1.00f, 12, 1, false },
        /*13 Шрам на камне*/ { false, false, false, false, false, false, 3, 6, 1.00f, false, false, false, false, 1.25f, 13, 5, false },
        /*14 Праховые канделябры*/ { false, false, false, false, false, false, 4, 6, 1.00f, false, false, false, false, 1.00f, 14, 3, true },
        /*15 Чумной коридор*/ { false, false, false, false, false, false, 3, 6, 1.00f, false, false, false, false, 0.75f, 15, 1, true },
        /*16 Разломанная часовня*/ { false, false, false, false, false, false, 1, 6, 1.00f, false, true, false, true, 1.75f, 9, 55, false },
        /*17 Нижняя печать*/ { true, true, false, false, true, false, 3, 5, 0.72f, false, false, false, false, 1.00f, 17, 3, false },
        /*18 Нижние рёбра*/ { false, false, false, false, false, false, 5, 6, 1.00f, false, false, false, false, 1.00f, 18, 6, false },
        /*19 Стальные когти*/ { true, false, false, false, true, false, 4, 6, 1.00f, false, true, false, false, 1.00f, 19, 3, true },
        /*20 Пепельный шквал*/ { false, false, false, false, false, false, 2, 6, 1.00f, false, false, false, false, 2.20f, 9, 150, false },
        /*21 Тёмный алтарь*/ { true, true, true, false, false, true, 4, 6, 1.00f, false, true, false, false, 1.00f, 7, 7, true },
        /*22 Капающий свод*/ { false, false, false, false, false, false, 3, 6, 1.00f, false, false, false, false, 1.00f, 22, 10, false },
        /*23 Сломанные ворота*/ { true, true, true, true, false, false, 5, 6, 1.00f, false, false, false, false, 1.00f, 12, 3, false },
        /*24 Костяное поле*/ { false, false, false, false, false, false, 3, 6, 1.00f, false, false, false, false, 1.60f, 24, 7, false },
        /*25 Затхлая шахта*/ { false, false, false, false, false, false, 4, 6, 1.00f, false, true, false, false, 1.00f, 2, 6, true },
        /*26 Разорванный периметр*/ { false, true, false, false, false, false, 3, 6, 1.00f, false, false, false, false, 1.00f, 26, 4, true },
        /*27 Пепельные занавесы*/ { false, false, false, false, false, false, 0, 6, 1.00f, false, false, false, false, 1.90f, 15, 1, true },
        /*28 Кровавый перекрёсток*/ { false, false, false, false, false, false, 1, 6, 1.00f, false, false, false, false, 1.00f, 28, 7, false },
        /*29 Последний дозор*/ { false, true, true, false, true, false, 3, 6, 1.00f, false, false, false, false, 1.00f, 29, 1, false },
        /*30 Осаждённая крепь*/ { true, true, false, false, true, false, 3, 6, 1.00f, false, true, false, false, 0.70f, 13, 9, false },
        /*31 Погасшее святилище*/ { false, false, false, false, false, false, 3, 6, 1.00f, false, false, false, false, 1.40f, 31, 5, false },
    };
    index = ((index % kAtmosphereVariantCount) + kAtmosphereVariantCount) % kAtmosphereVariantCount;
    return table[index];
}

// Псевдослучайный (но детерминированный по паре чисел) выбор индекса
// варианта — вызывается ОДИН РАЗ в момент входа в MENU/PAUSED (см.
// main.cpp: menuOpenCount/pauseOpenCount), а НЕ каждый кадр/hover —
// иначе композиция "плавала" бы внутри одного открытия меню. seed —
// menuSeed (фиксирован на сессию), entryCounter — счётчик входов в
// состояние, инкрементируемый в main.cpp при каждом переходе в
// MENU/PAUSED. Использует тот же Hash(), что и остальной dark-fantasy
// узор, просто с другими множителями, чтобы не коррелировать с ним.
int PickAtmosphereVariant(int entryCounter, int seed) {
    const unsigned int h = Hash(entryCounter * 104729 + 7, seed + 31337);
    return (int)(h % (unsigned int)kAtmosphereVariantCount);
}

// То же псевдослучайное распределение, но с гарантией, что при повторном
// открытии одного и того же меню новый вариант не совпадёт с предыдущим.
// Это особенно важно для PAUSED: если hash случайно выдаст тот же индекс,
// игрок визуально решит, что декорации паузы больше не меняются.
int PickNextAtmosphereVariant(int previousVariant, int entryCounter, int seed) {
    int next = PickAtmosphereVariant(entryCounter, seed);
    if (kAtmosphereVariantCount <= 1) return 0;

    previousVariant = ((previousVariant % kAtmosphereVariantCount) +
                       kAtmosphereVariantCount) % kAtmosphereVariantCount;

    if (next == previousVariant) {
        next = (next + 1 + (entryCounter & 3)) % kAtmosphereVariantCount;
        if (next == previousVariant)
            next = (next + 1) % kAtmosphereVariantCount;
    }
    return next;
}

void DrawDarkFantasyAtmosphere(std::vector<unsigned char>& grid, int cols, int rows,
                                        int seed, bool pauseMenu, const AtmosphereBounds& content,
                                        int variantIndex) {
    if (cols <= 8 || rows <= 8) return;

    const AtmosphereVariant v = GetAtmosphereVariant(variantIndex);

    const int edge = 2;

    // Внешняя рамка: та же "истёртая линия", что и у кнопок (DrawBox),
    // а не свой отдельный узор — визуально одна школа на весь экран.
    //
    // На очень тесных по вертикали разрешениях (напр. 1280x720 с крупным
    // заголовком) content-зона может начинаться почти у самого верха
    // экрана — тогда fixed-row внешняя рамка (row=edge) горизонтально
    // пересекается с ОТСТУПОМ рамки заголовка (зона между её собственной
    // рамкой DrawBox и текстом CELL, которую НИЧЕМ не перекрывают позже —
    // ни border заголовка, ни сами буквы). contentPadFrame — тот же
    // отступ, что и у пепельной виньетки ниже: если строка/столбец рамки
    // попадает в этот запас — сегмент, пересекающийся с content по
    // горизонтали/вертикали, просто не рисуется (рамка "разрывается"
    // ровно на ширину content-зоны), а не наезжает на неё.
    const int contentPadFrame = 2;
    const bool topEdgeNearContent    = edge >= content.y0 - contentPadFrame && edge <= content.y1 + contentPadFrame;
    const bool bottomEdgeNearContent = (rows - edge - 1) >= content.y0 - contentPadFrame &&
                                        (rows - edge - 1) <= content.y1 + contentPadFrame;
    const bool leftEdgeNearContent   = edge >= content.x0 - contentPadFrame && edge <= content.x1 + contentPadFrame;
    const bool rightEdgeNearContent  = (cols - edge - 1) >= content.x0 - contentPadFrame &&
                                        (cols - edge - 1) <= content.x1 + contentPadFrame;

    auto drawHEdgeAvoidingContent = [&](int y, int seedOffset) {
        const bool nearContent = (y == edge) ? topEdgeNearContent : bottomEdgeNearContent;
        if (!nearContent) {
            DrawWornEdgeH(grid, cols, rows, y, edge, cols - edge - 1, seed + seedOffset, true);
            return;
        }
        const int leftEnd = content.x0 - contentPadFrame - 1;
        const int rightStart = content.x1 + contentPadFrame + 1;
        if (leftEnd >= edge) DrawWornEdgeH(grid, cols, rows, y, edge, leftEnd, seed + seedOffset, true);
        if (rightStart <= cols - edge - 1) DrawWornEdgeH(grid, cols, rows, y, rightStart, cols - edge - 1, seed + seedOffset, true);
    };
    auto drawVEdgeAvoidingContent = [&](int x, int seedOffset) {
        const bool nearContent = (x == edge) ? leftEdgeNearContent : rightEdgeNearContent;
        if (!nearContent) {
            DrawWornEdgeV(grid, cols, rows, x, edge + 1, rows - edge - 2, seed + seedOffset, true);
            return;
        }
        const int topEnd = content.y0 - contentPadFrame - 1;
        const int bottomStart = content.y1 + contentPadFrame + 1;
        if (topEnd >= edge + 1) DrawWornEdgeV(grid, cols, rows, x, edge + 1, topEnd, seed + seedOffset, true);
        if (bottomStart <= rows - edge - 2) DrawWornEdgeV(grid, cols, rows, x, bottomStart, rows - edge - 2, seed + seedOffset, true);
    };

    drawHEdgeAvoidingContent(edge, 41);
    drawHEdgeAvoidingContent(rows - edge - 1, 57);
    drawVEdgeAvoidingContent(edge, 73);
    drawVEdgeAvoidingContent(cols - edge - 1, 89);

    // Углы рамки — с шансом пропуска (обвалившийся угол), а не все четыре
    // гарантированно целые. Асимметрия важнее аккуратности. Если у
    // варианта включены cornerTicks — добавляем мелкие штрихи внутрь
    // экрана у каждого угла (см. DrawCornerTick), независимо от того,
    // "выжил" сам угловой глиф или нет.
    auto cornerOrGap = [&](int x, int y, int cornerSeed, int dx, int dy) {
        if (Hash(cornerSeed, seed) % 5u != 0u) {
            PutGlyph(grid, cols, rows, x, y, GLYPH_CORNER);
        }
        if (v.cornerTicks) {
            DrawCornerTick(grid, cols, rows, x, y, dx, dy, seed + cornerSeed + 500);
        }
    };
    cornerOrGap(edge, edge, 101, 1, 1);
    cornerOrGap(cols - edge - 1, edge, 102, -1, 1);
    cornerOrGap(edge, rows - edge - 1, 103, 1, -1);
    cornerOrGap(cols - edge - 1, rows - edge - 1, 104, -1, -1);

    // Пепел виньеткой: гуще у краёв кадра, почти исчезает к центру.
    // Дополнительно НЕ рисуется поверх content-зоны (даже с отступом) —
    // не потому что это визуально сломало бы что-то (текст/кнопки всё
    // равно рисуются позже поверх), а чтобы не тратить проходы на клетки,
    // которые гарантированно будут перезаписаны. ashDensityMul варианта
    // уменьшает делитель (== гуще пепел), а не сам порог напрямую.
    const int contentPad = 2;
    for (int y = edge + 1; y <= rows - edge - 2; ++y) {
        if (y >= content.y0 - contentPad && y <= content.y1 + contentPad) continue;
        for (int x = edge + 1; x <= cols - edge - 2; ++x) {
            const int distX = std::min(x - edge, cols - edge - 1 - x);
            const int distY = std::min(y - edge, rows - edge - 1 - y);
            const int distToEdge = std::min(distX, distY);
            const int density = std::max(6, (int)((20 + distToEdge * 8) / std::max(0.1f, v.ashDensityMul)));
            if (Hash(x * 733 + y, seed + 900) % (unsigned int)density == 0u) {
                PutGlyph(grid, cols, rows, x, y, GLYPH_FLOOR);
            }
        }
    }

    // Диагональные трещины — режим зависит от варианта (v.crackMode), но
    // длина КАЖДОЙ трещины из ЛЮБОГО угла по-прежнему считается от
    // реального расстояния до content-зоны с того же угла (тот же принцип,
    // что был раньше только для TL/BR) — гарантированно не доходит до
    // текста/кнопок ни в одном режиме.
    const int distToContentTL = std::max(2, std::min(content.x0 - edge, content.y0 - edge) - 3);
    const int distToContentTR = std::max(2, std::min((cols - edge - 1) - content.x1, content.y0 - edge) - 3);
    const int distToContentBL = std::max(2, std::min(content.x0 - edge, (rows - edge - 1) - content.y1) - 3);
    const int distToContentBR = std::max(2, std::min((cols - edge - 1) - content.x1,
                                                       (rows - edge - 1) - content.y1) - 3);
    const int shortMax = std::min(cols, rows) / 3;
    const int longMax  = std::min(cols, rows) / 2;

    switch (v.crackMode) {
        case 0: // классика: из TL и BR
            DrawCrack(grid, cols, rows, edge + 2, edge + 2, 1, 1,
                       std::min(shortMax, distToContentTL), seed + 1201);
            DrawCrack(grid, cols, rows, cols - edge - 3, rows - edge - 3, -1, -1,
                       std::min(shortMax, distToContentBR), seed + 1213);
            break;
        case 1: // все четыре угла, каждая покороче
            DrawCrack(grid, cols, rows, edge + 2, edge + 2, 1, 1,
                       std::min(shortMax * 2 / 3, distToContentTL), seed + 1201);
            DrawCrack(grid, cols, rows, cols - edge - 3, edge + 2, -1, 1,
                       std::min(shortMax * 2 / 3, distToContentTR), seed + 1207);
            DrawCrack(grid, cols, rows, edge + 2, rows - edge - 3, 1, -1,
                       std::min(shortMax * 2 / 3, distToContentBL), seed + 1219);
            DrawCrack(grid, cols, rows, cols - edge - 3, rows - edge - 3, -1, -1,
                       std::min(shortMax * 2 / 3, distToContentBR), seed + 1213);
            break;
        case 2: { // одна длинная трещина из случайного угла
            const unsigned int pick = Hash(seed, 4242) % 4u;
            if (pick == 0u)
                DrawCrack(grid, cols, rows, edge + 2, edge + 2, 1, 1,
                           std::min(longMax, distToContentTL), seed + 1201);
            else if (pick == 1u)
                DrawCrack(grid, cols, rows, cols - edge - 3, edge + 2, -1, 1,
                           std::min(longMax, distToContentTR), seed + 1207);
            else if (pick == 2u)
                DrawCrack(grid, cols, rows, edge + 2, rows - edge - 3, 1, -1,
                           std::min(longMax, distToContentBL), seed + 1219);
            else
                DrawCrack(grid, cols, rows, cols - edge - 3, rows - edge - 3, -1, -1,
                           std::min(longMax, distToContentBR), seed + 1213);
            break;
        }
        case 3: // без трещин — сдержанный вариант
            break;
        case 4: // только из TL
            DrawCrack(grid, cols, rows, edge + 2, edge + 2, 1, 1,
                       std::min(shortMax, distToContentTL), seed + 1201);
            break;
        case 5: // только из BR
            DrawCrack(grid, cols, rows, cols - edge - 3, rows - edge - 3, -1, -1,
                       std::min(shortMax, distToContentBR), seed + 1213);
            break;
    }

    // ---- Боковые пилоны/стойки — СНАРУЖИ content-зоны с гарантированным
    // зазором, а не по угаданным midX/midY координатам. Если места сбоку
    // не хватает (узкое окно) — стойки просто не рисуются. У каждой
    // стороны своя проверка места (hasLeftRoom/hasRightRoom), поэтому
    // асимметричные варианты (только один пилон) остаются безопасными и
    // на узких экранах, где раньше пропадали бы оба разом.
    const int sideGap = 3;
    const int wingLeft = content.x0 - sideGap;
    const int wingRight = content.x1 + sideGap;
    const bool hasLeftRoom = wingLeft > edge + 2;
    const bool hasRightRoom = wingRight < cols - edge - 3;
    const bool hasSideRoom = hasLeftRoom && hasRightRoom;

    const int wingTop = content.y0;
    const int wingBottomL = content.y0 + (int)(0.7f * (content.y1 - content.y0));
    const int wingBottomR = content.y1;

    if (v.pylonLeft && hasLeftRoom) {
        if (v.pylonLine) {
            DrawWornEdgeV(grid, cols, rows, wingLeft, wingTop, wingBottomL, seed + 301, false);
        }
        if (v.torchAtPylonTop)    DrawTorch(grid, cols, rows, wingLeft, wingTop - 1, seed + 11);
        if (v.torchAtPylonBottom) DrawTorch(grid, cols, rows, wingLeft, wingBottomL + 1, seed + 21);
        if (v.pylonCrossbar)      DrawPylonCrossbar(grid, cols, rows, wingLeft, wingTop, edge, seed + 331);
    }
    if (v.pylonRight && hasRightRoom) {
        if (v.pylonLine) {
            DrawWornEdgeV(grid, cols, rows, wingRight, wingTop + 2, wingBottomR, seed + 317, false);
        }
        if (v.torchAtPylonTop)    DrawTorch(grid, cols, rows, wingRight, wingTop + 1, seed + 13);
        if (v.torchAtPylonBottom) DrawTorch(grid, cols, rows, wingRight, wingBottomR + 1, seed + 23);
        if (v.pylonCrossbar)      DrawPylonCrossbar(grid, cols, rows, wingRight, wingTop + 2, cols - edge - 1, seed + 347);
    }

    // ---- Руна-сигила. Кольцо теперь намеренно редкое: его используют только
    // варианты 0, 8 и 17. Все остальные рецепты имеют runeMode=6 и получают
    // уникальный небуквенный мотив вместо очередного кольца.
    // Экран игры широкий (16:9), а не высокий — на
    // реальном разрешении (напр. 1280x720) заголовок CELL часто занимает
    // почти весь запас по вертикали, и topGapAvailable оказывается близко
    // к нулю, а вот по бокам от content-зоны обычно остаётся много места
    // (десятки колонок). Поэтому базовый режим сначала пробует разместить
    // руну сверху, и ТОЛЬКО если там тесно — переносит в свободное боковое
    // поле (снаружи от пилона), по вертикали выровняв на середину
    // content-зоны. bottomGapAvailable вычисляется здесь же (раньше, чем
    // в оригинале), т.к. он нужен и режиму runeMode=5, и разделу
    // "основание" ниже — переиспользуем одно и то же значение.
    const int topGapAvailable = content.y0 - (edge + 1);
    const int bottomGapAvailable = (rows - edge - 2) - content.y1;

    // DrawRuneCircle рисует не только сам круг радиуса r, но и лучи-засечки
    // ещё на 2 клетки дальше (см. DrawRuneCircle: "радиус+2"). Поэтому
    // безопасный радиус считается от ПОЛОВИНЫ доступного пространства
    // (руна стоит по центру доступной полосы) с запасом в 1 клетку сверх
    // луча — иначе луч мог провалиться прямо в content-зону (ровно так
    // раньше и происходило на 1920x1080: радиус брался от topGapAvailable
    // без учёта +2 на луч). Возвращает 0, если руна совсем не помещается.
    auto safeRuneRadius = [](int availableSpan, float mul) -> int {
        const int half = availableSpan / 2;
        const int maxRadius = half - 3; // -2 на луч, -1 клетка запаса
        if (maxRadius < 2) return 0;
        return std::clamp((int)std::lround(maxRadius * mul), 2, maxRadius);
    };

    auto tryTopRune = [&](int biasX) -> bool {
        const int runeRadius = safeRuneRadius(topGapAvailable, v.runeRadiusMul);
        if (runeRadius <= 0) return false;
        const int runeCy = edge + 1 + topGapAvailable / 2;
        const int runeCx = (content.x0 + content.x1) / 2 + biasX;
        DrawRuneCircle(grid, cols, rows, runeCx, runeCy, runeRadius, seed + (pauseMenu ? 999 : 555));
        return true;
    };
    auto trySideRune = [&]() -> bool {
        if (!hasSideRoom) return false;
        const int leftRoom = wingLeft - (edge + 2);
        const int rightRoom = (cols - edge - 3) - wingRight;
        const bool useLeft = leftRoom >= rightRoom;
        const int room = useLeft ? leftRoom : rightRoom;
        const int runeRadius = safeRuneRadius(room, v.runeRadiusMul);
        if (runeRadius <= 0) return false;
        const int runeCx = useLeft ? (edge + 2 + room / 2) : (cols - edge - 3 - room / 2);
        const int runeCy = (content.y0 + content.y1) / 2;
        DrawRuneCircle(grid, cols, rows, runeCx, runeCy, runeRadius,
                        seed + (pauseMenu ? 999 : 555) + (useLeft ? 0 : 1));
        return true;
    };
    auto tryBottomRune = [&]() -> bool {
        const int runeRadius = safeRuneRadius(bottomGapAvailable, v.runeRadiusMul);
        if (runeRadius <= 0) return false;
        const int runeCy = content.y1 + 1 + bottomGapAvailable / 2;
        const int runeCx = (content.x0 + content.x1) / 2;
        DrawRuneCircle(grid, cols, rows, runeCx, runeCy, runeRadius, seed + (pauseMenu ? 999 : 555) + 2);
        return true;
    };

    bool runePlaced = false;
    switch (v.runeMode) {
        case 0: // сверху-центр (тот же лёгкий сдвиг для главного меню, что и раньше), фолбэк вбок
            runePlaced = tryTopRune(pauseMenu ? 0 : 6);
            if (!runePlaced) runePlaced = trySideRune();
            break;
        case 1: // сверху-влево
            runePlaced = tryTopRune(-std::max(4, (content.x1 - content.x0) / 6));
            if (!runePlaced) runePlaced = trySideRune();
            break;
        case 2: // сверху-вправо
            runePlaced = tryTopRune(std::max(4, (content.x1 - content.x0) / 6));
            if (!runePlaced) runePlaced = trySideRune();
            break;
        case 3: // принудительно сбоку (без попытки сверху)
            runePlaced = trySideRune();
            break;
        case 4: { // две малые руны по бокам верхней зоны
            const int runeRadius = safeRuneRadius(topGapAvailable, v.runeRadiusMul);
            if (runeRadius > 0) {
                const int spacing = std::max(runeRadius * 2 + 3, (content.x1 - content.x0) / 5);
                const int runeCy = edge + 1 + topGapAvailable / 2;
                const int runeCx = (content.x0 + content.x1) / 2;
                DrawTwinRunes(grid, cols, rows, runeCx, runeCy, spacing, runeRadius,
                               seed + (pauseMenu ? 999 : 555));
                runePlaced = true;
            }
            if (!runePlaced) runePlaced = trySideRune();
            break;
        }
        case 5: // снизу (если есть место под content-зоной), иначе как режим 0
            runePlaced = tryBottomRune();
            if (!runePlaced) runePlaced = tryTopRune(pauseMenu ? 0 : 6);
            if (!runePlaced) runePlaced = trySideRune();
            break;
        case 6: // без руны
            break;
    }

    // ---- Уникальный мотив текущего рецепта ----------------------------
    // Эти мотивы не создают кольцевых сигил. Кольцо остаётся только в
    // вариантах 0, 8 и 17 — всего три рецепта на весь пул.
    switch (v.motifMode) {
        case 1:
            DrawBloodDrips(grid, cols, rows, content, seed + 5000, true, true,
                           std::max(3, v.motifDensity));
            break;
        case 2:
            DrawHangingChains(grid, cols, rows, content, seed + 5100,
                              std::max(2, v.motifDensity), v.motifBothSides);
            break;
        case 3:
            DrawBrokenRibs(grid, cols, rows, content, seed + 5200,
                           std::max(3, v.motifDensity), true);
            break;
        case 4:
            DrawBrokenRibs(grid, cols, rows, content, seed + 5300,
                           std::max(2, v.motifDensity), false);
            DrawRitualCrosses(grid, cols, rows, content, seed + 5310, 2, true);
            break;
        case 5:
            DrawHangingCurtain(grid, cols, rows, content, seed + 5400, true, v.motifBothSides);
            break;
        case 6:
            DrawObelisks(grid, cols, rows, content, seed + 5500,
                         std::max(2, v.motifDensity), v.motifBothSides);
            break;
        case 7:
            DrawRitualCrosses(grid, cols, rows, content, seed + 5600,
                              std::max(3, v.motifDensity), v.motifBothSides);
            break;
        case 8:
            DrawRitualCrosses(grid, cols, rows, content, seed + 5700, 3, true);
            DrawEmberSwarm(grid, cols, rows, content, seed + 5710,
                           std::max(25, v.motifDensity * 10), false);
            break;
        case 9:
            DrawEmberSwarm(grid, cols, rows, content, seed + 5800,
                           std::max(40, v.motifDensity), v.motifDensity > 110);
            break;
        case 10:
            DrawHorizontalScars(grid, cols, rows, content, seed + 5900,
                                std::max(2, v.motifDensity), false);
            break;
        case 11:
            DrawBloodDrips(grid, cols, rows, content, seed + 6000, true, false,
                           std::max(4, v.motifDensity));
            DrawRitualCrosses(grid, cols, rows, content, seed + 6010, 2, true);
            break;
        case 12:
            if (content.y0 - (edge + 2) >= 3) {
                const int y = edge + 2;
                const int leftEnd = content.x0 - 4;
                const int rightStart = content.x1 + 4;
                if (leftEnd >= edge + 4)
                    DrawWornEdgeH(grid, cols, rows, y, edge + 4, leftEnd, seed + 6100, true);
                if (rightStart <= cols - edge - 5)
                    DrawWornEdgeH(grid, cols, rows, y, rightStart, cols - edge - 5, seed + 6110, true);
            }
            DrawBrokenRibs(grid, cols, rows, content, seed + 6120,
                           std::max(2, v.motifDensity), true);
            break;
        case 13:
            DrawHorizontalScars(grid, cols, rows, content, seed + 6200,
                                std::max(3, v.motifDensity), false);
            break;
        case 14:
            DrawHangingChains(grid, cols, rows, content, seed + 6300,
                              std::max(2, v.motifDensity), v.motifBothSides);
            DrawEmberSwarm(grid, cols, rows, content, seed + 6310, 26, false);
            break;
        case 15:
            DrawHangingCurtain(grid, cols, rows, content, seed + 6400,
                               true, v.motifBothSides);
            break;
        case 17:
            DrawRitualCrosses(grid, cols, rows, content, seed + 6500, 2, false);
            break;
        case 18:
            DrawBrokenRibs(grid, cols, rows, content, seed + 6600,
                           std::max(3, v.motifDensity), false);
            break;
        case 19:
            DrawObelisks(grid, cols, rows, content, seed + 6700,
                         std::max(2, v.motifDensity), true);
            DrawHangingChains(grid, cols, rows, content, seed + 6710, 2, true);
            break;
        case 22:
            DrawBloodDrips(grid, cols, rows, content, seed + 6800,
                           true, false, std::max(5, v.motifDensity));
            DrawBrokenRibs(grid, cols, rows, content, seed + 6810, 4, true);
            break;
        case 24:
            DrawObelisks(grid, cols, rows, content, seed + 6900,
                         std::max(4, v.motifDensity), false);
            DrawEmberSwarm(grid, cols, rows, content, seed + 6910, 36, false);
            break;
        case 26:
            DrawObelisks(grid, cols, rows, content, seed + 7000,
                         std::max(2, v.motifDensity), true);
            DrawHorizontalScars(grid, cols, rows, content, seed + 7010, 3, true);
            break;
        case 28:
            DrawBloodDrips(grid, cols, rows, content, seed + 7100, true, true,
                           std::max(5, v.motifDensity));
            DrawRitualCrosses(grid, cols, rows, content, seed + 7110,
                              std::max(3, v.motifDensity / 2), false);
            break;
        case 29:
            DrawObelisks(grid, cols, rows, content, seed + 7200, 1, true);
            DrawEmberSwarm(grid, cols, rows, content, seed + 7210, 18, false);
            break;
        case 31:
            DrawBloodDrips(grid, cols, rows, content, seed + 7300,
                           false, true, std::max(3, v.motifDensity));
            DrawRitualCrosses(grid, cols, rows, content, seed + 7310, 3, false);
            break;
        default:
            break;
    }

    // ---- Финальный слой безысходности: каждый рецепт получает отдельный
    // характер угрозы поверх своей основной архитектуры. Он намеренно
    // вызывается ПОСЛЕ основного мотива, но перед основанием, чтобы мелкие
    // следы могли "переехать" крупные декоративные штрихи и чувствоваться
    // как органическая грязь, а не как аккуратный UI-орнамент.
    DrawDespairLayer(grid, cols, rows, content, seed + 9000, variantIndex,
                     std::max(3, v.motifDensity));

    // ---- Основание — рваная черта под content-зоной, только если снизу
    // реально есть место под полноценную линию. Одиночный факел (без линии)
    // требует куда меньше места и ставится отдельно, только для главного
    // меню (пауза внизу не украшается факелом, см. !pauseMenu — так было и
    // раньше). bottomDoubleRule добавляет вторую, более тонкую полосу чуть
    // ниже первой — только если места хватает на обе.
    if (!pauseMenu && v.bottomTorch && bottomGapAvailable >= 2) {
        const int torchX = (content.x0 + content.x1) / 2 - std::min(20, (content.x1 - content.x0) / 3);
        DrawTorch(grid, cols, rows, torchX, content.y1 + 1, seed + 15);
    }
    if (v.bottomRule && bottomGapAvailable >= 4) {
        const int ruleWidth = std::min(content.x1 - content.x0, cols / 3);
        const int ruleY = content.y1 + std::min(bottomGapAvailable - 1, bottomGapAvailable / 2 + 1);
        const int ruleX = (content.x0 + content.x1) / 2 - ruleWidth / 2 + 2;
        DrawWornEdgeH(grid, cols, rows, ruleY, ruleX, ruleX + ruleWidth, seed + 777, false);

        if (v.bottomDoubleRule && bottomGapAvailable >= 6) {
            const int ruleY2 = std::min(rows - edge - 2, ruleY + 2);
            DrawWornEdgeH(grid, cols, rows, ruleY2, ruleX, ruleX + ruleWidth, seed + 787, true);
        }
    }
}

} // namespace MainMenu
