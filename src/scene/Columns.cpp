#include "Columns.h"

// THE MOST HATEFULL PLACE
// BECAUSE OF THESE COLUMNS, ENEMYAI CAN STUCK
// but it makes the maze look better

namespace Columns {

namespace {
// Seeded hash with a salt distinct from WallShapes
// column and chamfer decisions are independent
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

    // Collect candidates before mutating, or a new floor cell would qualify its neighbors in the
    // same pass. Isolated (4 open sides) and T-shaped (3 open) cells qualify
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
        // T-shaped candidates are far more common than isolated ones; dampen them so column density
        // follows the zone profile
        const float probability = c.isTShape ? baseProbability * kTShapeAcceptanceMultiplier : baseProbability;
        if (HashUnitFloat(seed, c.x, c.z) >= probability) continue; // this region "rejected" the candidate

        map[(size_t)c.z * mapW + c.x] = 2; // now floor: the column stands on it
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
