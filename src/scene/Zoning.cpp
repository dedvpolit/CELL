#include "Zoning.h"
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace Zoning {

namespace {

// Тот же hash-приём, что и в WallShapes.cpp (splitmix64-подобное
// перемешивание битов) — здесь по (seed, regionIndex, salt): salt
// разводит несколько независимых "случайных" чисел для ОДНОГО и того же
// региона (позиция центра, chamferProbability, columnProbability,
// widenProbability), чтобы они не были жёстко скоррелированы друг с
// другом.
float HashUnitFloat(unsigned int seed, int regionIndex, uint32_t salt) {
    uint64_t h = (uint64_t)seed * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t)(uint32_t)regionIndex * 0xBF58476D1CE4E5B9ull;
    h ^= (uint64_t)salt * 0xD6E8FEB86659FD93ull;
    h ^= (h >> 33);
    h *= 0xFF51AFD7ED558CCDull;
    h ^= (h >> 33);
    return (float)((h >> 40) & 0xFFFFFF) / (float)0x1000000;
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

} // namespace

ZoneGrid BuildZoneGrid(int mapW, int mapH, unsigned int seed) {
    ZoneGrid grid;
    if (mapW <= 0 || mapH <= 0) {
        grid.centers.assign(1, RegionCenter{0.0f, 0.0f});
        grid.styles.assign(1, ZoneStyle{});
        return grid;
    }

    // Число регионов — площадь карты / целевая площадь региона, минимум
    // 1 (совсем маленькие карты — один регион на всю карту, разумное
    // вырожденное поведение вместо кучи микро-blob'ов).
    const int numRegions = std::max(1, (mapW * mapH) / kTargetRegionArea);

    grid.centers.resize((size_t)numRegions);
    grid.styles.resize((size_t)numRegions);

    // Минимальное расстояние между центрами регионов — без него при
    // чисто случайном разбросе иногда выпадают два центра почти впритык
    // друг к другу (см. историю правок — реальный скриншот показал явно
    // наложившиеся друг на друга debug-маркеры соседних регионов около
    // одной точки). Порог — доля от "типичного" радиуса региона при
    // равномерном покрытии; ниже него регион был бы неотличимо тонкой
    // полоской, а не осмысленным blob'ом.
    const float typicalRegionRadius = std::sqrt((float)kTargetRegionArea / 3.14159265f);
    const float minCenterDistance = typicalRegionRadius * 0.8f;
    const int kMaxRejectionAttempts = 40;

    for (int i = 0; i < numRegions; ++i) {
        // Позиция центра региона — равномерно по всей карте (два
        // независимых хэша под salt 10/11), НЕ привязана к какой-либо
        // сетке — отсюда и органичная (Voronoi) форма итоговых регионов,
        // в отличие от прежних прямоугольных секторов фиксированного
        // размера. Rejection sampling (доп. salt 20 на попытку) отбрасывает
        // позицию, если она слишком близко к уже размещённому центру —
        // см. minCenterDistance выше. После kMaxRejectionAttempts неудач
        // просто берём последнюю попытку как есть (не зацикливаемся
        // навечно на очень плотных картах, где столько несовместимых
        // друг с другом регионов физически не помещается).
        float px = 0.0f, pz = 0.0f;
        for (int attempt = 0; attempt < kMaxRejectionAttempts; ++attempt) {
            px = HashUnitFloat(seed, i, 10u + (uint32_t)attempt * 100u) * (float)mapW;
            pz = HashUnitFloat(seed, i, 11u + (uint32_t)attempt * 100u) * (float)mapH;

            bool tooClose = false;
            for (int j = 0; j < i; ++j) {
                const float dx = px - grid.centers[(size_t)j].x;
                const float dz = pz - grid.centers[(size_t)j].z;
                if (dx * dx + dz * dz < minCenterDistance * minCenterDistance) {
                    tooClose = true;
                    break;
                }
            }
            if (!tooClose) break;
        }
        grid.centers[(size_t)i] = RegionCenter{px, pz};

        const float chamferT = HashUnitFloat(seed, i, 1u);
        const float columnT  = HashUnitFloat(seed, i, 2u);
        const float widenT   = HashUnitFloat(seed, i, 3u);
        const float paletteT = HashUnitFloat(seed, i, 4u);

        ZoneStyle style;
        style.chamferProbability = Lerp(kChamferProbabilityMin, kChamferProbabilityMax, chamferT);
        style.columnProbability  = Lerp(kColumnProbabilityMin, kColumnProbabilityMax, columnT);
        style.widenProbability   = Lerp(kWidenProbabilityMin, kWidenProbabilityMax, widenT);
        style.paletteIndex       = std::min(kPaletteCount - 1, (int)(paletteT * kPaletteCount));
        grid.styles[(size_t)i] = style;
    }

    return grid;
}

const ZoneStyle& StyleAt(const ZoneGrid& grid, int cellX, int cellZ) {
    static const ZoneStyle kDefaultStyle{};
    if (grid.centers.empty()) return kDefaultStyle;

    // Voronoi: ближайший центр региона по квадрату расстояния (корень
    // не нужен — порядок сравнения тот же). Линейный проход по всем
    // центрам — при типичном числе регионов (единицы-десятки на карту,
    // см. kTargetRegionArea) это дёшево и вызывается редко (только при
    // построении карты, не каждый кадр).
    const float fx = (float)cellX, fz = (float)cellZ;
    size_t best = 0;
    float bestDistSq = 1e30f;
    for (size_t i = 0; i < grid.centers.size(); ++i) {
        const float dx = fx - grid.centers[i].x;
        const float dz = fz - grid.centers[i].z;
        const float distSq = dx * dx + dz * dz;
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            best = i;
        }
    }
    return grid.styles[best];
}

} // namespace Zoning
