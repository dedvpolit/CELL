#include "Columns.h"

namespace Columns {

namespace {
// Тот же splitmix64-приём, что и в WallShapes.cpp — здесь решает, стать
// ли ЭТОМУ конкретному eligible-кандидату колонной, когда задан
// columnProbabilityAt (см. Zoning.h). Соль отличается от WallShapes,
// чтобы решение "срез угла" и решение "колонна" не были жёстко
// скоррелированы для одной и той же клетки/seed.
float HashUnitFloat(unsigned int seed, int x, int z) {
    uint64_t h = (uint64_t)seed * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t)(uint32_t)x * 0xC2B2AE3D27D4EB4Full;
    h ^= (uint64_t)(uint32_t)z * 0x165667B19E3779F9ull;
    h ^= (h >> 33);
    h *= 0xFF51AFD7ED558CCDull;
    h ^= (h >> 33);
    return (float)((h >> 40) & 0xFFFFFF) / (float)0x1000000;
}
} // namespace

std::vector<glm::vec2> BuildColumns(
    int mapW, int mapH,
    std::vector<int>& map,
    unsigned int seed,
    const std::function<float(int, int)>& columnProbabilityAt)
{
    std::vector<glm::vec2> centers;
    if (mapW <= 0 || mapH <= 0) return centers;

    auto isFloor = [&](int x, int z) {
        if (x < 0 || x >= mapW || z < 0 || z >= mapH) return false;
        return map[(size_t)z * mapW + x] == 2;
    };
    auto isWallCell = [&](int x, int z) {
        if (x < 0 || x >= mapW || z < 0 || z >= mapH) return true;
        return map[(size_t)z * mapW + x] == 1;
    };

    // Собираем кандидатов ПЕРЕД мутацией map — иначе, обходя строку за
    // строкой, только что превращённая в пол клетка сама открыла бы
    // соседнюю клетку с ещё одной стороны и могла бы неверно "заразить"
    // соседа как ещё один столб на том же проходе (тот же класс бага, что
    // и in-place-мутация сетки во время её же обхода в целом).
    //
    // Два вида кандидатов:
    //  - "изолированный" (isTShape=false) — все 4 ортогональных соседа
    //    открыты, колонна полностью свободностоящая (как было изначально).
    //  - "T-образный зуб" (isTShape=true) — открыты РОВНО 3 стороны, одна
    //    грань клетки всё ещё физически примыкает к более длинному
    //    участку стены (см. Columns.h). Геометрия та же самая — обычный
    //    цилиндр (см. AddColumnMesh в SceneGeometry.cpp): его часть со
    //    стороны закрытой грани просто уходит внутрь соседней сплошной
    //    стены и никогда не видна игроку (тот угол и так стена) — не
    //    нужно отдельной "полу-колонны" геометрии ради этого случая.
    struct Candidate { int x, z; bool isTShape; };
    std::vector<Candidate> candidates;
    for (int z = 0; z < mapH; ++z) {
        for (int x = 0; x < mapW; ++x) {
            if (!isWallCell(x, z)) continue;
            const bool wOpen = isFloor(x - 1, z);
            const bool eOpen = isFloor(x + 1, z);
            const bool sOpen = isFloor(x, z - 1);
            const bool nOpen = isFloor(x, z + 1);
            const int openCount = (wOpen?1:0) + (eOpen?1:0) + (sOpen?1:0) + (nOpen?1:0);
            if (openCount == 4) {
                candidates.push_back({x, z, false});
            } else if (openCount == 3) {
                candidates.push_back({x, z, true});
            }
        }
    }

    centers.reserve(candidates.size());
    for (const auto& c : candidates) {
        const float baseProbability = columnProbabilityAt ? columnProbabilityAt(c.x, c.z) : 1.0f;
        // T-образные кандидаты встречаются НАМНОГО чаще изолированных
        // (клетка с 3 открытыми сторонами — почти любой тупиковый выступ
        // стены, а не редкий полностью окружённый "зуб"), поэтому берём
        // ту же вероятность региона, но приглушённую фиксированным
        // множителем — иначе при том же columnProbability карта
        // покрылась бы колоннами гораздо гуще, чем предполагает Zoning
        // (там диапазон настроен исходя из редкости изолированного
        // случая, см. Zoning.h).
        const float probability = c.isTShape ? baseProbability * kTShapeAcceptanceMultiplier : baseProbability;
        if (HashUnitFloat(seed, c.x, c.z) >= probability) continue; // этот регион "отклонил" кандидата

        map[(size_t)c.z * mapW + c.x] = 2; // теперь пол — колонна стоит НА нём
        centers.emplace_back((float)c.x + 0.5f, (float)c.z + 0.5f);
    }

    return centers;
}

bool IsPointInsideColumn(float x, float z, const glm::vec2& columnCenterXZ, float radius) {
    const float dx = x - columnCenterXZ.x;
    const float dz = z - columnCenterXZ.y;
    return dx * dx + dz * dz < radius * radius;
}

} // namespace Columns
