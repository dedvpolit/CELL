#include "GridPathfinding.h"
#include <queue>
#include <algorithm>
#include <cstdlib>

namespace GridPathfinding {

std::vector<glm::ivec2> FindPath(
    int mapW, int mapH,
    const std::vector<int>& map,
    glm::ivec2 start,
    glm::ivec2 goal)
{
    std::vector<glm::ivec2> path;

    auto inBounds = [&](glm::ivec2 p) {
        return p.x >= 0 && p.y >= 0 && p.x < mapW && p.y < mapH;
    };
    auto isWalkable = [&](glm::ivec2 p) {
        if (!inBounds(p)) return false;
        return map[(size_t)p.y * mapW + p.x] != 1;
    };
    auto idx = [&](glm::ivec2 p) { return p.y * mapW + p.x; };

    if (!isWalkable(start) || !isWalkable(goal))
        return path; // one of the points is physically inside a wall: no path

    if (start == goal)
    {
        path.push_back(start);
        return path;
    }

    // -1 = unvisited; the start is its own parent
    std::vector<int> cameFrom((size_t)mapW * mapH, -1);
    cameFrom[idx(start)] = idx(start);

    std::queue<glm::ivec2> q;
    q.push(start);

    bool found = false;
    const glm::ivec2 dirs[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };

    while (!q.empty())
    {
        glm::ivec2 cur = q.front();
        q.pop();

        if (cur == goal)
        {
            found = true;
            break;
        }

        for (const glm::ivec2& d : dirs)
        {
            glm::ivec2 next = cur + d;
            if (!isWalkable(next))
                continue;
            if (cameFrom[idx(next)] != -1)
                continue;

            cameFrom[idx(next)] = idx(cur);
            q.push(next);
        }
    }

    if (!found)
        return path; // empty: the caller (EnemyAI) treats this as "no path"

    glm::ivec2 cur = goal;
    while (idx(cur) != cameFrom[idx(cur)])
    {
        path.push_back(cur);
        int parentIdx = cameFrom[idx(cur)];
        cur = glm::ivec2(parentIdx % mapW, parentIdx / mapW);
    }
    path.push_back(start);

    std::reverse(path.begin(), path.end());
    return path;
}

bool HasGridLineOfSight(int mapW, int mapH, const std::vector<int>& map, glm::ivec2 a, glm::ivec2 b)
{
    auto isWalkable = [&](int x, int z) {
        if (x < 0 || z < 0 || x >= mapW || z >= mapH) return false;
        return map[(size_t)z * mapW + x] != 1;
    };

    if (!isWalkable(a.x, a.y) || !isWalkable(b.x, b.y))
        return false;

    int x = a.x, z = a.y;
    const int dx = std::abs(b.x - a.x);
    const int dz = std::abs(b.y - a.y);
    const int sx = (a.x < b.x) ? 1 : ((a.x > b.x) ? -1 : 0);
    const int sz = (a.y < b.y) ? 1 : ((a.y > b.y) ? -1 : 0);
    int err = dx - dz;

    // Bresenham; a diagonal step between two walls is blocked, cutting a corner past one wall is allowed
    while (x != b.x || z != b.y)
    {
        const int e2 = 2 * err;
        const bool stepX = e2 > -dz;
        const bool stepZ = e2 < dx;

        const int nx = stepX ? x + sx : x;
        const int nz = stepZ ? z + sz : z;

        if (stepX) err -= dz;
        if (stepZ) err += dx;

        if (stepX && stepZ)
        {
            const bool sideA = isWalkable(nx, z);
            const bool sideB = isWalkable(x, nz);
            if (!sideA && !sideB)
                return false;
        }

        if (!isWalkable(nx, nz))
            return false;

        x = nx;
        z = nz;
    }

    return true;
}

std::vector<glm::ivec2> SmoothPath(
    int mapW, int mapH,
    const std::vector<int>& map,
    const std::vector<glm::ivec2>& cellPath)
{
    std::vector<glm::ivec2> smoothed;
    const size_t n = cellPath.size();
    if (n == 0)
        return smoothed;

    smoothed.push_back(cellPath[0]);
    size_t anchor = 0; // index of the last anchored point in cellPath

    for (size_t i = 1; i < n; ++i)
    {
        if (i == n - 1)
        {
            // The last point (the goal itself) is always kept, even if it is visible in a straight line
            // (otherwise there would be nowhere to go past the anchor)
            smoothed.push_back(cellPath[i]);
            break;
        }

        // Extend the anchor's line of sight along the path; when it breaks, the last visible point becomes the next anchor
        if (!HasGridLineOfSight(mapW, mapH, map, cellPath[anchor], cellPath[i + 1]))
        {
            smoothed.push_back(cellPath[i]);
            anchor = i;
        }
    }

    return smoothed;
}

} // namespace GridPathfinding
