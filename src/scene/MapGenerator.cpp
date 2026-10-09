#include "MapGenerator.h"
#include <random>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace MapGenerator {

GenerateResult Generate() {
    std::random_device rd;
    return Generate(rd());
}

GenerateResult Generate(unsigned int seed) {
    GenerateResult result;
    result.seed = seed;
    result.mapW = 128;
    result.mapH = 128;
    result.map.assign((size_t)result.mapW * result.mapH, 1);

    int nodesX = (result.mapW - 1) / 2;
    int nodesZ = (result.mapH - 1) / 2;

    std::vector<std::vector<bool>> visited(nodesZ, std::vector<bool>(nodesX, false));

    auto cellAt = [](int nx, int nz) { return glm::ivec2(nx * 2 + 1, nz * 2 + 1); };
    auto setFloor = [&](int x, int z) {
        if (x >= 0 && x < result.mapW && z >= 0 && z < result.mapH)
            result.map[z * result.mapW + x] = 2;
    };

    // Deterministic from the seed, so saves only store the seed
    std::mt19937 rng(seed);

    std::vector<glm::ivec2> stack;
    stack.push_back({0, 0});
    visited[0][0] = true;
    glm::ivec2 startCell = cellAt(0, 0);
    setFloor(startCell.x, startCell.y);

    const int dx[4] = { 1, -1, 0, 0 };
    const int dz[4] = { 0, 0, 1, -1 };

    while (!stack.empty()) {
        glm::ivec2 cur = stack.back();
        std::vector<int> dirs = { 0, 1, 2, 3 };
        std::shuffle(dirs.begin(), dirs.end(), rng);

        bool moved = false;
        for (int d : dirs) {
            int nx = cur.x + dx[d];
            int nz = cur.y + dz[d];
            if (nx < 0 || nx >= nodesX || nz < 0 || nz >= nodesZ) continue;
            if (visited[nz][nx]) continue;

            visited[nz][nx] = true;
            glm::ivec2 curCell  = cellAt(cur.x, cur.y);
            glm::ivec2 nextCell = cellAt(nx, nz);
            glm::ivec2 wallCell = (curCell + nextCell) / 2;

            setFloor(nextCell.x, nextCell.y);
            setFloor(wallCell.x, wallCell.y);

            stack.push_back({nx, nz});
            moved = true;
            break;
        }
        if (!moved) stack.pop_back();
    }

    // Starting safe zone: 10x10 cells near (0,0)
    const int safeX0 = 2, safeZ0 = 2, safeX1 = 11, safeZ1 = 11;
    for (int z = safeZ0; z <= safeZ1; z++)
        for (int x = safeX0; x <= safeX1; x++)
            setFloor(x, z);

    for (int z = safeZ0; z <= safeZ1; z++)
        setFloor(safeX1 + 1, z);

    // Finish safe zone in the opposite corner, same size;
    // the exit door stands in it
    result.endSafeX1 = result.mapW - 1 - safeX0;        // mirrored from safeX0 at the far edge
    result.endSafeZ1 = result.mapH - 1 - safeZ0;
    result.endSafeX0 = result.endSafeX1 - (safeX1 - safeX0); // same width as the starting zone
    result.endSafeZ0 = result.endSafeZ1 - (safeZ1 - safeZ0);

    for (int z = result.endSafeZ0; z <= result.endSafeZ1; z++)
        for (int x = result.endSafeX0; x <= result.endSafeX1; x++)
            setFloor(x, z);

    // Doors on the two sides facing the map center guarantee a visible connection
    for (int z = result.endSafeZ0; z <= result.endSafeZ1; z++)
        setFloor(result.endSafeX0 - 1, z);
    for (int x = result.endSafeX0; x <= result.endSafeX1; x++)
        setFloor(x, result.endSafeZ0 - 1);

    result.exitDoorPos = glm::vec3(
        (result.endSafeX0 + result.endSafeX1) * 0.5f + 0.5f,
        0.0f,
        (result.endSafeZ0 + result.endSafeZ1) * 0.5f + 0.5f
    );

    // Pockets:
    // about a third of the starting zone's side, each centered on a visited DFS node so it stays connected
    const int startSide = safeX1 - safeX0 + 1;
    const int smallSafeSide = std::max(1, startSide / 3);
    const int smallSafeRadius = smallSafeSide / 2;
    const int smallSafeCount = 12;
    const int minPocketSpacing = 8; // minimum distance between pockets/zones, in cells

    std::vector<glm::ivec2> pocketCenters;
    std::uniform_int_distribution<int> nxDist(1, std::max(1, nodesX - 2));
    std::uniform_int_distribution<int> nzDist(1, std::max(1, nodesZ - 2));

    int pocketAttempts = 0;
    while ((int)pocketCenters.size() < smallSafeCount && pocketAttempts < 3000) {
        pocketAttempts++;
        glm::ivec2 cell = cellAt(nxDist(rng), nzDist(rng));

        if (cell.x <= safeX1 + minPocketSpacing && cell.y <= safeZ1 + minPocketSpacing)
            continue;
        if (cell.x >= result.endSafeX0 - minPocketSpacing && cell.y >= result.endSafeZ0 - minPocketSpacing)
            continue;

        bool tooClose = false;
        for (const glm::ivec2& p : pocketCenters) {
            if (std::abs(p.x - cell.x) < minPocketSpacing &&
                std::abs(p.y - cell.y) < minPocketSpacing) {
                tooClose = true;
                break;
            }
        }
        if (tooClose)
            continue;

        pocketCenters.push_back(cell);
    }

    for (const glm::ivec2& c : pocketCenters) {
        for (int z = c.y - smallSafeRadius; z <= c.y + smallSafeRadius; z++)
            for (int x = c.x - smallSafeRadius; x <= c.x + smallSafeRadius; x++)
                setFloor(x, z);
    }

    result.smallSafeZoneCenters = pocketCenters;
    result.smallSafeZoneRadius = smallSafeRadius;

    // Same rng sequence: deterministic from the seed
    result.diaries = Diaries::PlaceInPockets(pocketCenters);
    result.storyVariants = Diaries::PickVariants(rng);

    return result;
}
static bool TryAddTorch(int x, int z,
                        const std::function<bool(int, int)>& isWall,
                        const std::function<bool(int, int)>& isFloor,
                        const std::function<bool(int, int)>& isChamfered,
                        TorchPlacement& out)
{
    if (!isWall(x, z))
        return false;

    // A chamfered face can leave the mount point in the cut wedge; skip such cells
    if (isChamfered && isChamfered(x, z))
        return false;

    static const glm::ivec2 dirs[4] =
    {
        { 1, 0 },
        {-1, 0 },
        { 0, 1 },
        { 0,-1 }
    };

    for (auto d : dirs)
    {
        if (isFloor(x + d.x, z + d.y))
        {
            glm::vec3 normal((float)d.x, 0.0f, (float)d.y);

            glm::vec3 wallCenter(x + 0.5f, 0.0f, z + 0.5f);

            glm::vec3 wallBase = wallCenter + normal * 0.5f;

            bool duplicate = false;

            for (const glm::vec3& p : out.wallBase)
            {
                glm::vec3 diff = wallBase - p;

                if (glm::dot(diff, diff) < 0.15f * 0.15f)
                {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate)
                return false;

            // Below eye level, handle nearly flush with the wall.
            // Must match SceneGeometry::AddTorchMesh() (tip 0.14 out, 0.62 up; flame center 0.045 above it)
            glm::vec3 flamePos = wallBase + normal * 0.14f + glm::vec3(0.0f, 0.665f, 0.0f);

            out.wallBase.push_back(wallBase);

            out.normal.push_back(normal);

            out.flamePos.push_back(flamePos);

            return true;
        }
    }

    return false;
}

TorchPlacement PlaceTorches(
    int mapW, int mapH,
    int endSafeX0, int endSafeZ0, int endSafeX1, int endSafeZ1,
    const std::vector<glm::ivec2>& smallSafeZoneCenters,
    int smallSafeZoneRadius,
    const std::function<bool(int, int)>& isWall,
    const std::function<bool(int, int)>& isFloor,
    const std::function<bool(int, int)>& isChamfered,
    int maxTorches)
{
    std::random_device rd;
    return PlaceTorches(mapW, mapH, endSafeX0, endSafeZ0, endSafeX1, endSafeZ1,
                         smallSafeZoneCenters, smallSafeZoneRadius,
                         isWall, isFloor, isChamfered, maxTorches, rd());
}

TorchPlacement PlaceTorches(
    int mapW, int mapH,
    int endSafeX0, int endSafeZ0, int endSafeX1, int endSafeZ1,
    const std::vector<glm::ivec2>& smallSafeZoneCenters,
    int smallSafeZoneRadius,
    const std::function<bool(int, int)>& isWall,
    const std::function<bool(int, int)>& isFloor,
    const std::function<bool(int, int)>& isChamfered,
    int maxTorches,
    unsigned int seed)
{
    TorchPlacement out;

    auto tryAddTorch = [&](int x, int z) {
        return TryAddTorch(x, z, isWall, isFloor, isChamfered, out);
    };

    // Deterministic from the map seed
    std::mt19937 rng(seed);

    struct TorchCandidate
    {
        int x;
        int z;
    };

    std::vector<TorchCandidate>
        safeCandidates;

    for (int z = 1; z <= 12; ++z)
    {
        for (int x = 1; x <= 12; ++x)
        {
            if (!isWall(x, z))
                continue;

            if (isFloor(x + 1, z) || isFloor(x - 1, z) || isFloor(x, z + 1) || isFloor(x, z - 1))
            {
                safeCandidates.push_back(
                    {
                        x,
                        z
                    }
                );
            }
        }
    }

    std::shuffle(safeCandidates.begin(), safeCandidates.end(), rng);

    const float safeSpacing = 3.0f;

    for (const TorchCandidate& c : safeCandidates)
    {
        if ((int)out.wallBase.size() >= 6)
            break;

        glm::vec3 center(c.x + 0.5f, 0.0f, c.z + 0.5f);

        bool farEnough = true;

        for (const glm::vec3& p : out.wallBase)
        {
            glm::vec3 diff = center - p;

            diff.y = 0.0f;

            if (glm::dot(diff, diff) < safeSpacing * safeSpacing)
            {
                farEnough = false;
                break;
            }
        }

        if (!farEnough)
            continue;

        tryAddTorch(c.x, c.z);
    }

    std::vector<TorchCandidate>
        endSafeCandidates;

    for (int z = endSafeZ0 - 1; z <= endSafeZ1 + 1; ++z)
    {
        for (int x = endSafeX0 - 1; x <= endSafeX1 + 1; ++x)
        {
            if (!isWall(x, z))
                continue;

            if (isFloor(x + 1, z) || isFloor(x - 1, z) || isFloor(x, z + 1) || isFloor(x, z - 1))
            {
                endSafeCandidates.push_back({ x, z });
            }
        }
    }

    std::shuffle(endSafeCandidates.begin(), endSafeCandidates.end(), rng);

    const int torchesBeforeEndZone = (int)out.wallBase.size();

    for (const TorchCandidate& c : endSafeCandidates)
    {
        if ((int)out.wallBase.size() >= torchesBeforeEndZone + 6)
            break;

        glm::vec3 center(c.x + 0.5f, 0.0f, c.z + 0.5f);

        bool farEnough = true;

        for (const glm::vec3& p : out.wallBase)
        {
            glm::vec3 diff = center - p;
            diff.y = 0.0f;

            if (glm::dot(diff, diff) < safeSpacing * safeSpacing)
            {
                farEnough = false;
                break;
            }
        }

        if (!farEnough)
            continue;

        tryAddTorch(c.x, c.z);
    }

    const int endSafeTorchCount = (int)out.wallBase.size() - torchesBeforeEndZone;

    // Guaranteed torches per pocket

    const int torchesPerPocket = 3;
    const float smallSafeSpacing = 1.3f; // the pocket is only 3x3; the regular safeSpacing would not fit

    const int torchesBeforePockets = (int)out.wallBase.size();

    for (const glm::ivec2& pocketCenter : smallSafeZoneCenters)
    {
        std::vector<TorchCandidate> pocketCandidates;

        const int px0 = pocketCenter.x - smallSafeZoneRadius - 1;
        const int px1 = pocketCenter.x + smallSafeZoneRadius + 1;
        const int pz0 = pocketCenter.y - smallSafeZoneRadius - 1;
        const int pz1 = pocketCenter.y + smallSafeZoneRadius + 1;

        for (int z = pz0; z <= pz1; ++z)
        {
            for (int x = px0; x <= px1; ++x)
            {
                if (!isWall(x, z))
                    continue;

                if (isFloor(x + 1, z) ||
                    isFloor(x - 1, z) ||
                    isFloor(x, z + 1) ||
                    isFloor(x, z - 1))
                {
                    pocketCandidates.push_back({ x, z });
                }
            }
        }

        std::shuffle(pocketCandidates.begin(), pocketCandidates.end(), rng);

        int placedInPocket = 0;

        for (const TorchCandidate& c : pocketCandidates)
        {
            if (placedInPocket >= torchesPerPocket)
                break;

            glm::vec3 center(c.x + 0.5f, 0.0f, c.z + 0.5f);

            bool farEnough = true;

            for (const glm::vec3& p : out.wallBase)
            {
                glm::vec3 diff = center - p;
                diff.y = 0.0f;

                if (glm::dot(diff, diff) < smallSafeSpacing * smallSafeSpacing)
                {
                    farEnough = false;
                    break;
                }
            }

            if (!farEnough)
                continue;

            if (tryAddTorch(c.x, c.z))
                placedInPocket++;
        }
    }

    const int pocketTorchCount = (int)out.wallBase.size() - torchesBeforePockets;

    std::vector<TorchCandidate>
        mazeCandidates;

    mazeCandidates.reserve(mapW * mapH);

    for (int z = 1; z < mapH - 1; ++z)
    {
        for (int x = 1; x < mapW - 1; ++x)
        {
            if (!isWall(x, z))
                continue;

            if (x >= 1 && x <= 12 && z >= 1 && z <= 12)
            {
                continue;
            }

            if (x >= endSafeX0 - 1 &&
                x <= endSafeX1 + 1 &&
                z >= endSafeZ0 - 1 &&
                z <= endSafeZ1 + 1)
            {
                continue;
            }

            {
                bool insidePocket = false;

                for (const glm::ivec2& pc : smallSafeZoneCenters)
                {
                    if (x >= pc.x - smallSafeZoneRadius - 1 &&
                        x <= pc.x + smallSafeZoneRadius + 1 &&
                        z >= pc.y - smallSafeZoneRadius - 1 &&
                        z <= pc.y + smallSafeZoneRadius + 1)
                    {
                        insidePocket = true;
                        break;
                    }
                }

                if (insidePocket)
                    continue;
            }

            bool touchesCorridor =
                isFloor(x + 1, z) ||
                isFloor(x - 1, z) ||
                isFloor(x, z + 1) ||
                isFloor(x, z - 1);

            if (!touchesCorridor)
                continue;

            mazeCandidates.push_back(
                {
                    x,
                    z
                }
            );
        }
    }

    std::shuffle(mazeCandidates.begin(), mazeCandidates.end(), rng);

    const int MIN_MAZE_TORCHES = 256;
    const float mazeSpacing = 3.5f;

    // Counted, not hardcoded, so adding zones does not shift it
    const int torchesBeforeMaze = (int)out.wallBase.size();

    for (const TorchCandidate& c : mazeCandidates)
    {
        if ((int)out.wallBase.size() >= torchesBeforeMaze + MIN_MAZE_TORCHES)
        {
            break;
        }

        glm::vec3 center(c.x + 0.5f, 0.0f, c.z + 0.5f);

        bool farEnough = true;

        for (const glm::vec3& p : out.wallBase)
        {
            glm::vec3 diff = center - p;

            diff.y = 0.0f;

            if (glm::dot(diff, diff) < mazeSpacing * mazeSpacing)
            {
                farEnough = false;
                break;
            }
        }

        if (!farEnough)
            continue;

        tryAddTorch(c.x, c.z);
    }

    if ((int)out.wallBase.size() < torchesBeforeMaze + MIN_MAZE_TORCHES)
    {
        const float fallbackSpacing = 2.2f;

        for (const TorchCandidate& c : mazeCandidates)
        {
            // Denser fallback when the regular step found too few;
            // stops at the maze target, not the global maximum
            if ((int)out.wallBase.size() >= torchesBeforeMaze + MIN_MAZE_TORCHES)
            {
                break;
            }

            glm::vec3 center(c.x + 0.5f, 0.0f, c.z + 0.5f);

            bool farEnough = true;

            for (const glm::vec3& p : out.wallBase)
            {
                glm::vec3 diff = center - p;

                diff.y = 0.0f;

                if (glm::dot(diff, diff) < fallbackSpacing * fallbackSpacing)
                {
                    farEnough = false;
                    break;
                }
            }

            if (!farEnough)
                continue;

            tryAddTorch(c.x, c.z);
        }
    }

    // Densest last resort for very small or tight mazes

    if ((int)out.wallBase.size() < torchesBeforeMaze + MIN_MAZE_TORCHES)
    {
        for (const TorchCandidate& c : mazeCandidates)
        {
            if ((int)out.wallBase.size() >= maxTorches)
            {
                break;
            }

            bool duplicate = false;

            glm::vec3 center(c.x + 0.5f, 0.0f, c.z + 0.5f);

            for (const glm::vec3& p : out.wallBase)
            {
                glm::vec3 diff = center - p;

                diff.y = 0.0f;

                if (glm::dot(diff, diff) < 1.5f * 1.5f)
                {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate)
                continue;

            tryAddTorch(c.x, c.z);
        }
    }

    std::uniform_real_distribution<float>
        intensRange(2.1f, 2.9f);

    for (size_t i = 0; i < out.wallBase.size(); ++i)
    {
        out.color.push_back(glm::vec3(1.0f, 0.55f, 0.20f));

        out.intensity.push_back(intensRange(rng));
    }

    int total = (int)out.wallBase.size();

    int mazeCount = std::max(0, total - 6 - endSafeTorchCount - pocketTorchCount);

    std::fprintf(
        stderr,
        "DungeonScene: total torches = %d, safe = %d, endSafe = %d, pockets = %d, maze = %d\n",
        total,
        std::min(total, 6),
        endSafeTorchCount,
        pocketTorchCount,
        mazeCount
    );

    out.torchCellLookup = BuildTorchCellLookup(mapW, mapH, out.wallBase, out.normal);
    return out;
}

std::vector<unsigned char> BuildTorchCellLookup(
    int mapW, int mapH,
    const std::vector<glm::vec3>& torchWallBase,
    const std::vector<glm::vec3>& torchNormal)
{
    std::vector<unsigned char> lookup((size_t)mapW * mapH, 0);

    // torchWallBase lies on the wall/floor boundary;
    // step back half a cell along the normal to get the wall cell for any facing
    for (size_t i = 0; i < torchWallBase.size(); ++i)
    {
        const glm::vec3& p = torchWallBase[i];
        const glm::vec3& n = torchNormal[i];

        int mx = (int)std::floor(p.x - n.x * 0.5f);
        int mz = (int)std::floor(p.z - n.z * 0.5f);

        if (mx >= 0 && mx < mapW && mz >= 0 && mz < mapH)
            lookup[(size_t)mz * mapW + mx] = 1;
    }

    return lookup;
}

} // namespace MapGenerator
