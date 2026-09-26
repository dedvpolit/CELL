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

    // The seed comes from the caller: NEW GAME generates a fresh random one, CONTINUE passes the
    // saved one. The same seed always unfolds into the same maze (deterministic mt19937), so the
    // geometry itself does not need saving.
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

    // Starting safe zone (player spawn): a 10x10 cell square near corner (0,0).
    const int safeX0 = 2, safeZ0 = 2, safeX1 = 11, safeZ1 = 11;
    for (int z = safeZ0; z <= safeZ1; z++)
        for (int x = safeX0; x <= safeX1; x++)
            setFloor(x, z);

    for (int z = safeZ0; z <= safeZ1; z++)
        setFloor(safeX1 + 1, z);

    // Finish safe zone (opposite corner), the same size as the starting one; the player reaches it
    // by running through the whole maze. The win button stands inside it.
    result.endSafeX1 = result.mapW - 1 - safeX0;             // mirrored from safeX0 at the far edge
    result.endSafeZ1 = result.mapH - 1 - safeZ0;
    result.endSafeX0 = result.endSafeX1 - (safeX1 - safeX0); // same width as the starting zone
    result.endSafeZ0 = result.endSafeZ1 - (safeZ1 - safeZ0);

    for (int z = result.endSafeZ0; z <= result.endSafeZ1; z++)
        for (int x = result.endSafeX0; x <= result.endSafeX1; x++)
            setFloor(x, z);

    // Doors from the finish zone into the maze on the two sides facing the map's center, so the
    // room is guaranteed to connect (it probably overlaps a DFS node anyway, but the door makes the
    // passage visually explicit, like the starting zone).
    for (int z = result.endSafeZ0; z <= result.endSafeZ1; z++)
        setFloor(result.endSafeX0 - 1, z);
    for (int x = result.endSafeX0; x <= result.endSafeX1; x++)
        setFloor(x, result.endSafeZ0 - 1);

    result.winButtonPos = glm::vec3(
        (result.endSafeX0 + result.endSafeX1) * 0.5f + 0.5f,
        0.0f,
        (result.endSafeZ0 + result.endSafeZ1) * 0.5f + 0.5f
    );

    // Small safe zones (pockets) inside the maze: about a third of the starting zone's side,
    // scattered randomly. Each pocket is centered on an already visited DFS node, so it always
    // stays connected to the rest of the maze.
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

    // Diaries use the same rng as the rest of the maze (a continuation of the same sequence, not a
    // new mt19937(seed)): all that matters is that the call is deterministic from the seed, so
    // CONTINUE gets the same set.
    result.diaries = Diaries::SelectForSeed(pocketCenters, rng);

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

    // A chamfered cell has a shortened face, so the face center where a torch would mount can land
    // in the cut wedge and leave the torch floating. Such cells are excluded instead of computing a
    // safe mount point per chamfer type.
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
            glm::vec3 normal(
                (float)d.x,
                0.0f,
                (float)d.y
            );

            glm::vec3 wallCenter(
                x + 0.5f,
                0.0f,
                z + 0.5f
            );

            glm::vec3 wallBase =
                wallCenter + normal * 0.5f;

            bool duplicate = false;

            for (const glm::vec3& p :
                 out.wallBase)
            {
                glm::vec3 diff =
                    wallBase - p;

                if (glm::dot(diff, diff) < 0.15f * 0.15f)
                {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate)
                return false;

            // Mount height is below eye level with the handle nearly flush with the wall (a high,
            // far mount made the handle vanish in the ASCII output). The normal offset and height
            // mirror addTorchMesh() (0.14 out, 0.62 + 0.045 up), or flamePos would differ from
            // where the flame is drawn.
            glm::vec3 flamePos =
                wallBase
                + normal * 0.14f
                + glm::vec3(
                    0.0f,
                    0.665f,
                    0.0f
                );

            out.wallBase.push_back(
                wallBase
            );

            out.normal.push_back(
                normal
            );

            out.flamePos.push_back(
                flamePos
            );

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

    // The seed comes from the caller, as in Generate(): torch placement is deterministic, so
    // CONTINUE reproduces the saved layout (the map's seed is reused here).
    std::mt19937 rng(seed);

    struct TorchCandidate
    {
        int x;
        int z;
    };

    std::vector<TorchCandidate>
        safeCandidates;

    for (int z = 1;
         z <= 12;
         ++z)
    {
        for (int x = 1;
             x <= 12;
             ++x)
        {
            if (!isWall(x, z))
                continue;

            if (isFloor(x + 1, z) ||
                isFloor(x - 1, z) ||
                isFloor(x, z + 1) ||
                isFloor(x, z - 1))
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

    std::shuffle(
        safeCandidates.begin(),
        safeCandidates.end(),
        rng
    );

    const float safeSpacing = 3.0f;

    for (const TorchCandidate& c :
         safeCandidates)
    {
        if ((int)out.wallBase.size() >= 6)
            break;

        glm::vec3 center(
            c.x + 0.5f,
            0.0f,
            c.z + 0.5f
        );

        bool farEnough = true;

        for (const glm::vec3& p :
             out.wallBase)
        {
            glm::vec3 diff =
                center - p;

            diff.y = 0.0f;

            if (glm::dot(diff, diff) <
                safeSpacing * safeSpacing)
            {
                farEnough = false;
                break;
            }
        }

        if (!farEnough)
            continue;

        tryAddTorch(
            c.x,
            c.z
        );
    }

    std::vector<TorchCandidate>
        endSafeCandidates;

    for (int z = endSafeZ0 - 1; z <= endSafeZ1 + 1; ++z)
    {
        for (int x = endSafeX0 - 1; x <= endSafeX1 + 1; ++x)
        {
            if (!isWall(x, z))
                continue;

            if (isFloor(x + 1, z) ||
                isFloor(x - 1, z) ||
                isFloor(x, z + 1) ||
                isFloor(x, z - 1))
            {
                endSafeCandidates.push_back({ x, z });
            }
        }
    }

    std::shuffle(
        endSafeCandidates.begin(),
        endSafeCandidates.end(),
        rng
    );

    const int torchesBeforeEndZone = (int)out.wallBase.size();

    for (const TorchCandidate& c : endSafeCandidates)
    {
        if ((int)out.wallBase.size() >= torchesBeforeEndZone + 6)
            break;

        glm::vec3 center(
            c.x + 0.5f,
            0.0f,
            c.z + 0.5f
        );

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

    // Small safe zones (pockets in the maze): a guaranteed number of torches per pocket instead of
    // whatever the general maze pass happens to give.

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

    mazeCandidates.reserve(
        mapW * mapH
    );

    for (int z = 1;
         z < mapH - 1;
         ++z)
    {
        for (int x = 1;
             x < mapW - 1;
             ++x)
        {
            if (!isWall(x, z))
                continue;

            if (x >= 1 &&
                x <= 12 &&
                z >= 1 &&
                z <= 12)
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

    std::shuffle(
        mazeCandidates.begin(),
        mazeCandidates.end(),
        rng
    );

    const int MIN_MAZE_TORCHES = 256;
    const float mazeSpacing = 3.5f;

    // The threshold is computed from the torches already placed in the safe zones and pockets
    // instead of being hardcoded, so adding zones does not shift it.
    const int torchesBeforeMaze = (int)out.wallBase.size();

    for (const TorchCandidate& c :
         mazeCandidates)
    {
        if ((int)out.wallBase.size() >=
            torchesBeforeMaze + MIN_MAZE_TORCHES)
        {
            break;
        }

        glm::vec3 center(
            c.x + 0.5f,
            0.0f,
            c.z + 0.5f
        );

        bool farEnough = true;

        for (const glm::vec3& p :
             out.wallBase)
        {
            glm::vec3 diff =
                center - p;

            diff.y = 0.0f;

            if (glm::dot(diff, diff) <
                mazeSpacing * mazeSpacing)
            {
                farEnough = false;
                break;
            }
        }

        if (!farEnough)
            continue;

        tryAddTorch(
            c.x,
            c.z
        );
    }

    if ((int)out.wallBase.size() <
        torchesBeforeMaze + MIN_MAZE_TORCHES)
    {
        const float fallbackSpacing = 2.2f;

        for (const TorchCandidate& c :
             mazeCandidates)
        {
            // Fallback pass with a denser step (2.2 instead of 3.5) for when the wider step found
            // too few candidates. It must stop at the same target (torchesBeforeMaze +
            // MIN_MAZE_TORCHES), not at the global maxTorches, or it would fill the map.
            if ((int)out.wallBase.size()
                >= torchesBeforeMaze + MIN_MAZE_TORCHES)
            {
                break;
            }

            glm::vec3 center(
                c.x + 0.5f,
                0.0f,
                c.z + 0.5f
            );

            bool farEnough = true;

            for (const glm::vec3& p :
                 out.wallBase)
            {
                glm::vec3 diff =
                    center - p;

                diff.y = 0.0f;

                if (glm::dot(diff, diff) <
                    fallbackSpacing *
                    fallbackSpacing)
                {
                    farEnough = false;
                    break;
                }
            }

            if (!farEnough)
                continue;

            tryAddTorch(
                c.x,
                c.z
            );
        }
    }

    // Last resort with the densest step (1.5) if MIN_MAZE_TORCHES is still not reached (a very
    // small or tight maze). Skipped when the target is met.

    if ((int)out.wallBase.size() < torchesBeforeMaze + MIN_MAZE_TORCHES)
    {
        for (const TorchCandidate& c :
             mazeCandidates)
        {
            if ((int)out.wallBase.size()
                >= maxTorches)
            {
                break;
            }

            bool duplicate = false;

            glm::vec3 center(
                c.x + 0.5f,
                0.0f,
                c.z + 0.5f
            );

            for (const glm::vec3& p :
                 out.wallBase)
            {
                glm::vec3 diff =
                    center - p;

                diff.y = 0.0f;

                if (glm::dot(diff, diff) <
                    1.5f * 1.5f)
                {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate)
                continue;

            tryAddTorch(
                c.x,
                c.z
            );
        }
    }

    std::uniform_real_distribution<float>
        intensRange(
            2.1f,
            2.9f
        );

    for (size_t i = 0;
         i < out.wallBase.size();
         ++i)
    {
        out.color.push_back(
            glm::vec3(
                1.0f,
                0.55f,
                0.20f
            )
        );

        out.intensity.push_back(
            intensRange(rng)
        );
    }

    int total =
        (int)out.wallBase.size();

    int mazeCount =
        std::max(
            0,
            total - 6 - endSafeTorchCount - pocketTorchCount
        );

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

    // torchWallBase lies exactly on the wall/floor boundary (wallCenter + normal * 0.5), so
    // floor(p) is the right wall cell only for walls facing -x/-z. Rolling back by normal * 0.5
    // recovers wallCenter for any direction (without it about half of the torches vanished from the
    // minimap).
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
