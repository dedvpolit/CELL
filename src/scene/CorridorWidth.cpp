#include "CorridorWidth.h"
#include <cstdint>

namespace CorridorWidth {

namespace {
// Тот же splitmix64-приём, что и в WallShapes.cpp/Columns.cpp — своя соль,
// чтобы решение "расширить корридор здесь" не было жёстко скоррелировано
// с решениями "срезать угол"/"поставить колонну" для той же клетки/seed.
float HashUnitFloat(unsigned int seed, int x, int z) {
    uint64_t h = (uint64_t)seed * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t)(uint32_t)x * 0xA24BAED4963EE407ull;
    h ^= (uint64_t)(uint32_t)z * 0x9FB21C651E98DF25ull;
    h ^= (h >> 33);
    h *= 0xFF51AFD7ED558CCDull;
    h ^= (h >> 33);
    return (float)((h >> 40) & 0xFFFFFF) / (float)0x1000000;
}
} // namespace

std::vector<unsigned char> ApplyWidening(
    int mapW, int mapH,
    std::vector<int>& map,
    unsigned int seed,
    const std::function<float(int, int)>& widenProbabilityAt)
{
    std::vector<unsigned char> widened((size_t)mapW * (size_t)mapH, 0);
    if (mapW <= 0 || mapH <= 0) return widened;

    auto isFloor = [&](int x, int z) {
        if (x < 0 || x >= mapW || z < 0 || z >= mapH) return false;
        return map[(size_t)z * mapW + x] == 2;
    };
    auto isWallCell = [&](int x, int z) {
        if (x < 0 || x >= mapW || z < 0 || z >= mapH) return true;
        return map[(size_t)z * mapW + x] == 1;
    };

    // Собираем кандидатов ПЕРЕД мутацией map — тот же приём, что и в
    // Columns::BuildColumns: иначе только что расширенная клетка сама
    // открыла бы соседнюю стену с ещё одной стороны и неверно "заразила"
    // бы её в этом же проходе (домино-эффект вместо независимых решений
    // по исходному лабиринту).
    std::vector<std::pair<int,int>> candidates;
    for (int z = 0; z < mapH; ++z) {
        for (int x = 0; x < mapW; ++x) {
            if (!isWallCell(x, z)) continue;

            const bool hOpen = isFloor(x - 1, z) && isFloor(x + 1, z);
            const bool vOpen = isFloor(x, z - 1) && isFloor(x, z + 1);

            // Клетка, открытая СРАЗУ по обеим осям — изолированный "зуб"
            // стены (4 ортогональных соседа открыты) — это ровно
            // eligibility Columns::BuildColumns (см. Columns.h), не
            // трогаем её здесь, чтобы не отбирать кандидата у колонн.
            if (hOpen && vOpen) continue;
            if (!hOpen && !vOpen) continue; // не "стена-перегородка" вовсе

            candidates.emplace_back(x, z);
        }
    }

    for (const auto& [x, z] : candidates) {
        const float probability = widenProbabilityAt ? widenProbabilityAt(x, z) : kWidenProbability;
        if (HashUnitFloat(seed, x, z) >= probability) continue;

        map[(size_t)z * mapW + x] = 2;
        widened[(size_t)z * mapW + x] = 1;
    }

    return widened;
}

} // namespace CorridorWidth
