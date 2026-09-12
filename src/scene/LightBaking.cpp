#include "LightBaking.h"
#include <algorithm>
#include <cmath>

namespace LightBaking {

namespace {

// Must match the hard light-distance cutoff in assets/shaders/scene.frag
// (`if (lightDist > 8.0) continue;`) — beyond this radius the shader skips
// the torch entirely before it would ever sample baked visibility, so
// baking further than this per torch is pure wasted work. Small margin
// added since the shader compares full 3D distance (a torch sits slightly
// above the floor plane) while this bakes a flat XZ radius.
constexpr float kLightRadius = 8.0f + 0.5f;

// Exact CPU mirror of shadowedByWall() in assets/shaders/scene.frag, minus
// the per-fragment jitter (that jitter existed only to soften a hard raster
// edge — irrelevant once we're baking a fixed per-cell result) and minus the
// early-out "dist < 0.15" case (a shading point standing exactly on top of
// its own light is always considered lit, same as the shader).
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

    // ---- Columns: same ray-vs-circle test as the GLSL version ----
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
                // else: ray passes through the cut-out wedge, keep tracing.
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

BakedVisibility Bake(
    int mapW, int mapH,
    const std::vector<int>& map,
    const std::vector<WallShapes::CornerCut>& cornerCuts,
    const std::vector<unsigned char>& diagonalChainMask,
    const std::vector<glm::vec2>& columnCentersXZ,
    float columnRadius,
    const std::vector<glm::vec3>& torchFlamePos)
{
    BakedVisibility out;
    out.mapW = mapW;
    out.mapH = mapH;
    out.subRes = mapW * kSubCellsPerAxis; // square map, mapW == mapH in practice
    out.torchCount = (int)torchFlamePos.size();

    if (out.torchCount <= 0 || mapW <= 0 || mapH <= 0)
        return out;

    const int subW = mapW * kSubCellsPerAxis;
    const int subH = mapH * kSubCellsPerAxis;
    // Everything starts at 0 ("shadowed"), NOT 1 — cells outside a torch's
    // kLightRadius bounding box are left at this default and are never
    // sampled by the shader (it skips the torch via its own distance check
    // first), so their value is irrelevant either way; 0 is just the
    // safer default if that assumption is ever wrong somewhere.
    out.data.assign((size_t)subW * subH * out.torchCount, 0);

    const float invSub = 1.0f / (float)kSubCellsPerAxis;

    for (int t = 0; t < out.torchCount; ++t)
    {
        const glm::vec2 lightXZ(torchFlamePos[t].x, torchFlamePos[t].z);
        uint8_t* layer = out.data.data() + (size_t)t * subW * subH;

        // Perf: only bake the bounding box the torch can actually reach
        // (see kLightRadius above) instead of the ENTIRE map for every
        // torch. On a 128x128 map that's ~16384 cells vs. ~200 cells per
        // torch — baking the whole map for every torch was the actual
        // cause of a multi-second-to-minutes synchronous stall during
        // level load (Windows shows this as the window "Not Responding"
        // — it isn't hung, it just isn't pumping messages while this
        // runs on the main thread), especially in an unoptimized Debug
        // build. This bounding box is the fix.
        int sx0 = (int)std::floor((lightXZ.x - kLightRadius) * kSubCellsPerAxis);
        int sx1 = (int)std::ceil ((lightXZ.x + kLightRadius) * kSubCellsPerAxis);
        int sz0 = (int)std::floor((lightXZ.y - kLightRadius) * kSubCellsPerAxis);
        int sz1 = (int)std::ceil ((lightXZ.y + kLightRadius) * kSubCellsPerAxis);

        sx0 = std::max(sx0, 0); sx1 = std::min(sx1, subW - 1);
        sz0 = std::max(sz0, 0); sz1 = std::min(sz1, subH - 1);

        for (int sz = sz0; sz <= sz1; ++sz)
        {
            for (int sx = sx0; sx <= sx1; ++sx)
            {
                glm::vec2 sampleXZ(
                    (sx + 0.5f) * invSub,
                    (sz + 0.5f) * invSub);

                // Also skip cells further than kLightRadius from the torch
                // even inside the bounding box (a circle inscribed in a
                // square only covers ~79% of it) — same reasoning as above.
                if (glm::distance(sampleXZ, lightXZ) > kLightRadius)
                    continue;

                bool blocked = TraceBlocked(
                    sampleXZ, lightXZ,
                    mapW, mapH, map, cornerCuts, diagonalChainMask,
                    columnCentersXZ, columnRadius);

                layer[(size_t)sz * subW + sx] = blocked ? 0 : 1;
            }
        }
    }

    return out;
}

} // namespace LightBaking
