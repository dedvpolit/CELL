#include "LineOfSight.h"
#include <algorithm>
#include <cmath>

namespace LineOfSight {

namespace {

// CPU mirror of shadowedByWall() in assets/shaders/scene.frag, without the per-fragment jitter
// (it only softens the shadow edge on the GPU). Points closer than 0.15 are never blocked (a
// point standing exactly on its own light is always considered lit, as in the shader).
bool TraceBlocked(
    const glm::vec2& rayStart,
    const glm::vec2& rayEnd,
    int mapW, int mapH,
    const std::vector<int>& map,
    const std::vector<WallShapes::CornerCut>& cornerCuts,
    const std::vector<unsigned char>& diagonalChainMask,
    const std::vector<glm::vec2>& columnCentersXZ,
    float columnRadius)
{
    glm::vec2 delta = rayEnd - rayStart;
    float dist = glm::length(delta);
    if (dist < 0.15f)
        return false;

    glm::vec2 dir = delta / dist;

    float startT = std::min(0.05f, dist * 0.5f);
    float endT = std::max(startT, dist - 0.05f);
    glm::vec2 p0 = rayStart + dir * startT;
    glm::vec2 p1 = rayStart + dir * endT;

    for (const glm::vec2& col : columnCentersXZ)
    {
        glm::vec2 seg = p1 - p0;
        float segLenSq = glm::dot(seg, seg);
        float t = segLenSq > 1e-9f
            ? glm::clamp(glm::dot(col - p0, seg) / segLenSq, 0.0f, 1.0f)
            : 0.0f;
        glm::vec2 closest = p0 + seg * t;
        if (glm::length(col - closest) < columnRadius)
            return true;
    }

    glm::ivec2 mapMax(mapW - 1, mapH - 1);
    glm::ivec2 cell((int)std::floor(p0.x), (int)std::floor(p0.y));
    glm::ivec2 endCell((int)std::floor(p1.x), (int)std::floor(p1.y));

    glm::ivec2 stepDir(
        dir.x > 0.0f ? 1 : (dir.x < 0.0f ? -1 : 0),
        dir.y > 0.0f ? 1 : (dir.y < 0.0f ? -1 : 0));

    const float BIG = 1.0e8f;
    float tDeltaX = std::abs(dir.x) > 1e-6f ? std::abs(1.0f / dir.x) : BIG;
    float tDeltaY = std::abs(dir.y) > 1e-6f ? std::abs(1.0f / dir.y) : BIG;

    float nextBoundaryX = stepDir.x > 0 ? float(cell.x + 1) : float(cell.x);
    float nextBoundaryY = stepDir.y > 0 ? float(cell.y + 1) : float(cell.y);

    float tMaxX = std::abs(dir.x) > 1e-6f ? (nextBoundaryX - p0.x) / dir.x : BIG;
    float tMaxY = std::abs(dir.y) > 1e-6f ? (nextBoundaryY - p0.y) / dir.y : BIG;

    const int MAX_STEPS = 48;
    float tEnter = 0.0f;

    for (int i = 0; i < MAX_STEPS; ++i)
    {
        if (cell.x < 0 || cell.y < 0 || cell.x > mapMax.x || cell.y > mapMax.y)
            return true;

        size_t idx = (size_t)cell.y * mapW + cell.x;
        float tExit = std::min(tMaxX, tMaxY);

        if (idx < map.size() && map[idx] == 1)
        {
            WallShapes::CornerCut cut = idx < cornerCuts.size()
                ? cornerCuts[idx] : WallShapes::CornerCut::None;

            if (cut == WallShapes::CornerCut::None)
            {
                return true;
            }
            else
            {
                bool isChain = idx < diagonalChainMask.size() && diagonalChainMask[idx] != 0;
                float chamferSize = isChain ? 0.92f : 0.35f;

                float tMid = glm::clamp((tEnter + std::min(tExit, dist)) * 0.5f, 0.0f, dist);
                glm::vec2 hitPoint = rayStart + dir * tMid;
                glm::vec2 localXZ = hitPoint - glm::vec2(cell);
                if (WallShapes::IsLocalPointSolid(localXZ.x, localXZ.y, cut, chamferSize))
                    return true;
                // else: the ray passes through the cut-out wedge, keep tracing.
            }
        }

        if (cell == endCell)
            break;

        tEnter = tExit;
        if (tMaxX < tMaxY)
        {
            cell.x += stepDir.x;
            tMaxX += tDeltaX;
        }
        else
        {
            cell.y += stepDir.y;
            tMaxY += tDeltaY;
        }
    }

    return false;
}

} // namespace

bool HasLineOfSight(
    const glm::vec2& fromXZ,
    const glm::vec2& toXZ,
    int mapW, int mapH,
    const std::vector<int>& map,
    const std::vector<WallShapes::CornerCut>& cornerCuts,
    const std::vector<unsigned char>& diagonalChainMask,
    const std::vector<glm::vec2>& columnCentersXZ,
    float columnRadius)
{
    return !TraceBlocked(fromXZ, toXZ, mapW, mapH, map, cornerCuts,
                          diagonalChainMask, columnCentersXZ, columnRadius);
}

} // namespace LineOfSight
