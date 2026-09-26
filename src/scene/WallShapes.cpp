#include "WallShapes.h"

namespace WallShapes {

namespace {

// A simple deterministic hash(seed, x, z) -> [0,1). It does not need cryptographic quality, only
// (a) stability across runs with the same seed and (b) no obvious grid correlation on x/z
// (otherwise chamfered corners would line up in stripes along the axes). splitmix64-style bit
// mixing has plenty of margin for this.
float HashUnitFloat(unsigned int seed, int x, int z) {
    uint64_t h = (uint64_t)seed * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t)(uint32_t)x * 0xBF58476D1CE4E5B9ull;
    h ^= (uint64_t)(uint32_t)z * 0x94D049BB133111EBull;
    h ^= (h >> 33);
    h *= 0xFF51AFD7ED558CCDull;
    h ^= (h >> 33);
    // The top 24 bits give enough entropy for one decision per cell and fit into a float without
    // losing precision.
    return (float)((h >> 40) & 0xFFFFFF) / (float)0x1000000;
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

} // namespace

float ComputeVariedChamferSize(unsigned int seed, int x, int z) {
    // A separate salt (xor 0x51) from HashUnitFloat above (which uses different x/z multipliers),
    // so "chamfer this cell?" (BuildCornerCuts, via HashUnitFloat) and "how much?" (this function)
    // are not tightly correlated for the same cell/seed.
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

    // Exactly one convex corner: the only case that is chamfered (see the header comment about
    // teeth/columns for cornerCount >= 2).
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
            // Corner (0,0). Walking the cell counter-clockwise South -> East -> North -> West, the
            // "previous" edge before this corner is West and the "next" is South.
            out.onEdgeCcwFrom = glm::vec2(0.0f, c);
            out.onEdgeCcwTo   = glm::vec2(c, 0.0f);
            return true;
        case CornerCut::SE:
            out.onEdgeCcwFrom = glm::vec2(1.0f - c, 0.0f);
            out.onEdgeCcwTo   = glm::vec2(1.0f, c);
            return true;
        case CornerCut::NE:
            out.onEdgeCcwFrom = glm::vec2(1.0f, 1.0f - c);
            out.onEdgeCcwTo   = glm::vec2(1.0f - c, 1.0f);
            return true;
        case CornerCut::NW:
            out.onEdgeCcwFrom = glm::vec2(c, 1.0f);
            out.onEdgeCcwTo   = glm::vec2(0.0f, 1.0f - c);
            return true;
        case CornerCut::None:
        default:
            return false;
    }
}

bool IsLocalPointSolid(float lx, float lz, CornerCut cut, float c) {
    // Half-plane "inside the wedge" for each corner: the wedge is formed by a diagonal through the
    // two chamfer points (see GetChamferPoints). A point counts as cut (not solid) if it lies
    // strictly closer to the chamfered corner than the diagonal, i.e. on the same side as the
    // corner itself.
    switch (cut) {
        case CornerCut::SW:
            // Diagonal through (0,c)-(c,0): x + z = c. Corner (0,0) gives x + z = 0 < c, so the
            // wedge is where x + z < c.
            return !(lx + lz < c);
        case CornerCut::SE:
            return !((1.0f - lx) + lz < c);
        case CornerCut::NE:
            return !((1.0f - lx) + (1.0f - lz) < c);
        case CornerCut::NW:
            return !(lx + (1.0f - lz) < c);
        case CornerCut::None:
        default:
            return true;
    }
}

} // namespace WallShapes
