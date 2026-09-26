#include "SceneGeometry.h"
#include "WallShapes.h"
#include "Columns.h"
#include "Zoning.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>

static constexpr float kWallHeight = 30.0f;

// Per-zone palette: a discrete set of pre-tuned wall/floor pairs instead of a continuous
// per-channel multiplier, which avoids muddy random tints. Index 0 is the engine's original warm
// brown stone; the others are clearly different region themes.
void SceneGeometry::GetZonePalette(int paletteIndex, glm::vec3& outWallColor, glm::vec3& outFloorColor) {
    static const glm::vec3 kPresets[Zoning::kPaletteCount][2] = {
        { glm::vec3(0.55f, 0.35f, 0.25f), glm::vec3(0.18f, 0.16f, 0.13f) }, // 0: warm brown (original)
        { glm::vec3(0.30f, 0.33f, 0.38f), glm::vec3(0.10f, 0.11f, 0.14f) }, // 1: cold blue-gray stone
        { glm::vec3(0.28f, 0.38f, 0.24f), glm::vec3(0.10f, 0.14f, 0.10f) }, // 2: mossy (muted) green
        { glm::vec3(0.42f, 0.40f, 0.38f), glm::vec3(0.15f, 0.14f, 0.13f) }, // 3: ashen gray
        { glm::vec3(0.50f, 0.28f, 0.22f), glm::vec3(0.17f, 0.12f, 0.10f) }, // 4: reddish clay
    };
    const int idx = ((paletteIndex % Zoning::kPaletteCount) + Zoning::kPaletteCount) % Zoning::kPaletteCount;
    outWallColor = kPresets[idx][0];
    outFloorColor = kPresets[idx][1];
}

static void AddQuad(std::vector<Vertex>& verts, std::vector<GLuint>& indices,
                            glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                            glm::vec3 normal, glm::vec3 color, float matId = 0.0f) {
    GLuint base = (GLuint)verts.size();
    verts.push_back({a, normal, color, matId});
    verts.push_back({b, normal, color, matId});
    verts.push_back({c, normal, color, matId});
    verts.push_back({d, normal, color, matId});

    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
    indices.push_back(base + 0);
}

void AddCylinder(std::vector<Vertex>& verts, std::vector<GLuint>& indices, glm::vec3 base, glm::vec3 tip,
                                float radiusBase, float radiusTip, int segments,
                                glm::vec3 color, float matId) {
    glm::vec3 axis = glm::normalize(tip - base);
    glm::vec3 upHint = std::abs(axis.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 side = glm::normalize(glm::cross(axis, upHint));
    glm::vec3 fwd  = glm::normalize(glm::cross(side, axis));

    for (int i = 0; i < segments; i++) {
        float a0 = (float)i / segments * glm::two_pi<float>();
        float a1 = (float)(i + 1) / segments * glm::two_pi<float>();

        glm::vec3 dir0 = side * std::cos(a0) + fwd * std::sin(a0);
        glm::vec3 dir1 = side * std::cos(a1) + fwd * std::sin(a1);

        glm::vec3 rb0 = base + dir0 * radiusBase;
        glm::vec3 rb1 = base + dir1 * radiusBase;
        glm::vec3 rt0 = tip  + dir0 * radiusTip;
        glm::vec3 rt1 = tip  + dir1 * radiusTip;

        // rb0/rb1 have different normals than rt0/rt1, so only rb0+dir0 and rt1+dir1 can be shared
        // between the two triangles; more would need smooth normals and change the look.
        GLuint base0 = (GLuint)verts.size();
        verts.push_back({rb0, dir0, color, matId});
        verts.push_back({rb1, dir1, color, matId});
        verts.push_back({rt1, dir1, color, matId});
        verts.push_back({rt0, dir0, color, matId});

        indices.push_back(base0 + 0);
        indices.push_back(base0 + 1);
        indices.push_back(base0 + 2);
        indices.push_back(base0 + 0);
        indices.push_back(base0 + 2);
        indices.push_back(base0 + 3);
    }
}

void AddSphere(std::vector<Vertex>& verts, std::vector<GLuint>& indices, glm::vec3 center, float radius,
                              int segments, int rings, glm::vec3 color, float matId) {
    auto sphPoint = [](float u, float v) {
        return glm::vec3(std::sin(v) * std::cos(u), std::cos(v), std::sin(v) * std::sin(u));
    };

    for (int r = 0; r < rings; r++) {
        float v0 = (float)r / rings * glm::pi<float>();
        float v1 = (float)(r + 1) / rings * glm::pi<float>();
        for (int s = 0; s < segments; s++) {
            float u0 = (float)s / segments * glm::two_pi<float>();
            float u1 = (float)(s + 1) / segments * glm::two_pi<float>();

            glm::vec3 p00 = sphPoint(u0, v0), p10 = sphPoint(u1, v0);
            glm::vec3 p01 = sphPoint(u0, v1), p11 = sphPoint(u1, v1);

            // Sphere normals equal the vertex directions (p00/p10/p01/p11 serve as both position
            // and normal), so all 4 corners of a patch can be shared between its two triangles, as
            // in AddQuad.
            GLuint base0 = (GLuint)verts.size();
            verts.push_back({center + p00 * radius, p00, color, matId});
            verts.push_back({center + p10 * radius, p10, color, matId});
            verts.push_back({center + p11 * radius, p11, color, matId});
            verts.push_back({center + p01 * radius, p01, color, matId});

            indices.push_back(base0 + 0);
            indices.push_back(base0 + 1);
            indices.push_back(base0 + 2);
            indices.push_back(base0 + 0);
            indices.push_back(base0 + 2);
            indices.push_back(base0 + 3);
        }
    }
}

static void AddTorchMesh(
    std::vector<Vertex>& verts,
    std::vector<GLuint>& indices,
    glm::vec3 wallBase,
    glm::vec3 normal,
    int torchIndex,
    bool isGuideTorch)
{
    // Torch handle: height and offset must match flamePos in tryAddTorch(), or the flame and the
    // light/particle source diverge. The negative offset (-0.05) buries the base in the wall so the
    // handle grows out of the stone.

    glm::vec3 base =
        wallBase
        + normal * -0.015f
        + glm::vec3(
            0.0f,
            0.50f,
            0.0f
        );

    // Torch tip: the offset must exceed the flame sphere radius (0.065) or half the flame would
    // sink into the wall.
    glm::vec3 tip =
        wallBase
        + normal * 0.14f
        + glm::vec3(
            0.0f,
            0.62f,
            0.0f
        );

    glm::vec3 handleColor(
        0.34f,
        0.25f,
        0.16f
    );

    // Landmark torches (firstGuideTorchIndex): the shader reads vColor.r only as flicker
    // brightness, so g/b are free. The signal (1, 0, 1), which a regular torch never has, selects
    // the blue flame gradient.
    glm::vec3 flameColor = isGuideTorch
        ? glm::vec3(1.0f, 0.0f, 1.0f)    // signal "blue flame" for the shader
        : glm::vec3(1.0f, 0.60f, 0.15f);

    // The handle carries its torch index in matId (1.0..<1.25, below the vMatId > 1.5 flame branch)
    // so pickupWallTorch() can hide exactly this handle.
    const float HANDLE_ID_SCALE = 4096.0f;
    const float handleMatId =
        1.0f
        + static_cast<float>(torchIndex)
          / HANDLE_ID_SCALE;

    AddCylinder(
        verts,
        indices,
        base,
        tip,
        0.038f,
        0.022f,
        isGuideTorch ? 4 : 8, // fewer segments for landmark torches (saves RAM); they are decorative
        handleColor,
        handleMatId
    );

    // Flame. matId is 2.0 .. <2.25, so even at MAX_TORCHES = 1024 it stays below the 2.5 material
    // boundary.

    const float FLAME_ID_SCALE = 4096.0f;

    float flameMatId =
        2.0f
        + static_cast<float>(torchIndex)
          / FLAME_ID_SCALE;

    // The flame sphere is small (radius 0.065) and sits close to the handle tip, so the torch reads
    // as one object: a thin handle with a compact flame.
    AddSphere(
        verts,
        indices,
        tip
        + glm::vec3(
            0.0f,
            0.045f,
            0.0f
        ),
        0.065f,
        isGuideTorch ? 5 : 8, // fewer segments, same reason as the handle
        isGuideTorch ? 3 : 5,
        flameColor,
        flameMatId
    );
}

// A small box with a top and 4 sides (no bottom: it sits on the floor or is covered by other parts
// of the book), one color per face. Building block of the diary prop below.
static void AddBoxNoBottom(std::vector<Vertex>& verts, std::vector<GLuint>& indices,
                            float x0, float x1, float y0, float y1, float z0, float z1,
                            const glm::vec3& color)
{
    AddQuad(verts, indices,
            glm::vec3(x0, y1, z0), glm::vec3(x1, y1, z0),
            glm::vec3(x1, y1, z1), glm::vec3(x0, y1, z1),
            glm::vec3(0, 1, 0), color, 0.0f);
    AddQuad(verts, indices,
            glm::vec3(x0, y0, z0), glm::vec3(x1, y0, z0),
            glm::vec3(x1, y1, z0), glm::vec3(x0, y1, z0),
            glm::vec3(0, 0, -1), color, 0.0f);
    AddQuad(verts, indices,
            glm::vec3(x0, y0, z1), glm::vec3(x1, y0, z1),
            glm::vec3(x1, y1, z1), glm::vec3(x0, y1, z1),
            glm::vec3(0, 0, 1), color, 0.0f);
    AddQuad(verts, indices,
            glm::vec3(x0, y0, z0), glm::vec3(x0, y0, z1),
            glm::vec3(x0, y1, z1), glm::vec3(x0, y1, z0),
            glm::vec3(-1, 0, 0), color, 0.0f);
    AddQuad(verts, indices,
            glm::vec3(x1, y0, z0), glm::vec3(x1, y0, z1),
            glm::vec3(x1, y1, z1), glm::vec3(x1, y1, z0),
            glm::vec3(1, 0, 0), color, 0.0f);
}

static void AddDiaryMesh(std::vector<Vertex>& verts, std::vector<GLuint>& indices,
                          const glm::vec3& diaryPos)
{
    // Diary prop: a small book on the pocket floor, flat colors (matId 0.0), baked once; it stays
    // after reading (m_diariesRead is only a UI flag). Three volumes give the silhouette: solid
    // spine, top/bottom cover plates, and an inset page block between them.
    const float halfW = 0.145f;        // half the book's width along X (including the spine)
    const float halfD = 0.10f;         // half the depth along Z
    const float totalThickness = 0.075f;
    const float coverPlate = 0.010f;   // thickness of each binding "plate" (top/bottom)
    const float spineWidth = 0.022f;   // spine width along X
    const float pageInset = 0.016f;    // how far the pages are inset from the cover edge (not the spine side)

    // The spine runs along the long X side (as in a real book: the spine along the page's long
    // edge, the opening along the short one) and has its own, noticeably darker color, so from
    // above there is a contrasting accent stripe instead of one solid rectangle.
    const glm::vec3 coverColor(0.58f, 0.22f, 0.14f); // cover — faded maroon
    const glm::vec3 spineColor(0.28f, 0.08f, 0.05f); // spine — noticeably darker, a separate accent
    const glm::vec3 pageColor(0.82f, 0.75f, 0.58f);  // inset page edge

    const float xOuter0 = diaryPos.x - halfW;
    const float xOuter1 = diaryPos.x + halfW;
    const float zOuter0 = diaryPos.z - halfD;
    const float zOuter1 = diaryPos.z + halfD;
    const float yBase   = diaryPos.y;
    const float yTop    = yBase + totalThickness;

    AddBoxNoBottom(verts, indices,
                    xOuter0, xOuter1, yBase, yTop, zOuter0, zOuter0 + spineWidth,
                    spineColor);

    // Cover: top and bottom binding plates over the full width and the remaining depth (the spine
    // claimed its strip above), covering the pages and overhanging them on the three open sides;
    // that overhang reads as a closed book's silhouette.
    AddBoxNoBottom(verts, indices,
                    xOuter0, xOuter1, yTop - coverPlate, yTop, zOuter0 + spineWidth, zOuter1,
                    coverColor);
    AddBoxNoBottom(verts, indices,
                    xOuter0, xOuter1, yBase, yBase + coverPlate, zOuter0 + spineWidth, zOuter1,
                    coverColor);

    // Pages are inset from the cover on 3 sides (west/east and the north trim); the spine side has
    // nothing to inset, since it is a solid glued block. They are clamped exactly between the
    // plates in height.
    AddBoxNoBottom(verts, indices,
                    xOuter0 + pageInset, xOuter1 - pageInset,
                    yBase + coverPlate, yTop - coverPlate,
                    zOuter0 + spineWidth, zOuter1 - pageInset,
                    pageColor);
}

// Chamfered corners (WallShapes.h): such a cell is built separately and skipped by the greedy
// passes. It emits the regular side faces (two shortened to the chamfer points) plus the diagonal
// wedge face, and a floor triangle over the wedge, which the floor pass (map[] == 2 cells) does not
// cover; otherwise the player would see a hole.
static void AddChamferedWallCell(
    std::vector<Vertex>& verts, std::vector<GLuint>& indices,
    int x, int z, WallShapes::CornerCut cut, float chamferSize,
    const std::function<bool(int, int)>& isWall,
    const glm::vec3& wallColor, const glm::vec3& floorColor, float y0, float y1)
{
    using WallShapes::CornerCut;

    WallShapes::ChamferPoints cp;
    if (!WallShapes::GetChamferPoints(cut, chamferSize, cp)) {
        return; // cut == None shouldn't reach here (see the caller)
    }

    const float xf = (float)x, zf = (float)z, z1 = zf + 1.0f, x1 = xf + 1.0f;

    // Chamfer points in world coordinates (see WallShapes.h for onEdgeCcwFrom/onEdgeCcwTo: which
    // point lies on which original edge for each cut).
    const glm::vec3 pFrom(xf + cp.onEdgeCcwFrom.x, 0.0f, zf + cp.onEdgeCcwFrom.y);
    const glm::vec3 pTo(xf + cp.onEdgeCcwTo.x,   0.0f, zf + cp.onEdgeCcwTo.y);

    if (isWall(x, z) && !isWall(x, z - 1)) {
        const float xStart = (cut == CornerCut::SW) ? pTo.x   : xf;
        const float xEnd   = (cut == CornerCut::SE) ? pFrom.x : x1;
        if (xEnd > xStart) {
            AddQuad(verts, indices,
                glm::vec3(xStart, y0, zf), glm::vec3(xEnd, y0, zf),
                glm::vec3(xEnd, y1, zf), glm::vec3(xStart, y1, zf),
                glm::vec3(0, 0, -1), wallColor);
        }
    }
    if (isWall(x, z) && !isWall(x, z + 1)) {
        const float xStart = (cut == CornerCut::NW) ? pFrom.x : xf;
        const float xEnd   = (cut == CornerCut::NE) ? pTo.x   : x1;
        if (xEnd > xStart) {
            AddQuad(verts, indices,
                glm::vec3(xStart, y0, z1), glm::vec3(xEnd, y0, z1),
                glm::vec3(xEnd, y1, z1), glm::vec3(xStart, y1, z1),
                glm::vec3(0, 0, 1), wallColor);
        }
    }
    if (isWall(x, z) && !isWall(x - 1, z)) {
        const float zStart = (cut == CornerCut::SW) ? pFrom.z : zf;
        const float zEnd   = (cut == CornerCut::NW) ? pTo.z   : z1;
        if (zEnd > zStart) {
            AddQuad(verts, indices,
                glm::vec3(xf, y0, zStart), glm::vec3(xf, y0, zEnd),
                glm::vec3(xf, y1, zEnd), glm::vec3(xf, y1, zStart),
                glm::vec3(-1, 0, 0), wallColor);
        }
    }
    if (isWall(x, z) && !isWall(x + 1, z)) {
        const float zStart = (cut == CornerCut::SE) ? pTo.z   : zf;
        const float zEnd   = (cut == CornerCut::NE) ? pFrom.z : z1;
        if (zEnd > zStart) {
            AddQuad(verts, indices,
                glm::vec3(x1, y0, zStart), glm::vec3(x1, y0, zEnd),
                glm::vec3(x1, y1, zEnd), glm::vec3(x1, y1, zStart),
                glm::vec3(1, 0, 0), wallColor);
        }
    }

    glm::vec3 diagNormal(0.0f);
    switch (cut) {
        case CornerCut::SW: diagNormal = glm::vec3(-1, 0, -1); break;
        case CornerCut::SE: diagNormal = glm::vec3(1, 0, -1);  break;
        case CornerCut::NE: diagNormal = glm::vec3(1, 0, 1);   break;
        case CornerCut::NW: diagNormal = glm::vec3(-1, 0, 1);  break;
        default: break;
    }
    diagNormal = glm::normalize(diagNormal);

    AddQuad(verts, indices,
        glm::vec3(pFrom.x, y0, pFrom.z), glm::vec3(pTo.x, y0, pTo.z),
        glm::vec3(pTo.x, y1, pTo.z), glm::vec3(pFrom.x, y1, pFrom.z),
        diagNormal, wallColor);

    // Floor patch for the wedge: the corner is repeated as the 3rd and 4th vertex so AddQuad()
    // degenerates to a single triangle.
    glm::vec3 corner(0.0f);
    switch (cut) {
        case CornerCut::SW: corner = glm::vec3(xf, 0.0f, zf);       break;
        case CornerCut::SE: corner = glm::vec3(x1, 0.0f, zf);       break;
        case CornerCut::NE: corner = glm::vec3(x1, 0.0f, z1);       break;
        case CornerCut::NW: corner = glm::vec3(xf, 0.0f, z1);       break;
        default: break;
    }
    AddQuad(verts, indices,
        pFrom, pTo, corner, corner,
        glm::vec3(0, 1, 0), floorColor);
}

// Free-standing columns (see Columns.h): a plain cylinder with radiusBase == radiusTip (straight,
// not tapering). A column cell is already floor in map[] by this point, so the regular floor pass
// builds the floor under it; only the cylinder is emitted here.
static void AddColumnMesh(
    std::vector<Vertex>& verts, std::vector<GLuint>& indices,
    const glm::vec2& centerXZ, const glm::vec3& wallColor,
    float y0, float y1)
{
    const glm::vec3 base(centerXZ.x, y0, centerXZ.y);
    const glm::vec3 tip(centerXZ.x, y1, centerXZ.y);
    AddCylinder(verts, indices, base, tip,
                Columns::kColumnRadius, Columns::kColumnRadius,
                Columns::kColumnSegments, wallColor, 0.0f);
}

static void BuildFloorAndWallsGreedy(
    int mapW, int mapH, const std::vector<int>& map,
    const std::function<bool(int, int)>& isWall,
    const std::function<bool(int, int)>& isFloor,
    const std::vector<WallShapes::CornerCut>& cornerCuts,
    const std::vector<float>& chamferSizes,
    const std::vector<glm::vec3>& wallColors,
    const std::vector<glm::vec3>& floorColors,
    std::vector<std::vector<Vertex>>& chunkMainVerts,
    std::vector<std::vector<GLuint>>& chunkMainIndices,
    int chunksX)
{
    // Per-cell wall/floor color, already computed by the caller (which blends neighboring zones). A
    // cell with no data (empty or too short array) falls back to palette 0, so behavior without
    // zoning is unchanged.
    glm::vec3 fallbackWallColor, fallbackFloorColor;
    SceneGeometry::GetZonePalette(0, fallbackWallColor, fallbackFloorColor);
    auto wallColorAt = [&](int x, int z) {
        const size_t idx = (size_t)z * mapW + x;
        return idx < wallColors.size() ? wallColors[idx] : fallbackWallColor;
    };
    auto floorColorAt = [&](int x, int z) {
        const size_t idx = (size_t)z * mapW + x;
        return idx < floorColors.size() ? floorColors[idx] : fallbackFloorColor;
    };
    auto chunkIdx = [&](int cx, int cz) {
        return (cz / SceneGeometry::kChunkSize) * chunksX + (cx / SceneGeometry::kChunkSize);
    };

    // Reserve each per-chunk bucket up front (a rough upper bound); a load-time startup win only.
    {
        const size_t cellsPerChunk = (size_t)SceneGeometry::kChunkSize * (size_t)SceneGeometry::kChunkSize;
        const size_t estVertsPerChunk = cellsPerChunk * 4;   // ~4 verts/quad face, upper bound
        const size_t estIndicesPerChunk = cellsPerChunk * 6; // 6 indices/quad (2 tris), upper bound
        for (std::vector<Vertex>& v : chunkMainVerts)
            v.reserve(estVertsPerChunk);
        for (std::vector<GLuint>& idxVec : chunkMainIndices)
            idxVec.reserve(estIndicesPerChunk);
    }

    const float y0 = 0.0f;
    const float y1 = kWallHeight;

    // Chamfered corners (WallShapes): a separate whole-cell pass. It runs before the greedy passes
    // below, which read cornerCuts[...] to skip these cells.
    for (int z = 0; z < mapH; z++) {
        for (int x = 0; x < mapW; x++) {
            const WallShapes::CornerCut cut = cornerCuts[(size_t)z * mapW + x];
            if (cut == WallShapes::CornerCut::None) continue;
            const int idx = chunkIdx(x, z);
            const float chamferSize = ((size_t)z * mapW + x) < chamferSizes.size()
                ? chamferSizes[(size_t)z * mapW + x]
                : WallShapes::kChamferSize;
            glm::vec3 cellWallColor = wallColorAt(x, z);
            glm::vec3 cellFloorColor = floorColorAt(x, z);
            AddChamferedWallCell(
                chunkMainVerts[idx], chunkMainIndices[idx],
                x, z, cut, chamferSize, isWall, cellWallColor, cellFloorColor, y0, y1);
        }
    }

    for (int z = 0; z < mapH; z++) {
        int x = 0;
        while (x < mapW) {
            if (map[z * mapW + x] != 2) { x++; continue; }

            const int runStart = x;
            const int curChunkX = x / SceneGeometry::kChunkSize;
            const glm::vec3 curFloorColor = floorColorAt(x, z);
            while (x < mapW &&
                   map[z * mapW + x] == 2 &&
                   floorColorAt(x, z) == curFloorColor &&
                   (x / SceneGeometry::kChunkSize) == curChunkX) {
                x++;
            }
            const int runEnd = x;

            const int idx = chunkIdx(runStart, z);
            AddQuad(
                chunkMainVerts[idx], chunkMainIndices[idx],
                glm::vec3((float)runStart, 0.0f, (float)z),
                glm::vec3((float)runEnd,   0.0f, (float)z),
                glm::vec3((float)runEnd,   0.0f, (float)z + 1.0f),
                glm::vec3((float)runStart, 0.0f, (float)z + 1.0f),
                glm::vec3(0, 1, 0),
                curFloorColor
            );
        }
    }

    for (int z = 0; z < mapH; z++) {
        int x = 0;
        while (x < mapW) {
            // Chamfered cells were built in the separate pass above; here they only break a run,
            // neither joining it nor getting their own quad.
            if (!(isWall(x, z) && !isWall(x, z - 1)) ||
                cornerCuts[(size_t)z * mapW + x] != WallShapes::CornerCut::None) {
                x++; continue;
            }

            const int runStart = x;
            const int curChunkX = x / SceneGeometry::kChunkSize;
            const glm::vec3 curWallColor = wallColorAt(x, z);
            while (x < mapW &&
                   isWall(x, z) && !isWall(x, z - 1) &&
                   cornerCuts[(size_t)z * mapW + x] == WallShapes::CornerCut::None &&
                   wallColorAt(x, z) == curWallColor &&
                   (x / SceneGeometry::kChunkSize) == curChunkX) {
                x++;
            }
            const int runEnd = x;

            const int idx = chunkIdx(runStart, z);
            AddQuad(
                chunkMainVerts[idx], chunkMainIndices[idx],
                glm::vec3((float)runStart, y0, (float)z),
                glm::vec3((float)runEnd,   y0, (float)z),
                glm::vec3((float)runEnd,   y1, (float)z),
                glm::vec3((float)runStart, y1, (float)z),
                glm::vec3(0, 0, -1),
                curWallColor
            );
        }
    }

    for (int z = 0; z < mapH; z++) {
        int x = 0;
        while (x < mapW) {
            if (!(isWall(x, z) && !isWall(x, z + 1)) ||
                cornerCuts[(size_t)z * mapW + x] != WallShapes::CornerCut::None) {
                x++; continue;
            }

            const int runStart = x;
            const int curChunkX = x / SceneGeometry::kChunkSize;
            const glm::vec3 curWallColor = wallColorAt(x, z);
            while (x < mapW &&
                   isWall(x, z) && !isWall(x, z + 1) &&
                   cornerCuts[(size_t)z * mapW + x] == WallShapes::CornerCut::None &&
                   wallColorAt(x, z) == curWallColor &&
                   (x / SceneGeometry::kChunkSize) == curChunkX) {
                x++;
            }
            const int runEnd = x;
            const float z1 = (float)z + 1.0f;

            const int idx = chunkIdx(runStart, z);
            AddQuad(
                chunkMainVerts[idx], chunkMainIndices[idx],
                glm::vec3((float)runStart, y0, z1),
                glm::vec3((float)runEnd,   y0, z1),
                glm::vec3((float)runEnd,   y1, z1),
                glm::vec3((float)runStart, y1, z1),
                glm::vec3(0, 0, 1),
                curWallColor
            );
        }
    }

    for (int x = 0; x < mapW; x++) {
        int z = 0;
        while (z < mapH) {
            if (!(isWall(x, z) && !isWall(x - 1, z)) ||
                cornerCuts[(size_t)z * mapW + x] != WallShapes::CornerCut::None) {
                z++; continue;
            }

            const int runStart = z;
            const int curChunkZ = z / SceneGeometry::kChunkSize;
            const glm::vec3 curWallColor = wallColorAt(x, z);
            while (z < mapH &&
                   isWall(x, z) && !isWall(x - 1, z) &&
                   cornerCuts[(size_t)z * mapW + x] == WallShapes::CornerCut::None &&
                   wallColorAt(x, z) == curWallColor &&
                   (z / SceneGeometry::kChunkSize) == curChunkZ) {
                z++;
            }
            const int runEnd = z;
            const float xf = (float)x;

            const int idx = chunkIdx(x, runStart);
            AddQuad(
                chunkMainVerts[idx], chunkMainIndices[idx],
                glm::vec3(xf, y0, (float)runStart),
                glm::vec3(xf, y0, (float)runEnd),
                glm::vec3(xf, y1, (float)runEnd),
                glm::vec3(xf, y1, (float)runStart),
                glm::vec3(-1, 0, 0),
                curWallColor
            );
        }
    }

    for (int x = 0; x < mapW; x++) {
        int z = 0;
        while (z < mapH) {
            if (!(isWall(x, z) && !isWall(x + 1, z)) ||
                cornerCuts[(size_t)z * mapW + x] != WallShapes::CornerCut::None) {
                z++; continue;
            }

            const int runStart = z;
            const int curChunkZ = z / SceneGeometry::kChunkSize;
            const glm::vec3 curWallColor = wallColorAt(x, z);
            while (z < mapH &&
                   isWall(x, z) && !isWall(x + 1, z) &&
                   cornerCuts[(size_t)z * mapW + x] == WallShapes::CornerCut::None &&
                   wallColorAt(x, z) == curWallColor &&
                   (z / SceneGeometry::kChunkSize) == curChunkZ) {
                z++;
            }
            const int runEnd = z;
            const float x1 = (float)x + 1.0f;

            const int idx = chunkIdx(x, runStart);
            AddQuad(
                chunkMainVerts[idx], chunkMainIndices[idx],
                glm::vec3(x1, y0, (float)runStart),
                glm::vec3(x1, y0, (float)runEnd),
                glm::vec3(x1, y1, (float)runEnd),
                glm::vec3(x1, y1, (float)runStart),
                glm::vec3(1, 0, 0),
                curWallColor
            );
        }
    }
}

void SceneGeometry::build(int mapW, int mapH, const std::vector<int>& map,
                          const glm::vec3& winButtonPos,
                          const std::vector<glm::vec3>& torchWallBase,
                          const std::vector<glm::vec3>& torchNormal,
                          const std::vector<glm::vec3>& torchFlamePos,
                          const std::vector<WallShapes::CornerCut>& cornerCuts,
                          const std::vector<glm::vec2>& columnCentersXZ,
                          const std::vector<float>& chamferSizes,
                          const std::vector<glm::vec3>& wallColors,
                          const std::vector<glm::vec3>& floorColors,
                          const std::vector<glm::vec3>& diaryPositions,
                          int firstGuideTorchIndex)
{
    auto isWall = [&](int x, int z) {
        if (x < 0 || x >= mapW || z < 0 || z >= mapH) return true;
        return map[z * mapW + x] == 1;
    };
    auto isFloor = [&](int x, int z) {
        if (x < 0 || x >= mapW || z < 0 || z >= mapH) return false;
        return map[z * mapW + x] == 2;
    };

    // Chunked geometry: bucketed per chunk while generated so render() can skip chunks outside the
    // frustum/render distance; the GPU buffers still hold all chunks contiguously.
    const int chunksX = (mapW + kChunkSize - 1) / kChunkSize;
    const int chunksZ = (mapH + kChunkSize - 1) / kChunkSize;
    const int numChunks = chunksX * chunksZ;

    m_chunksX = chunksX;

    auto chunkIndexForCell = [&](int cx, int cz) {
        cx = std::clamp(cx, 0, mapW - 1);
        cz = std::clamp(cz, 0, mapH - 1);
        return (cz / kChunkSize) * chunksX + (cx / kChunkSize);
    };

    std::vector<std::vector<Vertex>> chunkMainVerts(numChunks);
    std::vector<std::vector<GLuint>> chunkMainIndices(numChunks);
    std::vector<std::vector<Vertex>> chunkParticleVerts(numChunks);

    BuildFloorAndWallsGreedy(mapW, mapH, map, isWall, isFloor, cornerCuts, chamferSizes, wallColors, floorColors, chunkMainVerts, chunkMainIndices, chunksX);

    for (size_t i = 0;
         i < torchWallBase.size();
         ++i)
    {
        // The torch mesh and particles belong to the chunk of the torch's wall cell.
        // torchWallBase[i] lies on the wall/floor boundary, so floor(x)/floor(z) is wrong for half
        // the directions; stepping back by normal * 0.5 recovers the wall cell (otherwise culling
        // could hide a torch while its wall is in view).
        const glm::vec3& torchN = torchNormal[i];
        const int torchChunk = chunkIndexForCell(
            (int)std::floor(torchWallBase[i].x - torchN.x * 0.5f),
            (int)std::floor(torchWallBase[i].z - torchN.z * 0.5f)
        );
        std::vector<Vertex>& verts = chunkMainVerts[torchChunk];
        std::vector<GLuint>& indices = chunkMainIndices[torchChunk];
        std::vector<Vertex>& particleVerts = chunkParticleVerts[torchChunk];

        AddTorchMesh(
            verts,
            indices,
            torchWallBase[i],
            torchNormal[i],
            (int)i,
            firstGuideTorchIndex >= 0 && (int)i >= firstGuideTorchIndex
        );

        // Flickering ember particles are skipped for landmark torches (saves RAM): a small
        // decorative accent whose three spark points per torch add up across dozens of torches, and
        // a landmark is already visible from its blue flame.
        if (!(firstGuideTorchIndex >= 0 && (int)i >= firstGuideTorchIndex))
        {

        const glm::vec3 origin =
            torchFlamePos[i]
            + glm::vec3(
                0.0f,
                0.025f,
                0.0f
            );

        const int torchIndex =
            static_cast<int>(i);

        const float PARTICLE_ID_SCALE = 16384.0f;

        const float particleMatId0 =
            3.0f
            +
            (
                static_cast<float>(
                    torchIndex * 10 + 0
                )
                /
                PARTICLE_ID_SCALE
            );

        particleVerts.push_back(
            {
                origin,

                glm::vec3(
                    -0.030f,
                    0.40f,
                    0.010f
                ),

                glm::vec3(
                    1.0f,
                    0.48f,
                    0.08f
                ),

                particleMatId0
            }
        );

        const float particleMatId1 =
            3.0f
            +
            (
                static_cast<float>(
                    torchIndex * 10 + 1
                )
                /
                PARTICLE_ID_SCALE
            );

        particleVerts.push_back(
            {
                origin,

                glm::vec3(
                    0.020f,
                    0.48f,
                    -0.025f
                ),

                glm::vec3(
                    1.0f,
                    0.58f,
                    0.10f
                ),

                particleMatId1
            }
        );

        const float particleMatId2 =
            3.0f
            +
            (
                static_cast<float>(
                    torchIndex * 10 + 2
                )
                /
                PARTICLE_ID_SCALE
            );

        particleVerts.push_back(
            {
                origin,

                glm::vec3(
                    -0.045f,
                    0.43f,
                    -0.020f
                ),

                glm::vec3(
                    1.0f,
                    0.52f,
                    0.07f
                ),

                particleMatId2
            }
        );
        }
    }

    // The win pedestal is not baked: it animates, and baked chunk geometry cannot be moved or
    // removed. It is a dynamic mesh (m_winMonumentVao, AddDissolvingMonument()/AddSpinningTorus()
    // in DungeonScene.cpp).

    // Diaries: one prop per pocket, assigned to a chunk like the torches: find the cell via floor()
    // of the world coordinates, then the chunk through it, so chunk culling does not drop a diary
    // the camera is looking straight at.
    for (size_t i = 0; i < diaryPositions.size(); ++i) {
        const glm::vec3& diaryPos = diaryPositions[i];
        const int diaryChunk = chunkIndexForCell(
            (int)std::floor(diaryPos.x),
            (int)std::floor(diaryPos.z)
        );
        AddDiaryMesh(chunkMainVerts[diaryChunk], chunkMainIndices[diaryChunk], diaryPos);

        // A couple of pale-gold "moth" particles above the book reveal the diary from afar. They
        // reuse the torch particle system with a muted color and id torchWallBase.size() + i, so
        // they do not share a flicker phase with a torch.
        std::vector<Vertex>& diaryParticleVerts = chunkParticleVerts[diaryChunk];
        const glm::vec3 particleOrigin = diaryPos + glm::vec3(0.0f, 0.16f, 0.0f);
        const int diaryParticleId = (int)torchWallBase.size() + (int)i;
        const glm::vec3 diaryGlowColor(0.95f, 0.88f, 0.62f);
        const float PARTICLE_ID_SCALE = 16384.0f;

        const float slot0MatId = 3.0f + (float)(diaryParticleId * 10 + 0) / PARTICLE_ID_SCALE;
        diaryParticleVerts.push_back({
            particleOrigin, glm::vec3(-0.020f, 0.30f, 0.015f), diaryGlowColor, slot0MatId
        });

        const float slot1MatId = 3.0f + (float)(diaryParticleId * 10 + 1) / PARTICLE_ID_SCALE;
        diaryParticleVerts.push_back({
            particleOrigin, glm::vec3(0.018f, 0.34f, -0.020f), diaryGlowColor, slot1MatId
        });
    }

    // Free-standing columns (Columns.h): positions are world XZ cell centers and the cell is
    // already floor, so only the cylinder is added.
    for (const glm::vec2& col : columnCentersXZ) {
        const int colCellX = (int)std::floor(col.x);
        const int colCellZ = (int)std::floor(col.y);
        const int colChunk = chunkIndexForCell(colCellX, colCellZ);
        const size_t colIdx = (size_t)colCellZ * mapW + colCellX;
        glm::vec3 fallbackWallColor, fallbackFloorColorUnused;
        GetZonePalette(0, fallbackWallColor, fallbackFloorColorUnused);
        const glm::vec3 colWallColor = colIdx < wallColors.size() ? wallColors[colIdx] : fallbackWallColor;
        AddColumnMesh(chunkMainVerts[colChunk], chunkMainIndices[colChunk], col, colWallColor, 0.0f, kWallHeight);
    }

    // Flatten the per-chunk buckets into contiguous buffers and build the per-chunk metadata (draw
    // ranges + AABB) that render() uses for frustum/distance culling.

    std::vector<Vertex> verts;
    std::vector<GLuint> indices;
    std::vector<Vertex> particleVerts;

    m_chunks.clear();
    m_chunks.resize(numChunks);

    for (int cz = 0; cz < chunksZ; ++cz)
    {
        for (int cxi = 0; cxi < chunksX; ++cxi)
        {
            const int idx = cz * chunksX + cxi;
            GeoChunk& chunk = m_chunks[idx];

            const std::vector<Vertex>& mv = chunkMainVerts[idx];
            const std::vector<GLuint>& mi = chunkMainIndices[idx];
            const std::vector<Vertex>& pv = chunkParticleVerts[idx];

            // Indices in mi are local to mv (they start at 0). When mv's vertices are appended to
            // the global verts array they land at vertexBase.., so each local index needs
            // vertexBase added.
            const GLuint vertexBase = (GLuint)verts.size();
            verts.insert(verts.end(), mv.begin(), mv.end());

            chunk.mainIndexFirst = (GLint)indices.size();
            chunk.mainIndexCount = (GLsizei)mi.size();
            indices.reserve(indices.size() + mi.size());
            for (GLuint localIndex : mi)
                indices.push_back(localIndex + vertexBase);

            chunk.particleFirst = (GLint)particleVerts.size();
            chunk.particleCount = (GLsizei)pv.size();
            particleVerts.insert(particleVerts.end(), pv.begin(), pv.end());

            // Cell-grid-based AABB with a small margin for torch/flame meshes that poke past a cell
            // edge: cheap and always conservative (never smaller than the real geometry), which is
            // all frustum culling needs.
            const float margin = 0.5f;
            const int cellX0 = cxi * kChunkSize;
            const int cellZ0 = cz * kChunkSize;
            const int cellX1 = std::min(mapW, cellX0 + kChunkSize);
            const int cellZ1 = std::min(mapH, cellZ0 + kChunkSize);

            chunk.aabbMin = glm::vec3((float)cellX0 - margin, -margin, (float)cellZ0 - margin);
            chunk.aabbMax = glm::vec3((float)cellX1 + margin, kWallHeight + margin, (float)cellZ1 + margin);
        }
    }

    m_vertexCount =
        (int)verts.size();

    m_indexCount =
        (int)indices.size();

    glGenVertexArrays(
        1,
        &m_vao
    );

    glGenBuffers(
        1,
        &m_vbo
    );

    glGenBuffers(
        1,
        &m_ebo
    );

    glBindVertexArray(
        m_vao
    );

    glBindBuffer(
        GL_ARRAY_BUFFER,
        m_vbo
    );

    glBufferData(
        GL_ARRAY_BUFFER,
        verts.size()
        * sizeof(Vertex),
        verts.data(),
        GL_STATIC_DRAW
    );

    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            pos
        )
    );

    glEnableVertexAttribArray(1);

    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            normal
        )
    );

    glEnableVertexAttribArray(2);

    glVertexAttribPointer(
        2,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            color
        )
    );

    glEnableVertexAttribArray(3);

    glVertexAttribPointer(
        3,
        1,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            matId
        )
    );

    // The EBO binding is part of the VAO state, so it must be bound while m_vao is still bound
    // (before the glBindVertexArray(0) below), like the VBO/attribute setup above.
    glBindBuffer(
        GL_ELEMENT_ARRAY_BUFFER,
        m_ebo
    );

    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        indices.size()
        * sizeof(GLuint),
        indices.data(),
        GL_STATIC_DRAW
    );

    glBindVertexArray(0);

    m_particleVertexCount =
        (int)particleVerts.size();

    glGenVertexArrays(
        1,
        &m_particleVao
    );

    glGenBuffers(
        1,
        &m_particleVbo
    );

    glBindVertexArray(
        m_particleVao
    );

    glBindBuffer(
        GL_ARRAY_BUFFER,
        m_particleVbo
    );

    glBufferData(
        GL_ARRAY_BUFFER,
        particleVerts.size()
        * sizeof(Vertex),
        particleVerts.data(),
        GL_STATIC_DRAW
    );

    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            pos
        )
    );

    glEnableVertexAttribArray(1);

    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            normal
        )
    );

    glEnableVertexAttribArray(2);

    glVertexAttribPointer(
        2,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            color
        )
    );

    glEnableVertexAttribArray(3);

    glVertexAttribPointer(
        3,
        1,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            matId
        )
    );

    glBindVertexArray(0);
}

// Precomputed chunk PVS: flood-fill from each chunk's footprint through floor cells only, one bit
// per reachable chunk in a 64-bit mask. Built once after geometry. Conservative: it only rules out
// provably unreachable chunks, and render() combines it with frustum/distance culling.

void SceneGeometry::buildPVS(int mapW, int mapH, const std::function<bool(int, int)>& isWall)
{
    const size_t numChunks = m_chunks.size();
    m_chunkPvsMask.assign(numChunks, 0ull);

    // The mask is one uint64_t, so at most 64 chunks (the 128x128 maze with kChunkSize 16 gives
    // exactly 64). Beyond that the prefilter is disabled and render() falls back to frustum +
    // distance culling.
    if (numChunks == 0 || numChunks > 64 || m_chunksX <= 0 || mapW <= 0 || mapH <= 0)
    {
        m_chunkPvsEnabled = false;
        return;
    }

    m_chunkPvsEnabled = true;

    std::vector<unsigned char> visitedCell((size_t)mapW * (size_t)mapH, 0);
    std::vector<int> stack;
    stack.reserve(visitedCell.size());

    const int dx[4] = { 1, -1, 0, 0 };
    const int dz[4] = { 0, 0, 1, -1 };

    for (size_t srcChunk = 0; srcChunk < numChunks; ++srcChunk)
    {
        std::fill(visitedCell.begin(), visitedCell.end(), (unsigned char)0);
        stack.clear();

        const int cxi = (int)srcChunk % m_chunksX;
        const int cz = (int)srcChunk / m_chunksX;
        const int cellX0 = cxi * kChunkSize;
        const int cellZ0 = cz * kChunkSize;
        const int cellX1 = std::min(mapW, cellX0 + kChunkSize);
        const int cellZ1 = std::min(mapH, cellZ0 + kChunkSize);

        uint64_t mask = (1ull << srcChunk); // a chunk is always potentially visible from itself

        for (int z = cellZ0; z < cellZ1; ++z)
        {
            for (int x = cellX0; x < cellX1; ++x)
            {
                if (isWall(x, z)) continue;
                const int cell = z * mapW + x;
                if (!visitedCell[(size_t)cell])
                {
                    visitedCell[(size_t)cell] = 1;
                    stack.push_back(cell);
                }
            }
        }

        while (!stack.empty())
        {
            const int cell = stack.back();
            stack.pop_back();

            const int x = cell % mapW;
            const int z = cell / mapW;

            const int touchedChunk = (z / kChunkSize) * m_chunksX + (x / kChunkSize);
            mask |= (1ull << touchedChunk);

            for (int d = 0; d < 4; ++d)
            {
                const int nx = x + dx[d];
                const int nz = z + dz[d];
                if (nx < 0 || nz < 0 || nx >= mapW || nz >= mapH) continue;
                if (isWall(nx, nz)) continue;

                const int ncell = nz * mapW + nx;
                if (visitedCell[(size_t)ncell]) continue;

                visitedCell[(size_t)ncell] = 1;
                stack.push_back(ncell);
            }
        }

        m_chunkPvsMask[srcChunk] = mask;
    }
}

void SceneGeometry::destroy()
{
    if (m_particleVbo) {
        glDeleteBuffers(1, &m_particleVbo);
        m_particleVbo = 0;
    }
    if (m_particleVao) {
        glDeleteVertexArrays(1, &m_particleVao);
        m_particleVao = 0;
    }
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_ebo) {
        glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    m_particleVertexCount = 0;
    m_vertexCount = 0;
    m_indexCount = 0;
    m_chunks.clear();
    m_chunksX = 0;
    m_chunkPvsMask.clear();
    m_chunkPvsEnabled = false;
}
