#include "WallShapes.h"

namespace WallShapes {

namespace {

// Простой детерминированный hash(seed,x,z) -> [0,1). Не нужно
// криптографическое качество — только: (а) стабильность между запусками
// с тем же seed, (б) отсутствие явной решётчатой корреляции по x/z (иначе
// вырезанные углы легли бы полосами вдоль осей). splitmix64-подобное
// перемешивание битов с этим справляется с большим запасом для данной
// задачи.
float HashUnitFloat(unsigned int seed, int x, int z) {
    uint64_t h = (uint64_t)seed * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t)(uint32_t)x * 0xBF58476D1CE4E5B9ull;
    h ^= (uint64_t)(uint32_t)z * 0x94D049BB133111EBull;
    h ^= (h >> 33);
    h *= 0xFF51AFD7ED558CCDull;
    h ^= (h >> 33);
    // Верхние 24 бита — достаточно энтропии для одной decision-точки на
    // клетку, и укладываются в float без потери точности.
    return (float)((h >> 40) & 0xFFFFFF) / (float)0x1000000;
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

} // namespace

float ComputeVariedChamferSize(unsigned int seed, int x, int z) {
    // Отдельная соль (xor 0x51) от HashUnitFloat выше (там уже другие
    // множители на x/z), чтобы "срезать ли эту клетку" (BuildCornerCuts,
    // через HashUnitFloat) и "насколько сильно" (эта функция) не были
    // жёстко скоррелированы для одной и той же клетки/seed.
    uint64_t h = (uint64_t)seed * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t)(uint32_t)x * 0xA24BAED4963EE407ull;
    h ^= (uint64_t)(uint32_t)z * 0x9FB21C651E98DF25ull;
    h ^= 0x51ull;
    h ^= (h >> 33);
    h *= 0xFF51AFD7ED558CCDull;
    h ^= (h >> 33);
    const float t = (float)((h >> 40) & 0xFFFFFF) / (float)0x1000000;
    return Lerp(kChamferSizeVariedMin, kChamferSizeVariedMax, t);
}

CornerCut GetEligibleCornerCut(int mapW, int mapH, const std::vector<int>& map, int x, int z) {
    auto isFloor = [&](int cx, int cz) {
        if (cx < 0 || cx >= mapW || cz < 0 || cz >= mapH) return false;
        return map[(size_t)cz * mapW + cx] == 2;
    };
    auto isWallCell = [&](int cx, int cz) {
        if (cx < 0 || cx >= mapW || cz < 0 || cz >= mapH) return true;
        return map[(size_t)cz * mapW + cx] == 1;
    };

    if (!isWallCell(x, z)) return CornerCut::None;

    const bool wOpen = isFloor(x - 1, z);
    const bool eOpen = isFloor(x + 1, z);
    const bool sOpen = isFloor(x, z - 1);
    const bool nOpen = isFloor(x, z + 1);

    const bool swCorner = wOpen && sOpen;
    const bool seCorner = eOpen && sOpen;
    const bool neCorner = eOpen && nOpen;
    const bool nwCorner = wOpen && nOpen;

    const int cornerCount =
        (swCorner ? 1 : 0) + (seCorner ? 1 : 0) +
        (neCorner ? 1 : 0) + (nwCorner ? 1 : 0);

    // Ровно один выпуклый угол — единственный случай, который этот шаг
    // умеет срезать (см. комментарий в заголовке про "зубья"/колонны для
    // cornerCount >= 2).
    if (cornerCount != 1) return CornerCut::None;

    return swCorner ? CornerCut::SW :
           seCorner ? CornerCut::SE :
           neCorner ? CornerCut::NE :
                      CornerCut::NW;
}

std::vector<CornerCut> BuildCornerCuts(
    int mapW, int mapH,
    const std::vector<int>& map,
    unsigned int seed,
    const std::function<float(int, int)>& chamferProbabilityAt)
{
    std::vector<CornerCut> result((size_t)mapW * (size_t)mapH, CornerCut::None);
    if (mapW <= 0 || mapH <= 0) return result;

    for (int z = 0; z < mapH; ++z) {
        for (int x = 0; x < mapW; ++x) {
            const CornerCut candidate = GetEligibleCornerCut(mapW, mapH, map, x, z);
            if (candidate == CornerCut::None) continue;

            const float probability = chamferProbabilityAt
                ? chamferProbabilityAt(x, z)
                : kChamferProbability;
            if (HashUnitFloat(seed, x, z) < probability) {
                result[(size_t)z * mapW + x] = candidate;
            }
        }
    }

    return result;
}

bool GetChamferPoints(CornerCut cut, float c, ChamferPoints& out) {
    switch (cut) {
        case CornerCut::SW:
            // Угол (0,0). Обходя клетку против часовой стрелки South->East->
            // North->West, "предыдущая" грань перед этим углом — West
            // (идём ...North-edge -> West-edge -> [угол] -> South-edge...),
            // "следующая" — South.
            out.onEdgeCcwFrom = glm::vec2(0.0f, c);   // точка на западной грани
            out.onEdgeCcwTo   = glm::vec2(c, 0.0f);   // точка на южной грани
            return true;
        case CornerCut::SE:
            out.onEdgeCcwFrom = glm::vec2(1.0f - c, 0.0f); // на южной грани
            out.onEdgeCcwTo   = glm::vec2(1.0f, c);        // на восточной грани
            return true;
        case CornerCut::NE:
            out.onEdgeCcwFrom = glm::vec2(1.0f, 1.0f - c); // на восточной грани
            out.onEdgeCcwTo   = glm::vec2(1.0f - c, 1.0f); // на северной грани
            return true;
        case CornerCut::NW:
            out.onEdgeCcwFrom = glm::vec2(c, 1.0f);        // на северной грани
            out.onEdgeCcwTo   = glm::vec2(0.0f, 1.0f - c); // на западной грани
            return true;
        case CornerCut::None:
        default:
            return false;
    }
}

bool IsLocalPointSolid(float lx, float lz, CornerCut cut, float c) {
    // Полуплоскость "внутри клина" для каждого угла — клин образован
    // диагональю через две точки среза (см. GetChamferPoints). Точка
    // считается вырезанной (не сплошной), если она лежит СТРОГО ближе к
    // срезанному углу, чем диагональ — т.е. по ту же сторону, что и сам
    // угол клетки.
    switch (cut) {
        case CornerCut::SW:
            // Диагональ через (0,c)-(c,0): x + z = c. Угол (0,0) даёт
            // x+z=0 < c, значит клин — там, где x+z < c.
            return !(lx + lz < c);
        case CornerCut::SE:
            // Диагональ через (1-c,0)-(1,c): (1-x) + z = c. Угол (1,0)
            // даёт (1-1)+0=0 < c.
            return !((1.0f - lx) + lz < c);
        case CornerCut::NE:
            // Диагональ через (1,1-c)-(1-c,1): (1-x) + (1-z) = c.
            return !((1.0f - lx) + (1.0f - lz) < c);
        case CornerCut::NW:
            // Диагональ через (c,1)-(0,1-c): x + (1-z) = c.
            return !(lx + (1.0f - lz) < c);
        case CornerCut::None:
        default:
            return true; // обычный сплошной квадрат — везде стена
    }
}

} // namespace WallShapes
