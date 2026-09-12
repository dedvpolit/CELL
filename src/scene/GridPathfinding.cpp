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
        return path; // одна из точек физически в стене — пути нет

    if (start == goal)
    {
        path.push_back(start);
        return path;
    }

    // cameFrom[i] = -1 (не посещено), иначе линейный индекс клетки-предка.
    // Стартовая клетка сама себе предок — так отличаем "не посещено" от
    // "это старт" при восстановлении пути в конце.
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
        return path; // пустой — вызывающий код (EnemyAI) должен считать это "пути нет"

    // Восстановление пути от goal к start по cameFrom, потом разворот.
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

bool HasGridLineOfSight(
    int mapW, int mapH,
    const std::vector<int>& map,
    glm::ivec2 a,
    glm::ivec2 b)
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

    // Стандартный Брезенхэм, но с явной проверкой диагонального шага —
    // когда за один шаг меняются ОБЕ координаты, это "срезание угла"
    // между клеткой (nx, z) и клеткой (x, nz). Если ОБЕ они — стены,
    // диагональ технически проходит через щель толщиной в точку между
    // двумя стенами, поставленными по диагонали друг к другу —
    // геометрически невозможно, поэтому блокируем. Если хотя бы одна из
    // двух свободна — считаем, что можно "срезать" рядом с углом (как и
    // обычно трактуют диагональное движение по сетке в играх).
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
    size_t anchor = 0; // индекс последней ЗАКРЕПЛЁННОЙ точки в cellPath

    for (size_t i = 1; i < n; ++i)
    {
        if (i == n - 1)
        {
            // Последняя точка (сама цель) — всегда оставляем, даже если
            // видно по прямой (иначе некуда будет идти дальше anchor'а).
            smoothed.push_back(cellPath[i]);
            break;
        }

        // Пока от anchor'а всё ещё видно СЛЕДУЮЩУЮ точку по прямой — не
        // фиксируем cellPath[i], просто продолжаем расширять видимость
        // дальше по пути. Как только прямая до cellPath[i+1] чем-то
        // перекрыта — cellPath[i] это последняя точка, до которой ещё
        // было видно, фиксируем её как новый anchor.
        if (!HasGridLineOfSight(mapW, mapH, map, cellPath[anchor], cellPath[i + 1]))
        {
            smoothed.push_back(cellPath[i]);
            anchor = i;
        }
    }

    return smoothed;
}

} // namespace GridPathfinding
