#include "DungeonScene.h"
#include "render/ShaderLoader.h"
#include "render/ShaderProgram.h"
#include "WallTexture.h"
#include "MapGenerator.h"
#include "ExitDoor.h"
#include "audio/UiAudio.h"
#include "LineOfSight.h"
#include "GridPathfinding.h"
#include "save/SaveSystem.h"
#include "ui/MainMenu.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <cstdio>
#include <random>
#include <algorithm>
#include <array>
#include <limits>
#include <filesystem>

// The main pseudo-engine of this game
// Good luck

const int DungeonScene::MAX_TORCHES;
const int DungeonScene::MAX_ACTIVE_TORCHES;

GLuint DungeonScene::compileShader(GLenum type, const char* src) {
    return ShaderProgram::CompileShader(type, src, "DungeonScene");
}

GLuint DungeonScene::linkProgram(GLuint vs, GLuint fs) {
    return ShaderProgram::LinkProgram(vs, fs, "DungeonScene");
}

bool DungeonScene::isWall(int x, int z) const {
    if (x < 0 || x >= m_mapW || z < 0 || z >= m_mapH) return true;
    return m_map[z * m_mapW + x] == 1;
}

bool DungeonScene::isFloor(int x, int z) const {
    if (x < 0 || x >= m_mapW || z < 0 || z >= m_mapH) return false;
    return m_map[z * m_mapW + x] == 2;
}

WallShapes::CornerCut DungeonScene::wallCornerCut(int x, int z) const {
    if (x < 0 || x >= m_mapW || z < 0 || z >= m_mapH) return WallShapes::CornerCut::None;
    // Empty until generateMap() fills it, so check the size before indexing
    if (m_wallCornerCuts.size() != (size_t)m_mapW * (size_t)m_mapH) return WallShapes::CornerCut::None;
    return m_wallCornerCuts[(size_t)z * m_mapW + x];
}

float DungeonScene::wallChamferSize(int x, int z) const {
    if (x < 0 || x >= m_mapW || z < 0 || z >= m_mapH) return WallShapes::kChamferSize;
    if (m_chamferSizes.size() != (size_t)m_mapW * (size_t)m_mapH) return WallShapes::kChamferSize;
    return m_chamferSizes[(size_t)z * m_mapW + x];
}

void DungeonScene::setCompassUiFont(GLuint texture, int glyphCount)
{
    m_compass.setUiFont(texture, glyphCount);
}

static void BlendedZoneColor(const Zoning::ZoneGrid& zoneGrid, int cellX, int cellZ,
                              glm::vec3& outWallColor, glm::vec3& outFloorColor);

void DungeonScene::generateMap(unsigned int seed)
{
    MapGenerator::GenerateResult result = MapGenerator::Generate(seed);

    m_mapW = result.mapW;
    m_mapH = result.mapH;
    m_map = std::move(result.map);

    // Zones first: every generator below reads its per-cell probabilities from m_zoneGrid
    m_zoneGrid = Zoning::BuildZoneGrid(m_mapW, m_mapH, seed);

    // Grid passes run in a fixed order:
    // widening changes which cells are open, and columns and chamfers must see the widened grid
    m_corridorWidened = CorridorWidth::ApplyWidening(
        m_mapW, m_mapH, m_map, seed,
        [this](int x, int z) { return Zoning::StyleAt(m_zoneGrid, x, z).widenProbability; });

    // Before BuildCornerCuts(): column cells become floor, and chamfer detection must see that
    m_columnCentersXZ = Columns::BuildColumns(
        m_mapW, m_mapH, m_map, seed,
        [this](int x, int z) { return Zoning::StyleAt(m_zoneGrid, x, z).columnProbability; });

    // Chamfers are derived from the seed
    // saves do not store them. Diagonal chains are forced to probability 1 so the whole chain reads as one diagonal wall (wha?)
    m_diagonalChainMask = DiagonalCorridors::DetectChains(m_mapW, m_mapH, m_map);
    m_wallCornerCuts = WallShapes::BuildCornerCuts(
        m_mapW, m_mapH, m_map, seed,
        [this](int x, int z) {
            const size_t idx = (size_t)z * m_mapW + x;
            if (idx < m_diagonalChainMask.size() && m_diagonalChainMask[idx]) return 1.0f;
            return Zoning::StyleAt(m_zoneGrid, x, z).chamferProbability;
        });

    // Chains use kChamferSizeChain so the diagonal reads as connected;
    // other chamfers vary in size for silhouette variety
    m_chamferSizes.assign((size_t)m_mapW * (size_t)m_mapH, WallShapes::kChamferSize);
    for (int z = 0; z < m_mapH; ++z) {
        for (int x = 0; x < m_mapW; ++x) {
            const size_t idx = (size_t)z * m_mapW + x;
            if (m_wallCornerCuts[idx] == WallShapes::CornerCut::None) continue;
            m_chamferSizes[idx] = WallShapes::ComputeVariedChamferSize(seed, x, z);
        }
    }
    for (size_t i = 0; i < m_diagonalChainMask.size() && i < m_chamferSizes.size(); ++i) {
        if (m_diagonalChainMask[i]) m_chamferSizes[i] = WallShapes::kChamferSizeChain;
    }

    // Per-cell palette blended across zone borders. The spawn area always uses palette 0.
    m_paletteWallColors.assign((size_t)m_mapW * (size_t)m_mapH, glm::vec3(0.0f));
    m_paletteFloorColors.assign((size_t)m_mapW * (size_t)m_mapH, glm::vec3(0.0f));
    for (int z = 0; z < m_mapH; ++z) {
        for (int x = 0; x < m_mapW; ++x) {
            BlendedZoneColor(m_zoneGrid, x, z,
                              m_paletteWallColors[(size_t)z * m_mapW + x],
                              m_paletteFloorColors[(size_t)z * m_mapW + x]);
        }
    }
    {
        // must match the spawn position in generateFreshMapAndPlayer()
        constexpr float kSpawnX = 6.5f, kSpawnZ = 6.5f;
        constexpr float kSpawnPaletteRadius = 9.0f; // covers the starting safe zone with margin
        glm::vec3 spawnWallColor, spawnFloorColor;
        SceneGeometry::GetZonePalette(0, spawnWallColor, spawnFloorColor);
        for (int z = 0; z < m_mapH; ++z) {
            for (int x = 0; x < m_mapW; ++x) {
                const float dx = ((float)x + 0.5f) - kSpawnX;
                const float dz = ((float)z + 0.5f) - kSpawnZ;
                if (dx * dx + dz * dz <= kSpawnPaletteRadius * kSpawnPaletteRadius) {
                    m_paletteWallColors[(size_t)z * m_mapW + x] = spawnWallColor;
                    m_paletteFloorColors[(size_t)z * m_mapW + x] = spawnFloorColor;
                }
            }
        }
    }

    m_endSafeX0 = result.endSafeX0;
    m_endSafeZ0 = result.endSafeZ0;
    m_endSafeX1 = result.endSafeX1;
    m_endSafeZ1 = result.endSafeZ1;
    m_exitDoorPos = result.exitDoorPos;

    m_smallSafeZoneCenters = std::move(result.smallSafeZoneCenters);
    m_smallSafeZoneRadius = result.smallSafeZoneRadius;

    // One diary per pocket. Read state starts cleared; loadSlot() restores it
    m_diaries = std::move(result.diaries);
    m_storyVariants = result.storyVariants;
    m_diariesRead.assign(m_diaries.size(), false);
    m_diaryReadOrder.clear();
    rebuildDiaryReadWallLookup();

    m_minimapFog.resetExplored(m_mapW, m_mapH);
}

void DungeonScene::placeTorches(unsigned int seed)
{
    MapGenerator::TorchPlacement placement = MapGenerator::PlaceTorches(
        m_mapW, m_mapH,
        m_endSafeX0, m_endSafeZ0, m_endSafeX1, m_endSafeZ1,
        m_smallSafeZoneCenters, m_smallSafeZoneRadius,
        [this](int x, int z) { return isWall(x, z); },
        [this](int x, int z) { return isFloor(x, z); },
        [this](int x, int z) { return wallCornerCut(x, z) != WallShapes::CornerCut::None; },
        MAX_TORCHES,
        seed
    );

    m_torchWallBase = std::move(placement.wallBase);
    m_torchNormal = std::move(placement.normal);
    m_torchFlamePos = std::move(placement.flamePos);
    m_torchColor = std::move(placement.color);
    m_torchIntensity = std::move(placement.intensity);
    m_torchCellLookup = std::move(placement.torchCellLookup);

    m_torchTaken.assign(m_torchWallBase.size(), 0);
    resetTorchLitMask();
}

// Decorative props at corridor forks: plain diffuse geometry (matId 0), no collision
static void AddCairnProp(std::vector<Vertex>& verts, std::vector<GLuint>& indices, const glm::vec3& base)
{
    // Low segment counts: extra smoothness is invisible after the ASCII pass
    const glm::vec3 stoneColor(0.62f, 0.60f, 0.56f);

    AddSphere(verts, indices, base + glm::vec3(0.0f, 0.11f, 0.0f), 0.11f, 4, 3, stoneColor, 0.0f);
    AddSphere(verts, indices, base + glm::vec3(0.16f, 0.08f, 0.05f), 0.08f, 4, 3, stoneColor, 0.0f);
    AddSphere(verts, indices, base + glm::vec3(-0.13f, 0.06f, -0.08f), 0.06f, 4, 3, stoneColor, 0.0f);
}

static void AddObeliskProp(std::vector<Vertex>& verts, std::vector<GLuint>& indices, const glm::vec3& base)
{
    const glm::vec3 stoneColor(0.58f, 0.58f, 0.62f);
    AddCylinder(verts, indices, base, base + glm::vec3(0.0f, 0.30f, 0.0f), 0.09f, 0.085f, 4, stoneColor, 0.0f);
    AddCylinder(verts, indices, base + glm::vec3(0.0f, 0.30f, 0.0f), base + glm::vec3(0.025f, 0.38f, -0.015f), 0.085f, 0.015f, 4, stoneColor, 0.0f);
}

static void AddTripodProp(std::vector<Vertex>& verts, std::vector<GLuint>& indices, const glm::vec3& base)
{
    const glm::vec3 woodColor(0.45f, 0.32f, 0.18f);
    const float legLen = 0.32f;
    const float spread = 0.11f;
    const glm::vec3 top = base + glm::vec3(0.0f, legLen, 0.0f);
    AddCylinder(verts, indices, base + glm::vec3(spread, 0.0f, 0.0f), top, 0.020f, 0.014f, 4, woodColor, 0.0f);
    AddCylinder(verts, indices, base + glm::vec3(-spread * 0.5f, 0.0f, spread * 0.87f), top, 0.020f, 0.014f, 4, woodColor, 0.0f);
    AddCylinder(verts, indices, base + glm::vec3(-spread * 0.5f, 0.0f, -spread * 0.87f), top, 0.020f, 0.014f, 4, woodColor, 0.0f);
}

static void AddCrossProp(std::vector<Vertex>& verts, std::vector<GLuint>& indices, const glm::vec3& base)
{
    const glm::vec3 woodColor(0.42f, 0.30f, 0.16f);
    AddCylinder(verts, indices, base, base + glm::vec3(0.0f, 0.30f, 0.0f), 0.022f, 0.020f, 4, woodColor, 0.0f);
    AddCylinder(verts, indices,
                base + glm::vec3(-0.10f, 0.20f, 0.0f), base + glm::vec3(0.10f, 0.20f, 0.0f),
                0.018f, 0.018f, 4, woodColor, 0.0f);
}

// matId range 0.3..0.4 is left untextured by scene.frag:
// the exit door and the donut are flat colored, not brick.
static constexpr float kUntexturedMatId = 0.35f;

static void AddOrientedBox(std::vector<Vertex>& verts, std::vector<GLuint>& indices,
                           const glm::vec3& center, const glm::vec3& right, const glm::vec3& up,
                           const glm::vec3& front, const glm::vec3& halfExtents,
                           const glm::vec3& color, float matId)
{
    const glm::vec3 axes[3] = { right, up, front };
    for (int axis = 0; axis < 3; ++axis)
    {
        const glm::vec3 a = axes[(axis + 1) % 3] * halfExtents[(axis + 1) % 3];
        const glm::vec3 b = axes[(axis + 2) % 3] * halfExtents[(axis + 2) % 3];
        for (int sign = -1; sign <= 1; sign += 2)
        {
            const glm::vec3 n = axes[axis] * (float)sign;
            const glm::vec3 c = center + n * halfExtents[axis];
            const GLuint base = (GLuint)verts.size();
            verts.push_back({ c - a - b, n, color, matId });
            verts.push_back({ c + a - b, n, color, matId });
            verts.push_back({ c + a + b, n, color, matId });
            verts.push_back({ c - a + b, n, color, matId });
            // Winding does not matter: face culling is off
            indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        }
    }
}

// The exit door: frame, panel and handle. openAngle (radians, 0 = closed) swings the panel on its
// hinge away from the front
static void AddExitDoor(std::vector<Vertex>& verts, std::vector<GLuint>& indices,
                        const glm::vec3& base, float openAngle)
{
    using namespace ExitDoor;
    const glm::vec3 frameColor(0.30f, 0.20f, 0.12f);
    const glm::vec3 panelColor(0.46f, 0.33f, 0.19f);
    const glm::vec3 handleColor(0.78f, 0.64f, 0.30f);

    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 right(Right().x, 0.0f, Right().y);
    const glm::vec3 front(Front().x, 0.0f, Front().y);
    const float w = kOpeningWidth, h = kOpeningHeight, fw = kFrameWidth;

    const glm::vec3 jambHalf(fw * 0.5f, (h + fw) * 0.5f, kFrameDepth * 0.5f);
    for (int side = -1; side <= 1; side += 2)
    {
        const glm::vec3 c = base + right * ((float)side * (w + fw) * 0.5f) + up * ((h + fw) * 0.5f);
        AddOrientedBox(verts, indices, c, right, up, front, jambHalf, frameColor, kUntexturedMatId);
    }
    AddOrientedBox(verts, indices, base + up * (h + fw * 0.5f), right, up, front,
                   glm::vec3(w * 0.5f + fw, fw * 0.5f, kFrameDepth * 0.5f), frameColor, kUntexturedMatId);

    const glm::vec3 panelRight = right * std::cos(openAngle) - front * std::sin(openAngle);
    const glm::vec3 panelFront = right * std::sin(openAngle) + front * std::cos(openAngle);
    const glm::vec3 hinge = base - right * (w * 0.5f);
    const float gap = 0.01f;
    AddOrientedBox(verts, indices, hinge + panelRight * (w * 0.5f) + up * (h * 0.5f),
                   panelRight, up, panelFront,
                   glm::vec3(w * 0.5f - gap, h * 0.5f - gap, kPanelThickness * 0.5f),
                   panelColor, kUntexturedMatId);

    // A knob on each side of the panel, at the latch edge
    const glm::vec3 latch = hinge + panelRight * (w - 0.09f) + up * (h * 0.48f);
    const float reach = kPanelThickness * 0.5f + 0.035f;
    AddCylinder(verts, indices, latch - panelFront * reach, latch + panelFront * reach,
                0.012f, 0.012f, 6, handleColor, kUntexturedMatId);
    AddSphere(verts, indices, latch + panelFront * reach, 0.026f, 8, 6, handleColor, kUntexturedMatId);
    AddSphere(verts, indices, latch - panelFront * reach, 0.026f, 8, 6, handleColor, kUntexturedMatId);
}

// The donut tumbles on two axes. Its mesh is rebuilt every frame while it spins
// so the rotation is baked on the CPU
static void AddSpinningTorus(std::vector<Vertex>& verts, std::vector<GLuint>& indices,
                              const glm::vec3& center, float angleA, float angleB,
                              float majorRadius, float minorRadius,
                              int majorSegments, int minorSegments,
                              const glm::vec3& color)
{
    const glm::mat3 rotA(glm::rotate(glm::mat4(1.0f), angleA, glm::vec3(1.0f, 0.0f, 0.0f)));
    const glm::mat3 rotB(glm::rotate(glm::mat4(1.0f), angleB, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::mat3 rot = rotB * rotA;

    const size_t startIdx = verts.size();
    for (int i = 0; i <= majorSegments; ++i)
    {
        const float u = (float)i / (float)majorSegments * glm::two_pi<float>();
        const float cu = std::cos(u), su = std::sin(u);
        for (int j = 0; j <= minorSegments; ++j)
        {
            const float v = (float)j / (float)minorSegments * glm::two_pi<float>();
            const float cv = std::cos(v), sv = std::sin(v);

            const glm::vec3 local(
                (majorRadius + minorRadius * cv) * cu,
                minorRadius * sv,
                (majorRadius + minorRadius * cv) * su
            );
            const glm::vec3 normalLocal(cv * cu, sv, cv * su);

            verts.push_back({ center + rot * local, rot * normalLocal, color, kUntexturedMatId });
        }
    }

    for (int i = 0; i < majorSegments; ++i)
    {
        for (int j = 0; j < minorSegments; ++j)
        {
            const GLuint a = (GLuint)(startIdx + (size_t)i * (minorSegments + 1) + j);
            const GLuint b = (GLuint)(startIdx + (size_t)(i + 1) * (minorSegments + 1) + j);
            const GLuint c = (GLuint)(startIdx + (size_t)(i + 1) * (minorSegments + 1) + j + 1);
            const GLuint d = (GLuint)(startIdx + (size_t)i * (minorSegments + 1) + j + 1);
            indices.push_back(a); indices.push_back(b); indices.push_back(c);
            indices.push_back(a); indices.push_back(c); indices.push_back(d);
        }
    }
}

static constexpr float kStoneVisualRadius = 0.0375f; // shared by BuildStoneMesh() and placeStones()

// Tiny sphere stones at the given positions, for both pickable and thrown stones
static void BuildStoneMesh(std::vector<Vertex>& verts, std::vector<GLuint>& indices,
                            const std::vector<glm::vec3>& positions)
{
    const glm::vec3 stoneColor(0.40f, 0.38f, 0.36f);
    for (const glm::vec3& p : positions)
        AddSphere(verts, indices, p, kStoneVisualRadius, 6, 4, stoneColor, 0.0f);
}

// Buffers are rewritten in full every upload, hence GL_DYNAMIC_DRAW
static void UploadDynamicStoneMesh(GLuint& vao, GLuint& vbo, GLuint& ebo, GLsizei& indexCount,
                                     const std::vector<Vertex>& verts, const std::vector<GLuint>& indices)
{
    // Attribute bindings live in the VAO
    // they are set up once
    const bool firstTime = (vao == 0);
    if (firstTime)
    {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
    }

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(Vertex)),
                 verts.empty() ? nullptr : verts.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices.size() * sizeof(GLuint)),
                 indices.empty() ? nullptr : indices.data(), GL_DYNAMIC_DRAW);

    if (firstTime)
    {
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, matId));
        glEnableVertexAttribArray(3);
    }

    glBindVertexArray(0);

    indexCount = (GLsizei)indices.size();
}

void DungeonScene::placeStones(unsigned int seed)
{
    m_stoneWorldPositions.clear();
    m_stonePickedUp.clear();

    // Own RNG stream: the stone layout does not depend on the order of other generators
    std::mt19937 rng(seed ^ 0x53746F6Eu); // amazing 0x53746F6Eu the best part from the whole code
    std::uniform_int_distribution<int> countDist(1, 2);

    // Diagonal offsets inside the 3x3 pocket, clear of the diary in the center
    const glm::vec2 kOffsetCandidates[4] = {
        glm::vec2(0.6f, 0.6f), glm::vec2(-0.6f, -0.6f),
        glm::vec2(0.6f, -0.6f), glm::vec2(-0.6f, 0.6f)
    };

    for (const glm::ivec2& pocket : m_smallSafeZoneCenters)
    {
        const int count = countDist(rng);

        std::array<int, 4> order{ 0, 1, 2, 3 };
        std::shuffle(order.begin(), order.end(), rng);

        for (int k = 0; k < count; ++k)
        {
            const glm::vec2& off = kOffsetCandidates[(size_t)order[(size_t)k]];
            m_stoneWorldPositions.emplace_back(
                (float)pocket.x + 0.5f + off.x,
                kStoneVisualRadius,
                (float)pocket.y + 0.5f + off.y);
            m_stonePickedUp.push_back(0);
        }
    }

    rebuildStonePickupMesh();
}

void DungeonScene::rebuildStonePickupMesh()
{
    std::vector<glm::vec3> remaining;
    remaining.reserve(m_stoneWorldPositions.size());
    for (size_t i = 0; i < m_stoneWorldPositions.size(); ++i)
        if (!m_stonePickedUp[i])
            remaining.push_back(m_stoneWorldPositions[i]);

    std::vector<Vertex> verts;
    std::vector<GLuint> indices;
    BuildStoneMesh(verts, indices, remaining);
    UploadDynamicStoneMesh(m_stonePickupVao, m_stonePickupVbo, m_stonePickupEbo, m_stonePickupIndexCount, verts, indices);
}

void DungeonScene::markDiaryRead(int diaryIndex)
{
    if (diaryIndex < 0 || diaryIndex >= (int)m_diaries.size() || m_diariesRead[(size_t)diaryIndex])
        return;
    m_diariesRead[(size_t)diaryIndex] = true;
    Diaries::PlacedDiary& diary = m_diaries[(size_t)diaryIndex];
    diary.storyStep = (int)m_diaryReadOrder.size();
    diary.text = Diaries::StepText(diary.storyStep, m_storyVariants);
    m_diaryReadOrder.push_back(diaryIndex);
}

void DungeonScene::rebuildDiaryReadWallLookup()
{
    m_diaryReadWallLookup.assign((size_t)m_mapW * m_mapH, 0);

    int markedWallCells = 0;
    int readPockets = 0;
    for (size_t i = 0; i < m_diariesRead.size() && i < m_smallSafeZoneCenters.size(); ++i)
    {
        if (!m_diariesRead[i])
            continue;
        ++readPockets;

        const glm::ivec2& pocket = m_smallSafeZoneCenters[i];
        const int r = m_smallSafeZoneRadius;
        for (int z = pocket.y - r - 1; z <= pocket.y + r + 1; ++z)
        {
            for (int x = pocket.x - r - 1; x <= pocket.x + r + 1; ++x)
            {
                if (x < 0 || x >= m_mapW || z < 0 || z >= m_mapH)
                    continue;
                if (isWall(x, z))
                {
                    m_diaryReadWallLookup[(size_t)z * m_mapW + x] = 1;
                    ++markedWallCells;
                }
            }
        }
    }

    std::fprintf(stderr,
        "DungeonScene: diary-read wall lookup rebuilt - %d pockets read, %d wall cells marked.\n",
        readPockets, markedWallCells);
}

void DungeonScene::placeLandmarks(unsigned int seed)
{
    m_firstGuideTorchIndex = (int)m_torchWallBase.size();
    m_safeZoneChainOrder.clear();
    m_guideTorchCellLookup.assign((size_t)m_mapW * m_mapH, 0);

    const size_t pocketCount = m_smallSafeZoneCenters.size();
    if (pocketCount < 2)
        return;

    // Nearest-neighbor tour over the pockets. Straight-line distance is enough to order them;
    // real paths are computed only between chosen neighbors
    std::vector<unsigned char> visited(pocketCount, 0);
    m_safeZoneChainOrder.push_back(0);
    visited[0] = 1;
    while (m_safeZoneChainOrder.size() < pocketCount)
    {
        const glm::ivec2& from = m_smallSafeZoneCenters[(size_t)m_safeZoneChainOrder.back()];
        int best = -1;
        int bestDistSq = std::numeric_limits<int>::max();
        for (size_t i = 0; i < pocketCount; ++i)
        {
            if (visited[i]) continue;
            const glm::ivec2 d = m_smallSafeZoneCenters[i] - from;
            const int distSq = d.x * d.x + d.y * d.y;
            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                best = (int)i;
            }
        }
        m_safeZoneChainOrder.push_back(best);
        visited[(size_t)best] = 1;
    }

    // For each link, follow the real corridor path to the next pocket and place a few guide torches
    // along its first cells. Only the start of the path is marked: it hints at the direction
    // without revealing the route
    std::mt19937 rng(seed ^ 0x4C616E64u);

    const int kMinimapHintCells = 4;
    const int kMaxGuideTorches = 3;

    static const glm::ivec2 kNeighborDirs[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };

    const int kMaxProps = 18;
    std::vector<Vertex> propVerts;
    std::vector<GLuint> propIndices;
    int propsPlaced = 0;
    m_landmarkPropPositionsXZ.clear();

    for (size_t k = 0; k + 1 < m_safeZoneChainOrder.size(); ++k)
    {
        const glm::ivec2 startCell = m_smallSafeZoneCenters[(size_t)m_safeZoneChainOrder[k]];
        const glm::ivec2 goalCell  = m_smallSafeZoneCenters[(size_t)m_safeZoneChainOrder[k + 1]];

        std::vector<glm::ivec2> path = GridPathfinding::FindPath(m_mapW, m_mapH, m_map, startCell, goalCell);
        if (path.size() < 2)
            continue; // no path (should not happen on a connected maze): skip this link

        const int hintCount = std::min((int)path.size() - 1, kMinimapHintCells);

        // Guide torches are mounted like MapGenerator's TryAddTorch()
        int placedTorches = 0;
        for (int p = 1; p <= hintCount && placedTorches < kMaxGuideTorches; ++p)
        {
            const glm::ivec2& c = path[(size_t)p];
            for (const glm::ivec2& d : kNeighborDirs)
            {
                const int wx = c.x + d.x, wz = c.y + d.y;
                if (wx < 0 || wx >= m_mapW || wz < 0 || wz >= m_mapH)
                    continue;
                if (!isWall(wx, wz))
                    continue;
                if (wallCornerCut(wx, wz) != WallShapes::CornerCut::None)
                    continue; // chamfered walls are unreliable mount points

                // Normal points from the wall to the floor, opposite of d
                const glm::vec3 normal((float)-d.x, 0.0f, (float)-d.y);

                const glm::vec3 wallCenter((float)wx + 0.5f, 0.0f, (float)wz + 0.5f);
                const glm::vec3 wallBase = wallCenter + normal * 0.5f;

                // Same minimum spacing as TryAddTorch(), against all torches
                bool tooClose = false;
                for (const glm::vec3& existing : m_torchWallBase)
                {
                    const glm::vec3 diff = wallBase - existing;
                    if (glm::dot(diff, diff) < 0.15f * 0.15f) { tooClose = true; break; }
                }
                if (tooClose)
                    continue;

                const glm::vec3 flamePos = wallBase + normal * 0.14f + glm::vec3(0.0f, 0.665f, 0.0f);

                m_torchWallBase.push_back(wallBase);
                m_torchNormal.push_back(normal);
                m_torchFlamePos.push_back(flamePos);
                m_torchColor.push_back(glm::vec3(0.30f, 0.55f, 1.0f)); // landmark torches burn blue
                std::uniform_real_distribution<float> intensRange(2.3f, 2.8f);
                m_torchIntensity.push_back(intensRange(rng));
                // Guide torches cannot be picked up or extinguished
                m_torchTaken.push_back(1);

                // The minimap marks the wall cell, like m_torchCellLookup
                if (wx >= 0 && wx < m_mapW && wz >= 0 && wz < m_mapH)
                    m_guideTorchCellLookup[(size_t)wz * m_mapW + wx] = 1;

                ++placedTorches;
                break; // one wall per path cell
            }
        }

        // Props on every other link at about 1/3 and 2/3 of the path. They are decoration, not a
        // hint, so they are not limited to the first cells
        if (propsPlaced < kMaxProps && (k % 2 == 0) && path.size() >= 3)
        {
            const size_t marks[2] = { path.size() / 3, (path.size() * 2) / 3 };
            for (size_t m = 0; m < 2 && propsPlaced < kMaxProps; ++m)
            {
                if (m == 1 && marks[1] - marks[0] < 3)
                    continue; // too close to the first mark of this link

                const glm::ivec2& mid = path[marks[m]];

                glm::vec2 wallDir(0.0f, 0.0f);
                for (const glm::ivec2& d : kNeighborDirs)
                {
                    if (isWall(mid.x + d.x, mid.y + d.y)) { wallDir = glm::vec2((float)d.x, (float)d.y); break; }
                }
                const glm::vec3 propBase(
                    (float)mid.x + 0.5f + wallDir.x * 0.32f,
                    0.0f,
                    (float)mid.y + 0.5f + wallDir.y * 0.32f);

                const size_t vertsBefore = propVerts.size();
                switch (propsPlaced % 4)
                {
                    case 0: AddCairnProp(propVerts, propIndices, propBase); break;
                    case 1: AddObeliskProp(propVerts, propIndices, propBase); break;
                    case 2: AddTripodProp(propVerts, propIndices, propBase); break;
                    default: AddCrossProp(propVerts, propIndices, propBase); break;
                }
                for (size_t vi = vertsBefore; vi < propVerts.size(); ++vi)
                {
                    propVerts[vi].pos.x += wallDir.x * propVerts[vi].pos.y * 0.35f;
                    propVerts[vi].pos.z += wallDir.y * propVerts[vi].pos.y * 0.35f;
                }
                ++propsPlaced;
                m_landmarkPropPositionsXZ.emplace_back(propBase.x, propBase.z);
            }
        }
    }

    UploadDynamicStoneMesh(m_landmarkPropVao, m_landmarkPropVbo, m_landmarkPropEbo,
                           m_landmarkPropIndexCount, propVerts, propIndices);

    std::fprintf(stderr,
        "DungeonScene: landmarks placed - %d guide torches (indices %d..%zu), %d props.\n",
        (int)m_torchWallBase.size() - m_firstGuideTorchIndex,
        m_firstGuideTorchIndex, m_torchWallBase.size(),
        propsPlaced);
}

void DungeonScene::tryAutoPickupStones()
{
    if (m_stoneWorldPositions.empty())
        return;

    const float kPickupRadius = 0.5f;
    const glm::vec3 camPos = m_player.camPos();
    bool pickedAny = false;

    for (size_t i = 0; i < m_stoneWorldPositions.size(); ++i)
    {
        if (m_stonePickedUp[i])
            continue;

        const glm::vec2 to(m_stoneWorldPositions[i].x - camPos.x, m_stoneWorldPositions[i].z - camPos.z);
        if (glm::dot(to, to) <= kPickupRadius * kPickupRadius)
        {
            m_stonePickedUp[i] = 1;
            m_player.addStoneToInventory();
            UiAudio::PlayClick();
            pickedAny = true;
        }
    }

    if (pickedAny)
        rebuildStonePickupMesh();
}

void DungeonScene::updateThrownStones(float deltaTime)
{
    if (m_thrownStones.empty())
        return;

    const float kGravity = 9.8f;
    const float kStoneRadius = kStoneVisualRadius;
    const float kEnemyHitRadius = EnemyAI::kCollisionRadius + kStoneRadius;
    // Louder than footsteps, otherwise it would not work as a distraction
    const float kNoiseImpactRadius = 9.0f;
    const float kMaxLifetime = 4.0f; // safety cap in case a stone never lands

    for (size_t i = 0; i < m_thrownStones.size(); )
    {
        ThrownStone& stone = m_thrownStones[i];
        stone.vel.y -= kGravity * deltaTime;
        const glm::vec3 nextPos = stone.pos + stone.vel * deltaTime;
        stone.life += deltaTime;

        bool consumed = false;

        // A direct enemy hit takes priority over a wall/floor impact. Circle test in XZ
        for (int e = 0; e < kEnemyCount && !consumed; ++e)
        {
            if (!m_enemies[e].isLoaded())
                continue;

            const glm::vec3 enemyPos = m_enemyAIs[e].position();
            const glm::vec2 d(nextPos.x - enemyPos.x, nextPos.z - enemyPos.z);
            if (glm::dot(d, d) <= kEnemyHitRadius * kEnemyHitRadius)
            {
                m_enemyAIs[e].forceAggroFromImpact(m_player.camPos());
                consumed = true;
            }
        }

        // A wall/floor impact still makes noise
        if (!consumed)
        {
            const int cx = (int)std::floor(nextPos.x);
            const int cz = (int)std::floor(nextPos.z);
            if (nextPos.y <= kStoneRadius || isWall(cx, cz))
            {
                for (int e = 0; e < kEnemyCount; ++e)
                {
                    if (!m_enemies[e].isLoaded())
                        continue;
                    const glm::vec3 enemyPos = m_enemyAIs[e].position();
                    const glm::vec2 d(nextPos.x - enemyPos.x, nextPos.z - enemyPos.z);
                    if (glm::dot(d, d) <= kNoiseImpactRadius * kNoiseImpactRadius)
                        m_enemyAIs[e].notifyNoiseEvent(nextPos);
                }
                consumed = true;
            }
        }

        if (!consumed && stone.life >= kMaxLifetime)
            consumed = true;

        if (consumed)
        {
            m_thrownStones.erase(m_thrownStones.begin() + (long)i);
        }
        else
        {
            stone.pos = nextPos;
            ++i;
        }
    }

    std::vector<glm::vec3> flyingPositions;
    flyingPositions.reserve(m_thrownStones.size());
    for (const ThrownStone& s : m_thrownStones)
        flyingPositions.push_back(s.pos);

    std::vector<Vertex> verts;
    std::vector<GLuint> indices;
    BuildStoneMesh(verts, indices, flyingPositions);
    UploadDynamicStoneMesh(m_thrownStoneVao, m_thrownStoneVbo, m_thrownStoneEbo, m_thrownStoneIndexCount, verts, indices);
}

void DungeonScene::updateWinSequence(float deltaTime)
{
    auto uploadDoor = [this](float openAngle) {
        m_exitDoorVertsScratch.clear();
        m_exitDoorIndicesScratch.clear();
        AddExitDoor(m_exitDoorVertsScratch, m_exitDoorIndicesScratch, m_exitDoorPos, openAngle);
        UploadDynamicStoneMesh(m_exitDoorVao, m_exitDoorVbo, m_exitDoorEbo, m_exitDoorIndexCount,
                               m_exitDoorVertsScratch, m_exitDoorIndicesScratch);
    };

    if (m_winSequenceState == WinSequenceState::None)
    {
        // The closed door is built once per map and does not change until the sequence starts
        if (!m_exitDoorBuiltOnce)
        {
            m_exitDoorBuiltOnce = true;
            uploadDoor(0.0f);
        }
        return;
    }

    m_winSequenceTimer += deltaTime;
    const float openAngle = glm::radians(ExitDoor::kOpenAngleDeg);

    if (m_winSequenceState == WinSequenceState::Opening)
    {
        const float t = std::clamp(m_winSequenceTimer / kExitDoorOpenDuration, 0.0f, 1.0f);
        uploadDoor(openAngle * t * t * (3.0f - 2.0f * t));

        if (m_winSequenceTimer >= kExitDoorOpenDuration)
        {
            uploadDoor(openAngle);
            m_winSequenceState = WinSequenceState::Spinning;
            m_winSequenceTimer = 0.0f;
            m_torusAngleA = 0.0f;
            m_torusAngleB = 0.0f;
        }
        return;
    }

    // Different speeds on the two axes; equal speeds would look like a spin around one diagonal
    const float kSpinSpeedA = 1.1f; // rad/sec
    const float kSpinSpeedB = 0.7f;
    m_torusAngleA = std::fmod(m_torusAngleA + kSpinSpeedA * deltaTime, glm::two_pi<float>());
    m_torusAngleB = std::fmod(m_torusAngleB + kSpinSpeedB * deltaTime, glm::two_pi<float>());

    const float appear = std::clamp(m_winSequenceTimer / kDonutAppearDuration, 0.0f, 1.0f);
    m_donutAlpha = appear * appear * (3.0f - 2.0f * appear);

    // Sized to tumble inside the doorway without touching the frame
    m_donutVertsScratch.clear();
    m_donutIndicesScratch.clear();
    const glm::vec3 torusCenter = m_exitDoorPos + glm::vec3(0.0f, ExitDoor::kOpeningHeight * 0.5f, 0.0f);
    AddSpinningTorus(m_donutVertsScratch, m_donutIndicesScratch, torusCenter, m_torusAngleA,
                     m_torusAngleB, 0.24f, 0.09f, 24, 12, glm::vec3(0.62f, 0.62f, 0.62f));
    UploadDynamicStoneMesh(m_donutVao, m_donutVbo, m_donutEbo, m_donutIndexCount,
                           m_donutVertsScratch, m_donutIndicesScratch);
}

// AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA

void DungeonScene::updateGlitchEffects(float deltaTime)
{
    static std::mt19937 rng(std::random_device{}());

    // Drives glitchStage(); the caller only ticks this during unpaused gameplay
    m_glitchElapsedTime += deltaTime;

    const int stage = glitchStage();

    // Effect 1: disappearing wall glyph (stage >= 1)
    if (m_glyphGlitchActiveTimer > 0.0f)
    {
        m_glyphGlitchActiveTimer -= deltaTime;
    }
    else if (stage >= 1)
    {
        m_glyphGlitchCooldown -= deltaTime;
        if (m_glyphGlitchCooldown <= 0.0f)
        {
            std::uniform_real_distribution<float> uvDist(0.05f, 0.95f);
            m_glyphGlitchUV = glm::vec2(uvDist(rng), uvDist(rng));
            m_glyphGlitchActiveTimer = (stage >= 4) ? 0.25f : 0.15f;

            // Radius in ASCII cells.
            std::uniform_real_distribution<float> radiusDist(4.0f, 7.0f);
            m_glyphGlitchRadiusCells = radiusDist(rng);

            float lo = 7.0f, hi = 10.0f;
            if (stage == 2)      { lo = 5.0f; hi = 7.0f; }
            else if (stage == 3) { lo = 4.0f; hi = 6.0f; }
            else if (stage >= 4) { lo = 2.0f; hi = 5.0f; }
            std::uniform_real_distribution<float> cdDist(lo, hi);
            m_glyphGlitchCooldown = cdDist(rng);
        }
    }

    // Effect 2: nearest torch flame trembles (stage >= 2)
    const bool torchGlitchStillActive = m_torchGlitchUntilTime > 0.0 && glfwGetTime() < m_torchGlitchUntilTime;
    if (!torchGlitchStillActive && stage >= 2)
    {
        m_torchGlitchCooldown -= deltaTime;
        if (m_torchGlitchCooldown <= 0.0f && !m_torchWallBase.empty())
        {
            // Linear scan over all torches; this runs rarely
            const glm::vec3 camPos = m_player.camPos();
            int nearest = -1;
            float bestDistSq = std::numeric_limits<float>::max();
            for (size_t i = 0; i < m_torchWallBase.size(); ++i)
            {
                const glm::vec3 diff = m_torchWallBase[i] - camPos;
                const float distSq = glm::dot(diff, diff);
                if (distSq < bestDistSq) { bestDistSq = distSq; nearest = (int)i; }
            }
            m_torchGlitchIndex = nearest;
            const double duration = (stage >= 4) ? 0.6 : 0.4;
            m_torchGlitchUntilTime = glfwGetTime() + duration;

            // Stage 4 does not make this effect more frequent
            float lo = 12.0f, hi = 16.0f;
            if (stage >= 3) { lo = 8.0f; hi = 12.0f; }
            std::uniform_real_distribution<float> cdDist(lo, hi);
            m_torchGlitchCooldown = cdDist(rng);
        }
    }

    // Effect 4: flickering enemy silhouette (stage >= 4)
    if (m_silhouetteActiveTimer > 0.0f)
    {
        m_silhouetteActiveTimer -= deltaTime;
        // The pose is static, but the clip clock must tick, or draw() shows the first animation
        // frame instead of the idle stance
        m_glitchGhost.update(0.0f);
    }
    else if (stage >= 4)
    {
        m_silhouetteCooldown -= deltaTime;
        if (m_silhouetteCooldown <= 0.0f)
        {
            const glm::vec3 camPos = m_player.camPos();
            glm::vec3 front = m_player.getFront();
            front.y = 0.0f;
            const float frontLen = glm::length(front);
            bool placed = false;

            if (frontLen > 0.0001f)
            {
                front /= frontLen;
                const glm::vec3 right(-front.z, 0.0f, front.x);

                // Closer reads as a teleport rather than a glitch
                const float kMinDist = 4.5f;
                // Just under the player's sight radius in isEnemyVisibleToPlayer()
                const float kMaxDist = 13.0f;
                // Slightly narrower than the real FOV, away from the frame edge
                const float kHalfFOVDeg = 40.0f;

                std::uniform_real_distribution<float> distDist(kMinDist, kMaxDist);
                std::uniform_real_distribution<float> angleDist(-kHalfFOVDeg, kHalfFOVDeg);

                for (int attempt = 0; attempt < 12 && !placed; ++attempt)
                {
                    const float dist = distDist(rng);
                    const float angleRad = glm::radians(angleDist(rng));
                    const glm::vec3 dir = front * std::cos(angleRad) + right * std::sin(angleRad);
                    const glm::vec3 candidate = camPos + dir * dist;

                    const int cx = (int)std::floor(candidate.x);
                    const int cz = (int)std::floor(candidate.z);
                    if (!isFloor(cx, cz))
                        continue;

                    // Must pass the same visibility test as a real enemy
                    if (!isEnemyVisibleToPlayer(candidate))
                        continue;

                    m_glitchGhost.setPosition(glm::vec3(cx + 0.5f, 0.0f, cz + 0.5f));

                    const glm::vec3 toPlayer = glm::normalize(camPos - candidate);
                    const float yawDeg = glm::degrees(std::atan2(toPlayer.x, toPlayer.z));
                    m_glitchGhost.setYawDegrees(yawDeg);
                    m_glitchGhost.setState(EnemyCharacter::State::Idle);
                    m_glitchGhost.update(0.0f);

                    placed = true;
                }
            }

            if (placed)
            {
                m_silhouetteActiveTimer = 1.5f; // long enough to notice, short enough to read as a flicker
                std::uniform_real_distribution<float> cdDist(35.0f, 60.0f);
                m_silhouetteCooldown = cdDist(rng);
            }
            else
            {
                // No valid spot: retry soon instead of waiting a full cooldown
                m_silhouetteCooldown = 5.0f;
            }
        }
    }

    // Fake footsteps run independently of the trigger, so they play out fully even if the stage
    // changes. Intervals match the real walk/run cadence (PlayerController.cpp)
    if (m_fakeFootstepsTimer > 0.0f)
    {
        m_fakeFootstepsTimer -= deltaTime;
        m_fakeFootstepsNextStepIn -= deltaTime;
        if (m_fakeFootstepsNextStepIn <= 0.0f)
        {
            static constexpr float kFakeWalkFootstepInterval = 0.68f / 1.5f;
            static constexpr float kFakeRunFootstepInterval = 0.95f / 3.0f;
            if (m_fakeFootstepsIsRun)
            {
                m_player.playFakeRunFootstep();
                m_fakeFootstepsNextStepIn = kFakeRunFootstepInterval;
            }
            else
            {
                m_player.playFakeWalkFootstep();
                m_fakeFootstepsNextStepIn = kFakeWalkFootstepInterval;
            }
        }
    }

    if (stage >= 3)
    {
        m_soundGlitchCooldown -= deltaTime;
        if (m_soundGlitchCooldown <= 0.0f)
        {
            // 0: moan (always)
            // 1: fake walk steps (not while walking)
            // 2: fake run steps (not while running)
            int kinds[3];
            int kindCount = 0;
            kinds[kindCount++] = 0;
            if (!m_player.isMoving() || m_player.isRunning()) kinds[kindCount++] = 1;
            if (!m_player.isRunning()) kinds[kindCount++] = 2;

            std::uniform_int_distribution<int> pickDist(0, kindCount - 1);
            const int kind = kinds[pickDist(rng)];

            if (kind == 0)
            {
                std::uniform_real_distribution<float> volDist(0.55f, 0.85f);
                m_glitchAudio.playMoan(volDist(rng));
            }
            else
            {
                std::uniform_real_distribution<float> durDist(2.0f, 4.0f);
                m_fakeFootstepsTimer = durDist(rng);
                m_fakeFootstepsNextStepIn = 0.0f; // first step immediately, not after a full interval
                m_fakeFootstepsIsRun = (kind == 2);
            }
            std::uniform_real_distribution<float> cdDist(20.0f, 35.0f);
            m_soundGlitchCooldown = cdDist(rng);
        }
    }
}

// Marks every torch lit again; used for a new map. Pickups update a single texel instead
void DungeonScene::resetTorchLitMask()
{
    if (m_torchLitMaskTex == 0)
        return;

    std::vector<unsigned char> allLit((size_t)kTorchLitMaskDim * kTorchLitMaskDim, 255);

    glBindTexture(GL_TEXTURE_2D, m_torchLitMaskTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                     kTorchLitMaskDim, kTorchLitMaskDim,
                     GL_RED, GL_UNSIGNED_BYTE, allLit.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void DungeonScene::pickupWallTorch(int torchIndex)
{
    if (torchIndex < 0 || (size_t)torchIndex >= m_torchWallBase.size())
        return;
    if (m_torchTaken[(size_t)torchIndex])
        return; // already taken; the pickup hint never offers it, but the check is cheap

    extinguishTorchVisualAndLight(torchIndex);
    rebuildTorchCellLookupFromTaken();
    m_player.addTorchToInventory();
    UiAudio::PlayClick();

    std::fprintf(stderr, "DungeonScene: torch #%d picked up (inventory now %d).\n",
                 torchIndex, m_player.torchInventoryCount());
}

void DungeonScene::restoreTorchPickups(const std::vector<int>& takenIndices)
{
    if (takenIndices.empty())
        return;

    for (int idx : takenIndices)
    {
        if (idx < 0 || (size_t)idx >= m_torchWallBase.size())
            continue; // corrupted or stale save: skip the index
        if (m_torchTaken[(size_t)idx])
            continue;
        extinguishTorchVisualAndLight(idx);
    }

    // One rebuild for the whole list
    rebuildTorchCellLookupFromTaken();
}

// Turns off a torch's light and flame and marks it taken
// The caller updates m_torchCellLookup and the inventory: a pickup adds a torch, restoring a save must not
void DungeonScene::extinguishTorchVisualAndLight(int torchIndex)
{
    m_torchTaken[(size_t)torchIndex] = true;

    // Lighting skips torches with zero intensity
    m_torchIntensity[(size_t)torchIndex] = 0.0f;

    // Hide the baked flame: one texel of uTorchLitMask
    if (m_torchLitMaskTex != 0)
    {
        const unsigned char off = 0;
        const int tx = torchIndex % kTorchLitMaskDim;
        const int ty = torchIndex / kTorchLitMaskDim;
        glBindTexture(GL_TEXTURE_2D, m_torchLitMaskTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, tx, ty, 1, 1, GL_RED, GL_UNSIGNED_BYTE, &off);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

// Minimap torch icons for untaken torches only
// Full rebuild; runs on pickup and on load
void DungeonScene::rebuildTorchCellLookupFromTaken()
{
    std::vector<glm::vec3> remainingWallBase;
    std::vector<glm::vec3> remainingNormal;
    remainingWallBase.reserve(m_torchWallBase.size());
    remainingNormal.reserve(m_torchNormal.size());
    for (size_t i = 0; i < m_torchWallBase.size(); ++i)
    {
        if (m_torchTaken[i])
            continue;
        remainingWallBase.push_back(m_torchWallBase[i]);
        remainingNormal.push_back(m_torchNormal[i]);
    }
    m_torchCellLookup = MapGenerator::BuildTorchCellLookup(
        m_mapW, m_mapH, remainingWallBase, remainingNormal);
}

// Palette of a cell: blends 50/50 on the border between the two nearest zone centers, pure nearest
// beyond kPaletteBlendWidth
static void BlendedZoneColor(const Zoning::ZoneGrid& zoneGrid, int cellX, int cellZ,
                              glm::vec3& outWallColor, glm::vec3& outFloorColor)
{
    if (zoneGrid.centers.empty())
    {
        SceneGeometry::GetZonePalette(0, outWallColor, outFloorColor);
        return;
    }

    const float fx = (float)cellX, fz = (float)cellZ;
    size_t nearestIdx = 0, secondIdx = 0;
    float nearestDistSq = 1e30f, secondDistSq = 1e30f; // I dont love exponential form

    for (size_t i = 0; i < zoneGrid.centers.size(); ++i)
    {
        const float dx = fx - zoneGrid.centers[i].x;
        const float dz = fz - zoneGrid.centers[i].z;
        const float distSq = dx * dx + dz * dz;
        if (distSq < nearestDistSq)
        {
            secondDistSq = nearestDistSq;
            secondIdx = nearestIdx;
            nearestDistSq = distSq;
            nearestIdx = i;
        }
        else if (distSq < secondDistSq)
        {
            secondDistSq = distSq;
            secondIdx = i;
        }
    }

    glm::vec3 wallA, floorA;
    SceneGeometry::GetZonePalette(zoneGrid.styles[nearestIdx].paletteIndex, wallA, floorA);

    if (zoneGrid.centers.size() < 2)
    {
        outWallColor = wallA;
        outFloorColor = floorA;
        return;
    }

    glm::vec3 wallB, floorB;
    SceneGeometry::GetZonePalette(zoneGrid.styles[secondIdx].paletteIndex, wallB, floorB);

    const float diff = std::sqrt(secondDistSq) - std::sqrt(nearestDistSq);
    const float blend = std::clamp(1.0f - diff / Zoning::kPaletteBlendWidth, 0.0f, 1.0f) * 0.5f;

    outWallColor = glm::mix(wallA, wallB, blend);
    outFloorColor = glm::mix(floorA, floorB, blend);
}

void DungeonScene::loadMapAndGeometry(unsigned int seed)
{
    generateMap(seed);
    m_currentSeed = seed;

    // Reset all glitch timers, not only the elapsed time:
    // active timers are not stage-gated, so an effect in progress would carry into the new run
    m_glitchElapsedTime = 0.0f;
    m_glyphGlitchActiveTimer = 0.0f;
    m_silhouetteActiveTimer = 0.0f;
    m_fakeFootstepsTimer = 0.0f;
    m_fakeFootstepsNextStepIn = 0.0f;
    m_torchGlitchUntilTime = -1.0;

    m_minimapFog.uploadMapTexture(m_mapW, m_mapH, m_map, m_wallCornerCuts, m_corridorWidened, m_diagonalChainMask);

    placeTorches(seed);
    placeStones(seed);
    placeLandmarks(seed);

    // Every map starts with a closed door and no donut;
    // building it here also shows it behind the main menu, where the gameplay update does not run.
    m_winSequenceState = WinSequenceState::None;
    m_winSequenceTimer = 0.0f;
    m_donutIndexCount = 0;
    m_exitDoorBuiltOnce = false;
    updateWinSequence(0.0f);

    m_torchShadowMap.build(
        [this](int x, int z) { return isWall(x, z); },
        [this](float x, float z) {
            const int cx = (int)std::floor(x), cz = (int)std::floor(z);
            if (!isWall(cx, cz))
                return false;
            return WallShapes::IsLocalPointSolid(x - (float)cx, z - (float)cz,
                                                 wallCornerCut(cx, cz), wallChamferSize(cx, cz));
        },
        m_columnCentersXZ, Columns::kColumnRadius, m_torchFlamePos);

    // Shared by all enemies by pointer
    m_pathfindingMapWithColumns = m_map;
    for (const glm::vec2& col : m_columnCentersXZ)
    {
        const int cx = (int)std::floor(col.x);
        const int cz = (int)std::floor(col.y);
        if (cx < 0 || cz < 0 || cx >= m_mapW || cz >= m_mapH)
            continue; // should not happen; skip rather than crash
        m_pathfindingMapWithColumns[(size_t)cz * m_mapW + cx] = 1;
    }

    for (int i = 0; i < kEnemyCount; ++i)
    {
        m_enemyAIs[i].init(
            m_mapW, m_mapH,
            &m_map, &m_wallCornerCuts, &m_diagonalChainMask,
            &m_columnCentersXZ, Columns::kColumnRadius,
            &m_pathfindingMapWithColumns
        );
    }
    spawnEnemiesAcrossMap(seed);

    // not necessary part:
    // Dev dummy (K): needs the map for its own collision. The first K press moves it in front of the player
    m_enemyAI.init(
        m_mapW, m_mapH,
        &m_map, &m_wallCornerCuts, &m_diagonalChainMask,
        &m_columnCentersXZ, Columns::kColumnRadius,
        &m_pathfindingMapWithColumns
    );
    const glm::vec3 dummySpawnPos(3.5f, 0.0f, 3.5f);
    m_enemyAI.setPosition(dummySpawnPos);

    // destroy() is a no-op on zero handles, so init(), newGame() and loadSlot() share this path
    m_geometry.destroy();

    // Pocket cell centers.
    std::vector<glm::vec3> diaryPositions;
    diaryPositions.reserve(m_diaries.size());
    for (const Diaries::PlacedDiary& d : m_diaries) {
        diaryPositions.emplace_back((float)d.pocketCellX + 0.5f, 0.0f, (float)d.pocketCellZ + 0.5f);
    }

    m_geometry.build(m_mapW, m_mapH, m_map, m_exitDoorPos,
                      m_torchWallBase, m_torchNormal, m_torchFlamePos,
                      m_wallCornerCuts, m_columnCentersXZ, m_chamferSizes,
                      m_paletteWallColors, m_paletteFloorColors,
                      diaryPositions,
                      m_firstGuideTorchIndex);

    // Chamfered cells break the greedy quad merge, so the chamfer probability shows up directly in these counts
    std::printf(
        "[perf] map %dx%d -> %d vertices, %d indices (%d triangles)\n",
        m_mapW, m_mapH,
        (int)m_geometry.vertexCount(),
        (int)m_geometry.indexCount(),
        (int)m_geometry.indexCount() / 3
    );

    // Both depend on the chunk layout from m_geometry.build()
    reserveRenderScratchBuffers();
    m_geometry.buildPVS(m_mapW, m_mapH, [this](int x, int z) { return isWall(x, z); });
}

void DungeonScene::generateFreshMapAndPlayer()
{
    std::random_device rd;
    const unsigned int seed = rd();
    loadMapAndGeometry(seed);

    // Center of the starting safe zone. resetForNewGame() also resets view, health, stamina and the
    // compass.
    const glm::vec3 spawnPos(6.5f, 0.5f, 6.5f);
    m_player.resetForNewGame(spawnPos);
}

// The map behind the main menu. It never picks a slot and never saves: saveActiveSlot() is a no-op
// while m_activeSlot < 0. Each launch shows a different spot:
// the starting zone, a pocket, the exit door or a random corridor
void DungeonScene::generateMenuBackgroundMaze()
{
    generateFreshMapAndPlayer();

    std::random_device rd;
    std::mt19937 rng(rd());
    auto yawTo = [](const glm::vec3& from, const glm::vec3& to) {
        return glm::degrees(std::atan2(to.z - from.z, to.x - from.x));
    };

    glm::vec3 pos(6.5f, 0.5f, 6.5f); // starting zone center
    float yaw = 45.0f * (float)std::uniform_int_distribution<int>(0, 7)(rng);

    enum Spot { StartZone, Pocket, Door, Corridor, SpotCount };
    Spot spot = (Spot)std::uniform_int_distribution<int>(0, SpotCount - 1)(rng);
    if (spot == Pocket && m_smallSafeZoneCenters.empty())
        spot = StartZone;

    if (spot == Pocket)
    {
        // A step off the pocket's center, looking at the diary that lies there
        const glm::ivec2 c = m_smallSafeZoneCenters[std::uniform_int_distribution<size_t>(
            0, m_smallSafeZoneCenters.size() - 1)(rng)];
        const glm::vec3 diary(c.x + 0.5f, 0.5f, c.y + 0.5f);
        static const glm::vec2 kOffsets[4] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        const glm::vec2 o = kOffsets[std::uniform_int_distribution<int>(0, 3)(rng)];
        pos = diary + glm::vec3(o.x, 0.0f, o.y);
        yaw = yawTo(pos, diary);
    }
    else if (spot == Door)
    {
        const glm::vec2 front = ExitDoor::Front();
        pos = glm::vec3(m_exitDoorPos.x + front.x * 2.2f, 0.5f, m_exitDoorPos.z + front.y * 2.2f);
        yaw = yawTo(pos, m_exitDoorPos);
    }
    else if (spot == Corridor)
    {
        // A floor cell away from the safe zones, looking down its longest open direction
        auto inSafeArea = [&](int x, int z) {
            if (x < 13 && z < 13)
                return true;
            if (x >= m_endSafeX0 - 1 && x <= m_endSafeX1 + 1 && z >= m_endSafeZ0 - 1 && z <= m_endSafeZ1 + 1)
                return true;
            for (const glm::ivec2& c : m_smallSafeZoneCenters)
                if (std::abs(c.x - x) <= 2 && std::abs(c.y - z) <= 2)
                    return true;
            return false;
        };
        std::uniform_int_distribution<int> cellX(1, m_mapW - 2), cellZ(1, m_mapH - 2);
        for (int attempt = 0; attempt < 500; ++attempt)
        {
            const int x = cellX(rng), z = cellZ(rng);
            if (!isFloor(x, z) || inSafeArea(x, z))
                continue;
            static const glm::ivec2 kDirs[4] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            int bestRun = 0;
            glm::ivec2 bestDir(1, 0);
            for (const glm::ivec2& d : kDirs)
            {
                int run = 0;
                while (run < 16 && isFloor(x + d.x * (run + 1), z + d.y * (run + 1)))
                    ++run;
                if (run > bestRun)
                {
                    bestRun = run;
                    bestDir = d;
                }
            }
            if (bestRun < 3)
                continue;
            pos = glm::vec3(x + 0.5f, 0.5f, z + 0.5f);
            yaw = glm::degrees(std::atan2((float)bestDir.y, (float)bestDir.x));
            break;
        }
    }

    // Tilted down so the floor and the torches above eye level both fit in frame
    m_player.restoreState(pos, yaw, -6.0f, 1.0f, 1.0f);
}

void DungeonScene::applyDifficulty()
{
    // A map with fewer pockets than the requirement would be unwinnable
    const DifficultyRules& rules = RulesFor(m_difficulty);
    m_player.setWinRules(std::min(rules.diariesToWin, (int)m_diaries.size()), rules.oneHitKills);
}

void DungeonScene::newGame(int explicitSlot, const std::string& explicitName)
{
    generateFreshMapAndPlayer();
    applyDifficulty();

    // explicitSlot >= 0: slot (and maybe name) chosen by the player
    //Otherwise the first empty slot or the oldest save, unnamed (death path)
    if (explicitSlot >= 0)
    {
        m_activeSlot = explicitSlot;
        m_saveName = explicitName;
    }
    else
    {
        m_activeSlot = SaveSystem::PickSlotForNewGame();
        m_saveName.clear();
    }
    saveActiveSlot();
}

bool DungeonScene::loadSlot(int slotIndex)
{
    SaveSystem::SaveData save = SaveSystem::LoadSlot(slotIndex);
    if (!save.valid) return false;

    // The seed reproduces the maze and torch layout, so saves store no geometry
    loadMapAndGeometry(save.seed);
    m_difficulty = DifficultyFromInt(save.difficulty);
    applyDifficulty();

    glm::vec3 pos(save.posX, save.posY, save.posZ);
    if (!(pos.x > 0.0f && pos.x < (float)m_mapW && pos.z > 0.0f && pos.z < (float)m_mapH)) {
        const SaveSystem::SaveData defaults;
        pos = glm::vec3(defaults.posX, defaults.posY, defaults.posZ);
    }
    m_player.restoreState(pos, save.yaw, save.pitch, save.healthFraction, save.staminaFraction);

    // Fog of war is restored only if its size matches the map
    if (save.explored.size() == (size_t)m_mapW * m_mapH) {
        m_minimapFog.setExplored(std::move(save.explored));
    }

    // Saved in read order, which decides the story step of each diary
    for (int idx : save.diariesReadIndices)
        markDiaryRead(idx);
    rebuildDiaryReadWallLookup();

    // Extinguish the torches taken at save time. Fuel and inventory are restored separately
    restoreTorchPickups(save.torchTakenIndices);
    m_player.restoreTorchState(save.torchFuel, save.torchInventoryCount);

    // Picked-up stones must not respawn; the inventory count is restored separately
    for (int idx : save.stoneTakenIndices) {
        if (idx >= 0 && (size_t)idx < m_stonePickedUp.size())
            m_stonePickedUp[(size_t)idx] = 1;
    }
    rebuildStonePickupMesh();
    m_player.restoreStoneCount(save.stoneCount);

    m_activeSlot = slotIndex;
    m_saveName = save.name; // keep the saved name so later autosaves do not overwrite it
    return true;
}

void DungeonScene::saveActiveSlot()
{
    if (m_activeSlot < 0) return; // no active session: nothing to save

    // The snapshot is built here; SaveSlotAsync() writes it off the game loop
    SaveSystem::SaveData data;
    data.seed = m_currentSeed;
    data.name = m_saveName;
    data.difficulty = (int)m_difficulty;

    const glm::vec3 pos = m_player.camPos();
    data.posX = pos.x;
    data.posY = pos.y;
    data.posZ = pos.z;
    data.yaw = m_player.yaw();
    data.pitch = m_player.pitch();
    data.healthFraction = m_player.healthFraction();
    data.staminaFraction = m_player.staminaFraction();
    data.explored = m_minimapFog.explored();

    data.diariesReadIndices.clear();
    data.diariesReadIndices = m_diaryReadOrder;

    data.torchFuel = m_player.torchFuel();
    data.torchInventoryCount = m_player.torchInventoryCount();
    data.torchTakenIndices.clear();
    for (size_t i = 0; i < m_torchTaken.size(); ++i)
    {
        // Guide torches are regenerated as taken from the seed; restoring them would extinguish them.
        if (m_firstGuideTorchIndex >= 0 && (int)i >= m_firstGuideTorchIndex)
            continue;
        if (m_torchTaken[i]) data.torchTakenIndices.push_back((int)i);
    }

    data.stoneCount = m_player.stoneCount();
    data.stoneTakenIndices.clear();
    for (size_t i = 0; i < m_stonePickedUp.size(); ++i)
        if (m_stonePickedUp[i]) data.stoneTakenIndices.push_back((int)i);

    SaveSystem::SaveSlotAsync(m_activeSlot, data);
}

void DungeonScene::reserveRenderScratchBuffers()
{
    const size_t n = m_geometry.chunks().size();
    m_chunkVisible.clear();
    m_chunkVisible.resize(n, 0);
    m_mainCounts.clear();
    m_mainCounts.reserve(n);
    m_mainOffsets.clear();
    m_mainOffsets.reserve(n);
    m_particleFirsts.clear();
    m_particleFirsts.reserve(n);
    m_particleCounts.clear();
    m_particleCounts.reserve(n);
    m_enemyOccluderScratch.clear();
    m_enemyOccluderScratch.reserve(kEnemyCount + 1); // +1 for the dev dummy (K key)
}

void DungeonScene::processInput(GLFWwindow* window, float deltaTime)
{
    // Enemy positions lag one frame (EnemyAI::update() runs in render())
    // The dev dummy must not block the player
    std::vector<glm::vec3> enemyPositions;
    enemyPositions.reserve(kEnemyCount);
    for (int i = 0; i < kEnemyCount; ++i)
        enemyPositions.push_back(m_enemyAIs[i].position());

    std::vector<glm::vec3> diaryPositions;
    diaryPositions.reserve(m_diaries.size());
    for (const Diaries::PlacedDiary& d : m_diaries)
        diaryPositions.emplace_back((float)d.pocketCellX + 0.5f, 0.0f, (float)d.pocketCellZ + 0.5f);

    m_player.processInput(
        window, deltaTime,
        [this](int x, int z) { return isFloor(x, z); },
        [this](int x, int z) { return wallCornerCut(x, z); },
        [this](int x, int z) { return wallChamferSize(x, z); },
        m_columnCentersXZ,
        m_exitDoorPos,
        enemyPositions,
        diaryPositions,
        diariesReadCount(),
        m_torchWallBase,
        m_torchTaken,
        winReadyToPressE()
    );

    if (m_player.consumeWinSequenceRequest())
    {
        m_winSequenceState = WinSequenceState::Opening;
        m_winSequenceTimer = 0.0f;
    }
    updateWinSequence(deltaTime);

    if (m_player.consumeWinBlockedRequest())
        m_winBlockedMessageTimer = 2.5f;
    if (m_winBlockedMessageTimer > 0.0f)
        m_winBlockedMessageTimer = std::max(0.0f, m_winBlockedMessageTimer - deltaTime);

    m_nearbyDiaryIndex = m_player.nearbyDiaryIndex();

    // processInput() is not called while a reading screen is open, so E opens a diary only from gameplay
    if (m_player.consumeDiaryOpenRequest() && m_nearbyDiaryIndex != -1)
    {
        m_openDiaryIndex = m_nearbyDiaryIndex;
        markDiaryRead(m_openDiaryIndex);
        rebuildDiaryReadWallLookup();
        UiAudio::PlayClick();

        // The key is still held: without this, tickReadingOverlayInput() sees a new press next
        // frame and closes the diary it just opened
        m_overlayEKeyWasDown = true;
    }

    if (m_player.consumeJournalToggleRequest())
    {
        m_journalOpen = true;
        m_journalSelectedIndex = 0;

        // Same for Tab
        m_overlayTabKeyWasDown = true;
    }

    m_nearbyWallTorchIndex = m_player.nearbyTorchIndex();
    if (m_player.consumeTorchPickupRequest() && m_nearbyWallTorchIndex != -1)
    {
        pickupWallTorch(m_nearbyWallTorchIndex);
        // Hide the hint until nearbyTorchIndex() is recomputed
        m_nearbyWallTorchIndex = -1;
    }
    if (m_player.consumeTorchEmptyWarningRequest())
        m_torchEmptyMessageTimer = 2.0f;
    if (m_torchEmptyMessageTimer > 0.0f)
        m_torchEmptyMessageTimer = std::max(0.0f, m_torchEmptyMessageTimer - deltaTime);

    // Stones: auto-pickup and throwing (G)
    // Flight runs in updateThrownStones() from render(), which has this frame's enemy positions
    tryAutoPickupStones();
    if (m_player.consumeThrowStoneRequest())
    {
        const glm::vec3 origin = m_player.camPos() + m_player.getFront() * 0.3f;
        const float kThrowSpeed = 9.0f;
        ThrownStone stone;
        stone.pos = origin;
        stone.vel = m_player.getFront() * kThrowSpeed + glm::vec3(0.0f, 2.0f, 0.0f);
        stone.life = 0.0f;
        m_thrownStones.push_back(stone);
    }

    updateGlitchEffects(deltaTime);

    // Dev tools: L light, K enemy dummy, U undo, P palette cycle
#ifdef HAS_DEV_TOOLS
    if (DevTools::ConsumeSpawnLightKey(window, m_devLightKeyWasDown))
        spawnDevLightAtPlayerView();
    if (DevTools::ConsumeSpawnDummyEnemyKey(window, m_devEnemyKeyWasDown))
        spawnDevDummyEnemyAtPlayerView();
    if (DevTools::ConsumeUndoKey(window, m_devUndoKeyWasDown))
        undoLastDevAction();
    if (DevTools::ConsumeCyclePaletteKey(window, m_devPaletteKeyWasDown))
        cycleDevPalette();
#endif
}

// Diary reading screen and journal
// While isReadingOverlayOpen(), Application calls this instead of processInput().

void DungeonScene::tickReadingOverlayInput(GLFWwindow* window)
{
    const bool eDown     = glfwGetKey(window, GLFW_KEY_E)      == GLFW_PRESS;
    const bool enterDown = glfwGetKey(window, GLFW_KEY_ENTER)  == GLFW_PRESS;
    const bool tabDown   = glfwGetKey(window, GLFW_KEY_TAB)    == GLFW_PRESS;
    const bool upDown    = glfwGetKey(window, GLFW_KEY_UP)     == GLFW_PRESS;
    const bool downDown  = glfwGetKey(window, GLFW_KEY_DOWN)   == GLFW_PRESS;

    // E or Enter, each edge-triggered on its own so releasing one while holding the other does not fire twice.
    const bool confirmPressed =
        (eDown && !m_overlayEKeyWasDown) || (enterDown && !m_overlayEnterKeyWasDown);

    if (m_openDiaryIndex != -1)
    {
        if (confirmPressed)
            m_openDiaryIndex = -1;
    }
    else if (m_journalOpen)
    {
        if (!m_diaries.empty())
        {
            const int count = (int)m_diaries.size();
            if (upDown && !m_overlayUpKeyWasDown)
            {
                m_journalSelectedIndex = (m_journalSelectedIndex - 1 + count) % count;
                UiAudio::PlayHover();
            }
            if (downDown && !m_overlayDownKeyWasDown)
            {
                m_journalSelectedIndex = (m_journalSelectedIndex + 1) % count;
                UiAudio::PlayHover();
            }

            // Entries are listed in read order; unread ones show that they exist, not their text
            if (confirmPressed && journalEntryRead(m_journalSelectedIndex))
            {
                m_openDiaryIndex = m_diaryReadOrder[(size_t)m_journalSelectedIndex];
                UiAudio::PlayClick();
            }
        }

        if (tabDown && !m_overlayTabKeyWasDown)
            m_journalOpen = false;
    }

    m_overlayEKeyWasDown     = eDown;
    m_overlayEnterKeyWasDown = enterDown;
    m_overlayTabKeyWasDown   = tabDown;
    m_overlayUpKeyWasDown    = upDown;
    m_overlayDownKeyWasDown  = downDown;
}

bool DungeonScene::buildReadingOverlayGrid(std::vector<unsigned char>& grid, int cols, int rows) const
{
    grid.assign((size_t)std::max(0, cols) * std::max(0, rows), 0);
    if (!isReadingOverlayOpen()) return false;

    // Readable text is drawn by TextRenderer after ascii.end();
    // the grid holds only the frame and decorations, since the grid font cannot scale
    int boxX0, boxY0, boxX1, boxY1;
    getReadingBoxBounds(cols, rows, boxX0, boxY0, boxX1, boxY1);
    MainMenu::DrawBox(grid, cols, rows, boxX0, boxY0, boxX1, boxY1,
                       /*thickness=*/1, /*filled=*/false, /*seed=*/1);

    if (m_openDiaryIndex != -1 && m_openDiaryIndex < (int)m_diaries.size())
    {
        const Diaries::PlacedDiary& d = m_diaries[(size_t)m_openDiaryIndex];

        MainMenu::PutText(grid, cols, rows, cols / 2 - 4, boxY1 - 1, "E CLOSE");

        // Blood streaks on the top and bottom edges, fixed per story step
        for (int i = 0; i < 5; ++i)
        {
            const int span = std::max(1, (boxX1 - boxX0) - 4);
            const unsigned char dripGlyph = (i % 2 == 0) ? MainMenu::GLYPH_DRIP_BIG : MainMenu::GLYPH_DRIP_SMALL;

            const unsigned int hTop = MainMenu::Hash(d.storyStep * 97 + i, 4242);
            MainMenu::PutGlyph(grid, cols, rows, boxX0 + 2 + (int)(hTop % (unsigned)span), boxY0 + 1, dripGlyph);

            const unsigned int hBot = MainMenu::Hash(d.storyStep * 131 + i, 9001);
            MainMenu::PutGlyph(grid, cols, rows, boxX0 + 2 + (int)(hBot % (unsigned)span), boxY1 - 1, dripGlyph);
        }
    }
    else if (m_journalOpen)
    {
        // The selection highlight is drawn by TextRenderer at the same coordinates as the row label
        MainMenu::PutText(grid, cols, rows, cols / 2 - 10, boxY1 - 1, "TAB CLOSE, E READ");
    }

    return true;
}

void DungeonScene::getReadingBoxBounds(int cols, int rows, int& x0, int& y0, int& x1, int& y1) const
{
    // Frame bounds, shared with Application to place TextRenderer text inside
    x0 = cols / 2 - 38; x1 = cols / 2 + 38;
    y0 = rows / 2 - 20; y1 = rows / 2 + 20;
}

void DungeonScene::spawnEnemiesAcrossMap(uint32_t seed)
{
    // One enemy per map sector so they do not clump, each at least kMinDistFromPlayerSpawn from the player's start
    std::mt19937 rng(seed ^ 0x5EED0004u); // own RNG stream, independent of the other generators

    const glm::vec3 playerSpawnPos(6.5f, 0.0f, 6.5f);
    const float kMinDistFromPlayerSpawn = 8.0f;
    const int kAttemptsPerSector = 200;

    // Near-square grid with at least kEnemyCount sectors (7 enemies -> 3x3, two left empty)
    const int gridCols = (int)std::ceil(std::sqrt((double)std::max(1, kEnemyCount)));
    const int gridRows = (int)std::ceil((double)std::max(1, kEnemyCount) / (double)gridCols);

    const int sectorW = std::max(1, m_mapW / gridCols);
    const int sectorH = std::max(1, m_mapH / gridRows);

    for (int i = 0; i < kEnemyCount; ++i)
    {
        const int sx = i % gridCols;
        const int sz = (i / gridCols) % gridRows;

        const int x0 = sx * sectorW;
        // The last column absorbs the remainder
        const int x1 = (sx == gridCols - 1) ? m_mapW : x0 + sectorW;
        const int z0 = sz * sectorH;
        const int z1 = (sz == gridRows - 1) ? m_mapH : z0 + sectorH;

        std::uniform_int_distribution<int> distX(x0, std::max(x0, x1 - 1));
        std::uniform_int_distribution<int> distZ(z0, std::max(z0, z1 - 1));

        glm::vec3 chosen(x0 + 0.5f, 0.0f, z0 + 0.5f); // fallback if nothing is found
        bool found = false;

        for (int attempt = 0; attempt < kAttemptsPerSector && !found; ++attempt)
        {
            const int cx = distX(rng);
            const int cz = distZ(rng);
            if (!isFloor(cx, cz))
                continue;

            const glm::vec3 candidate(cx + 0.5f, 0.0f, cz + 0.5f);
            if (glm::length(candidate - playerSpawnPos) < kMinDistFromPlayerSpawn)
                continue;

            chosen = candidate;
            found = true;
        }

        if (!found)
        {
            // Fallback scan in order: deterministic, never leaves an enemy on a wall
            for (int cz = z0; cz < z1 && !found; ++cz)
            {
                for (int cx = x0; cx < x1 && !found; ++cx)
                {
                    if (!isFloor(cx, cz))
                        continue;
                    chosen = glm::vec3(cx + 0.5f, 0.0f, cz + 0.5f);
                    found = true;
                }
            }
        }

        m_enemyAIs[i].setPosition(chosen);
        m_enemies[i].setPosition(chosen);
    }
}

bool DungeonScene::isEnemyVisibleToPlayer(const glm::vec3& enemyPos) const
{
    const glm::vec3 playerPos = m_player.camPos();

    glm::vec3 toEnemy = enemyPos - playerPos;
    toEnemy.y = 0.0f;
    const float dist = glm::length(toEnemy);

    // About the AI's running sight range: the player should not see farther than the enemy
    const float kPlayerSightRadius = 14.0f;
    if (dist > kPlayerSightRadius)
        return false;

    if (dist > 0.001f)
    {
        const glm::vec3 toEnemyDir = toEnemy / dist;

        glm::vec3 front = m_player.getFront();
        front.y = 0.0f;
        const float frontLen = glm::length(front);
        if (frontLen > 0.0001f)
        {
            front /= frontLen;

            // 63 degree vertical FOV is about 90 degrees horizontal on widescreen, plus peripheral margin
            const float kPlayerFOVHalfAngleDeg = 45.0f;
            const float cosHalfFOV = std::cos(glm::radians(kPlayerFOVHalfAngleDeg));
            if (glm::dot(front, toEnemyDir) < cosHalfFOV)
                return false;
        }
    }

    // Same test as AI perception, with the camera as the observer
    const glm::vec2 fromXZ(playerPos.x, playerPos.z);
    const glm::vec2 toXZ(enemyPos.x, enemyPos.z);
    return LineOfSight::HasLineOfSight(
        fromXZ, toXZ,
        m_mapW, m_mapH,
        m_map, m_wallCornerCuts, m_diagonalChainMask,
        m_columnCentersXZ, Columns::kColumnRadius);
}

void DungeonScene::spawnDevLightAtPlayerView()
{
    // devLightPos[8] in the shaders
    if ((int)m_devSpotlights.size() >= 8)
        return;

    DevSpotlight light;
    light.position = m_player.camPos();
    light.direction = glm::normalize(m_player.getFront());
    m_devSpotlights.push_back(light);

    DevAction action;
    action.type = DevActionType::SpawnLight;
    m_devActionHistory.push_back(action);
}

void DungeonScene::spawnDevDummyEnemyAtPlayerView()
{
    if (!m_testEnemy.isLoaded())
        return;

    DevAction action;
    action.type = DevActionType::SpawnDummyEnemy;
    action.dummyWasActiveBefore = m_devDummyEnemyActive;
    m_devActionHistory.push_back(action);

    // A little ahead, not inside the player
    const glm::vec3 front = glm::normalize(m_player.getFront());
    const glm::vec3 spawnPos = m_player.camPos() + front * 2.0f;

    m_enemyAI.setPosition(spawnPos);
    m_testEnemy.setPosition(spawnPos);
    // Faces the player; same axis convention as EnemyAI.cpp
    const float yawDeg = glm::degrees(std::atan2(-front.x, -front.z));
    m_testEnemy.setYawDegrees(yawDeg);
    m_testEnemy.setState(EnemyCharacter::State::Idle);

    m_devDummyEnemyActive = true;
}

void DungeonScene::undoLastDevAction()
{
    if (m_devActionHistory.empty())
        return;

    const DevAction last = m_devActionHistory.back();
    m_devActionHistory.pop_back();

    if (last.type == DevActionType::SpawnLight)
    {
        if (!m_devSpotlights.empty())
            m_devSpotlights.pop_back();
    }
    else
    {
        m_devDummyEnemyActive = last.dummyWasActiveBefore;
    }
}

void DungeonScene::cycleDevPalette()
{
    // Cycles -1 (zone palette) -> 0 .. kPaletteCount-1 -> -1
    m_devPaletteOverride++;
    if (m_devPaletteOverride >= Zoning::kPaletteCount)
        m_devPaletteOverride = -1;
}

void DungeonScene::processMouse(double xpos, double ypos)
{
    m_player.processMouse(xpos, ypos);
}

void DungeonScene::tickPauseCameraIdle(float deltaTime)
{
    m_player.tickPauseCameraIdle(deltaTime);
}

glm::vec3 DungeonScene::getFront() const
{
    return m_player.getFront();
}

bool DungeonScene::init(){
    m_player.init();

    // For the deceptive glitch sounds; not tied to an enemy
    m_glitchAudio.init();

    // One-time GL setup, independent of any map
    const std::string sceneVertSrc = ShaderLoader::LoadSource("assets/shaders/scene.vert");
    const std::string sceneFragSrc = ShaderLoader::LoadSource("assets/shaders/scene.frag");
    GLuint vs = compileShader(GL_VERTEX_SHADER, sceneVertSrc.c_str());
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, sceneFragSrc.c_str());
    m_program = linkProgram(vs, fs);
    cacheUniformLocations();

    // Without the texture, walls fall back to procedural bricks
    m_wallTex.load(WallTexture::kDefaultName);

    // Wall torch lit mask (R8, kTorchLitMaskDim squared)
    // Filled per map by placeTorches()/resetTorchLitMask()
    {
        std::vector<unsigned char> allLit((size_t)kTorchLitMaskDim * kTorchLitMaskDim, 255);
        glGenTextures(1, &m_torchLitMaskTex);
        glBindTexture(GL_TEXTURE_2D, m_torchLitMaskTex);
        // Single-byte rows: the default alignment of 4 would misalign them
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kTorchLitMaskDim, kTorchLitMaskDim,
                     0, GL_RED, GL_UNSIGNED_BYTE, allLit.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    m_compass.create();
    m_debugMapOverlay.create();
    m_playerTorch.init();

    // Enemy: skinned model with its own shader, lit like the scene
    {
        const std::string enemyVertSrc = ShaderLoader::LoadSource("assets/shaders/enemy.vert");
        const std::string enemyFragSrc = ShaderLoader::LoadSource("assets/shaders/enemy.frag");
        GLuint evs = compileShader(GL_VERTEX_SHADER, enemyVertSrc.c_str());
        GLuint efs = compileShader(GL_FRAGMENT_SHADER, enemyFragSrc.c_str());
        m_enemyProgram = linkProgram(evs, efs);
        cacheEnemyUniformLocations();
    }

    // One model shared by all enemies. A missing file means no enemies are drawn
    // Loaded without the diffuse texture (see hasDiffuseTex in render())
    const bool enemyModelLoaded = m_enemySharedModel.load("assets/models/enemy/the_wrapped.glb");
    if (!enemyModelLoaded)
    {
        std::fprintf(stderr,
            "[EnemyCharacter] no model at assets/models/enemy/the_wrapped.glb: "
            "enemies will not render until this file is provided (see EnemyCharacter.h).\n");
    }

    auto assignClipNames = [](EnemyCharacter& c)
    {
        c.setClipName(EnemyCharacter::State::Idle,     "Idle_Watchful");
        c.setClipName(EnemyCharacter::State::Walk,     "Walk_Nervous");
        c.setClipName(EnemyCharacter::State::Run,      "Run_Frantic");
        c.setClipName(EnemyCharacter::State::Attack,   "Attack_Lunge");
        c.setClipName(EnemyCharacter::State::WallSlam, "Wall_slam");
        c.setClipName(EnemyCharacter::State::Scream,   "Scream");
    };

    // The dev dummy does not think; spawnDevDummyEnemyAtPlayerView() places it
    m_testEnemy.attachSharedModel(enemyModelLoaded ? &m_enemySharedModel : nullptr);
    if (m_testEnemy.isLoaded())
        assignClipNames(m_testEnemy);

    // The glitch silhouette, driven by updateGlitchEffects() instead of AI
    m_glitchGhost.attachSharedModel(enemyModelLoaded ? &m_enemySharedModel : nullptr);
    if (m_glitchGhost.isLoaded())
        assignClipNames(m_glitchGhost);

    for (int i = 0; i < kEnemyCount; ++i)
    {
        m_enemies[i].attachSharedModel(enemyModelLoaded ? &m_enemySharedModel : nullptr);
        if (m_enemies[i].isLoaded())
            assignClipNames(m_enemies[i]);
    }

    // The first map only serves as the menu background
    generateMenuBackgroundMaze();

    return m_program != 0 && m_compass.isReady();
}

void DungeonScene::cacheUniformLocations()
{
    if (!m_program) return;
    m_uniView             = glGetUniformLocation(m_program, "view");
    m_uniProjection        = glGetUniformLocation(m_program, "projection");
    m_uniCamPos            = glGetUniformLocation(m_program, "camPos");
    m_uniPlayerLightPos       = glGetUniformLocation(m_program, "playerLightPos");
    m_uniPlayerLightDir       = glGetUniformLocation(m_program, "playerLightDir");
    m_uniPlayerLightColor     = glGetUniformLocation(m_program, "playerLightColor");
    m_uniPlayerLightIntensity = glGetUniformLocation(m_program, "playerLightIntensity");
    m_uniDevLightPos          = glGetUniformLocation(m_program, "devLightPos");
    m_uniDevLightDir          = glGetUniformLocation(m_program, "devLightDir");
    m_uniDevLightCount        = glGetUniformLocation(m_program, "devLightCount");
    m_uniDevPaletteOverride   = glGetUniformLocation(m_program, "devPaletteOverride");
    m_uniIsViewmodelDraw      = glGetUniformLocation(m_program, "uIsViewmodelDraw");
    m_uniViewmodelTorchFuel   = glGetUniformLocation(m_program, "uViewmodelTorchFuel");
    m_uniGlitchTorchIndex     = glGetUniformLocation(m_program, "uGlitchTorchIndex");
    m_uniGlitchTorchUntilTime = glGetUniformLocation(m_program, "uGlitchTorchUntilTime");
    m_uniInvView              = glGetUniformLocation(m_program, "uInvView");
    m_uniViewmodelSway        = glGetUniformLocation(m_program, "uViewmodelSway");
    m_uniTime              = glGetUniformLocation(m_program, "uTime");
    m_uniRenderDistance    = glGetUniformLocation(m_program, "renderDistance");
    m_uniDepthOnly         = glGetUniformLocation(m_program, "uDepthOnly");
    m_uniAlpha             = glGetUniformLocation(m_program, "uAlpha");
    m_uniWallTex           = glGetUniformLocation(m_program, "wallTex");
    m_uniWallTexEnabled    = glGetUniformLocation(m_program, "wallTexEnabled");
    m_uniWallTexContrast   = glGetUniformLocation(m_program, "wallTexContrast");
    m_uniTorchLitMask      = glGetUniformLocation(m_program, "uTorchLitMask");
    m_uniTorchPos          = glGetUniformLocation(m_program, "torchPos");
    m_uniTorchColor        = glGetUniformLocation(m_program, "torchColor");
    m_uniTorchIntensity    = glGetUniformLocation(m_program, "torchIntensity");
    m_uniTorchCount        = glGetUniformLocation(m_program, "torchCount");
    m_uniTorchShadowRow    = glGetUniformLocation(m_program, "torchShadowRow");
    m_uniTorchShadowMap    = glGetUniformLocation(m_program, "uTorchShadowMap");

    m_uniEnemyOccluderPosXZ  = glGetUniformLocation(m_program, "enemyOccluderPosXZ");
    m_uniEnemyOccluderCount  = glGetUniformLocation(m_program, "enemyOccluderCount");
    m_uniEnemyOccluderRadius = glGetUniformLocation(m_program, "enemyOccluderRadius");
    m_uniEnemyOccluderHeight = glGetUniformLocation(m_program, "enemyOccluderHeight");
}

void DungeonScene::cacheEnemyUniformLocations()
{
    if (!m_enemyProgram) return;
    m_uEnemyView                = glGetUniformLocation(m_enemyProgram, "view");
    m_uEnemyProjection           = glGetUniformLocation(m_enemyProgram, "projection");
    m_uEnemyModel                = glGetUniformLocation(m_enemyProgram, "model");
    m_uEnemyBoneMatrices         = glGetUniformLocation(m_enemyProgram, "boneMatrices");
    m_uEnemyCamPos               = glGetUniformLocation(m_enemyProgram, "camPos");
    m_uEnemyTime                 = glGetUniformLocation(m_enemyProgram, "uTime");
    m_uEnemyPlayerLightPos       = glGetUniformLocation(m_enemyProgram, "playerLightPos");
    m_uEnemyPlayerLightDir       = glGetUniformLocation(m_enemyProgram, "playerLightDir");
    m_uEnemyPlayerLightColor     = glGetUniformLocation(m_enemyProgram, "playerLightColor");
    m_uEnemyPlayerLightIntensity = glGetUniformLocation(m_enemyProgram, "playerLightIntensity");
    m_uEnemyDevLightPos          = glGetUniformLocation(m_enemyProgram, "devLightPos");
    m_uEnemyDevLightDir          = glGetUniformLocation(m_enemyProgram, "devLightDir");
    m_uEnemyDevLightCount        = glGetUniformLocation(m_enemyProgram, "devLightCount");
    m_uEnemyDevPaletteOverride   = glGetUniformLocation(m_enemyProgram, "devPaletteOverride");
    m_uEnemyTorchPos             = glGetUniformLocation(m_enemyProgram, "torchPos");
    m_uEnemyTorchColor           = glGetUniformLocation(m_enemyProgram, "torchColor");
    m_uEnemyTorchIntensity       = glGetUniformLocation(m_enemyProgram, "torchIntensity");
    m_uEnemyTorchCount           = glGetUniformLocation(m_enemyProgram, "torchCount");
    m_uEnemyTorchShadowRow       = glGetUniformLocation(m_enemyProgram, "torchShadowRow");
    m_uEnemyTorchShadowMap       = glGetUniformLocation(m_enemyProgram, "uTorchShadowMap");
    m_uEnemyRenderDistance       = glGetUniformLocation(m_enemyProgram, "renderDistance");
    m_uEnemyDiffuseTex           = glGetUniformLocation(m_enemyProgram, "diffuseTex");
    m_uEnemyHasDiffuseTex        = glGetUniformLocation(m_enemyProgram, "hasDiffuseTex");
}

void DungeonScene::shutdown()
{
    m_player.shutdown();
    m_compass.destroy();
    m_debugMapOverlay.destroy();

    m_geometry.destroy();
    m_torchShadowMap.destroy();

    m_playerTorch.destroy();

    m_testEnemy.destroy();
    for (int i = 0; i < kEnemyCount; ++i)
        m_enemies[i].destroy();
    // The only owner of the shared model's GPU resources
    m_enemySharedModel.destroy();
    if (m_enemyProgram)
    {
        glDeleteProgram(m_enemyProgram);
        m_enemyProgram = 0;
    }

    m_minimapFog.destroy();

    m_wallTex.destroy();

    if (m_torchLitMaskTex)
    {
        glDeleteTextures(1, &m_torchLitMaskTex);
        m_torchLitMaskTex = 0;
    }

    // Created lazily, may not exist
    if (m_stonePickupVao)
    {
        glDeleteVertexArrays(1, &m_stonePickupVao);
        glDeleteBuffers(1, &m_stonePickupVbo);
        glDeleteBuffers(1, &m_stonePickupEbo);
        m_stonePickupVao = m_stonePickupVbo = m_stonePickupEbo = 0;
    }
    if (m_thrownStoneVao)
    {
        glDeleteVertexArrays(1, &m_thrownStoneVao);
        glDeleteBuffers(1, &m_thrownStoneVbo);
        glDeleteBuffers(1, &m_thrownStoneEbo);
        m_thrownStoneVao = m_thrownStoneVbo = m_thrownStoneEbo = 0;
    }
    if (m_landmarkPropVao)
    {
        glDeleteVertexArrays(1, &m_landmarkPropVao);
        glDeleteBuffers(1, &m_landmarkPropVbo);
        glDeleteBuffers(1, &m_landmarkPropEbo);
        m_landmarkPropVao = m_landmarkPropVbo = m_landmarkPropEbo = 0;
    }
    for (GLuint* vao : { &m_exitDoorVao, &m_donutVao })
    {
        if (*vao)
            glDeleteVertexArrays(1, vao);
        *vao = 0;
    }
    for (GLuint* buffer : { &m_exitDoorVbo, &m_exitDoorEbo, &m_donutVbo, &m_donutEbo })
    {
        if (*buffer)
            glDeleteBuffers(1, buffer);
        *buffer = 0;
    }
    m_donutIndexCount = 0;
    m_exitDoorBuiltOnce = false;

    if (m_program)
    {
        glDeleteProgram(m_program);

        m_program = 0;
    }
}

void DungeonScene::uploadDevLights(GLint posLoc, GLint dirLoc, GLint countLoc) const
{
    glm::vec3 positions[8];
    glm::vec3 directions[8];
    const int count = std::min((int)m_devSpotlights.size(), 8);
    for (int i = 0; i < count; ++i)
    {
        positions[i] = m_devSpotlights[(size_t)i].position;
        directions[i] = m_devSpotlights[(size_t)i].direction;
    }
    if (count > 0)
    {
        glUniform3fv(posLoc, count, glm::value_ptr(positions[0]));
        glUniform3fv(dirLoc, count, glm::value_ptr(directions[0]));
    }
    glUniform1i(countLoc, count);
}

void DungeonScene::render(int viewportWidth, int viewportHeight, bool gameplayActive)
{
    m_minimapFog.updateMinimap(
        m_mapW, m_mapH, m_map, m_player.camPos(), m_player.yaw(),
        [this](int x, int z) { return isWall(x, z); },
        m_torchCellLookup, m_guideTorchCellLookup, m_diaryReadWallLookup);

    // Overlays drawn after the previous frame may leave blending on
    // Opaque geometry must not blend: flames and handles write alpha 0 (an ASCII edge mask) and would turn black
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    const glm::vec3 front = getFront();
    const glm::vec3 renderCamPos = m_player.getCameraRenderPosition();
    const glm::mat4 view = glm::lookAt(renderCamPos, renderCamPos + front, m_player.getCameraRenderUp());

    // 16 for a regular player; +/- scale it in noclip (DevTools.h)
    const float kBaseRenderDistance = 16.0f;
    m_currentRenderDistance = kBaseRenderDistance * getViewDistanceMultiplier();

    const float aspect = (float)viewportWidth / (float)viewportHeight;
    const glm::mat4 proj = glm::perspective(glm::radians(63.0f), aspect, nearPlane(), farPlane());
    const float now = (float)glfwGetTime();

    glUseProgram(m_program);
    glUniformMatrix4fv(m_uniView, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(m_uniProjection, 1, GL_FALSE, glm::value_ptr(proj));
    glUniform3fv(m_uniCamPos, 1, glm::value_ptr(renderCamPos));
    glUniformMatrix4fv(m_uniInvView, 1, GL_FALSE, glm::value_ptr(glm::inverse(view)));
    glUniform1f(m_uniTime, now);
    glUniform1f(m_uniRenderDistance, m_currentRenderDistance);
    glUniform1i(m_uniDepthOnly, 0);
    glUniform1f(m_uniAlpha, 1.0f);

    // The hand light starts at the camera: the viewmodel flame can end up inside a wall when the player hugs it torch
    // Blend fades it with the raise/lower animation

    const glm::vec3 handLightColor(1.0f, 0.60f, 0.15f);
    const float handLightIntensity = m_player.torchBlend();
    glUniform3fv(m_uniPlayerLightPos, 1, glm::value_ptr(renderCamPos));
    glUniform3fv(m_uniPlayerLightDir, 1, glm::value_ptr(glm::normalize(front)));
    glUniform3fv(m_uniPlayerLightColor, 1, glm::value_ptr(handLightColor));
    glUniform1f(m_uniPlayerLightIntensity, handLightIntensity);

    uploadDevLights(m_uniDevLightPos, m_uniDevLightDir, m_uniDevLightCount);
    glUniform1i(m_uniDevPaletteOverride, m_devPaletteOverride);

    // -1 = no torch is trembling; real indices are >= 0
    glUniform1f(m_uniGlitchTorchIndex, (float)m_torchGlitchIndex);
    glUniform1f(m_uniGlitchTorchUntilTime, (float)m_torchGlitchUntilTime);

    // Texture units:
    // 0 = torch shadow map
    // 1 = wall texture
    // 2 = torch lit mask
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_torchShadowMap.texture());
    glUniform1i(m_uniTorchShadowMap, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_wallTex.id());
    glUniform1i(m_uniWallTex, 1);
    glUniform1f(m_uniWallTexEnabled, m_wallTex.id() != 0 ? 1.0f : 0.0f);
    glUniform1f(m_uniWallTexContrast, m_wallTex.contrast);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_torchLitMaskTex);
    glUniform1i(m_uniTorchLitMask, 2);
    glActiveTexture(GL_TEXTURE0);

    m_lighting.update(m_player.camPos(), m_currentRenderDistance, MAX_ACTIVE_TORCHES,
                      m_torchFlamePos, m_torchColor, m_torchIntensity, now);
    const int torchCount = m_lighting.activeCount();
    auto uploadTorches = [&](GLint posLoc, GLint colorLoc, GLint intensityLoc, GLint rowLoc, GLint countLoc) {
        if (torchCount > 0)
        {
            glUniform3fv(posLoc, torchCount, glm::value_ptr(m_lighting.activePositions()[0]));
            glUniform3fv(colorLoc, torchCount, glm::value_ptr(m_lighting.activeColors()[0]));
            glUniform1fv(intensityLoc, torchCount, m_lighting.activeIntensities().data());
            glUniform1iv(rowLoc, torchCount, m_lighting.activeIndices().data());
        }
        glUniform1i(countLoc, torchCount);
    };
    uploadTorches(m_uniTorchPos, m_uniTorchColor, m_uniTorchIntensity, m_uniTorchShadowRow, m_uniTorchCount);

    // Enemy shadow casters. Each one costs a test per lit pixel and torch, so only enemies that can be within reach of a visible torch are uploaded (mewomeow)
    {
        const glm::vec3 cam = m_player.camPos();
        const float cullRadius = m_currentRenderDistance + 2.0f;
        auto withinCullRadius = [&](const glm::vec3& p) {
            const float dx = p.x - cam.x, dz = p.z - cam.z;
            return dx * dx + dz * dz <= cullRadius * cullRadius;
        };

        m_enemyOccluderScratch.clear();
        for (int i = 0; i < kEnemyCount; ++i)
        {
            if (m_enemies[i].isLoaded() && withinCullRadius(m_enemyAIs[i].position()))
            {
                const glm::vec3 p = m_enemyAIs[i].position();
                m_enemyOccluderScratch.emplace_back(p.x, p.z);
            }
        }
        if (m_devDummyEnemyActive && m_testEnemy.isLoaded() && withinCullRadius(m_testEnemy.position()))
        {
            const glm::vec3 p = m_testEnemy.position();
            m_enemyOccluderScratch.emplace_back(p.x, p.z);
        }

        const int occluderCount = std::min((int)m_enemyOccluderScratch.size(), 8); // MAX_ENEMY_OCCLUDERS
        if (occluderCount > 0)
            glUniform2fv(m_uniEnemyOccluderPosXZ, occluderCount, glm::value_ptr(m_enemyOccluderScratch[0]));
        glUniform1i(m_uniEnemyOccluderCount, occluderCount);

        // Shoulder width and in-world height (bind pose 1.75 * kModelScale 0.6)
        glUniform1f(m_uniEnemyOccluderRadius, 0.30f);
        glUniform1f(m_uniEnemyOccluderHeight, 1.05f);
    }

    // Chunk culling: PVS from the camera's chunk, then distance, then frustum
    // The distance slack keeps chunks from popping while they are still fading in
    glm::vec4 frustumPlanes[6];
    Culling::ExtractFrustumPlanes(proj * view, frustumPlanes);
    const float cullDistance = m_currentRenderDistance + (float)SceneGeometry::kChunkSize;
    const float cullDistanceSq = cullDistance * cullDistance;

    int camChunk = -1;
    if (m_geometry.pvsEnabled() && m_geometry.chunksX() > 0 && m_mapW > 0 && m_mapH > 0)
    {
        const int camCellX = std::clamp((int)std::floor(renderCamPos.x), 0, m_mapW - 1);
        const int camCellZ = std::clamp((int)std::floor(renderCamPos.z), 0, m_mapH - 1);
        camChunk = (camCellZ / SceneGeometry::kChunkSize) * m_geometry.chunksX() + camCellX / SceneGeometry::kChunkSize;
        if (camChunk >= (int)m_geometry.pvsMask().size())
            camChunk = -1;
    }
    const uint64_t camPvsMask = camChunk >= 0 ? m_geometry.pvsMask()[(size_t)camChunk] : ~0ull;

    const std::vector<GeoChunk>& chunks = m_geometry.chunks();
    m_mainCounts.clear();
    m_mainOffsets.clear();
    m_particleFirsts.clear();
    m_particleCounts.clear();
    int visibleChunks = 0, visibleTriangles = 0, visibleParticles = 0;

    for (size_t i = 0; i < chunks.size(); ++i)
    {
        const GeoChunk& c = chunks[i];
        const glm::vec3 closest = glm::clamp(renderCamPos, c.aabbMin, c.aabbMax);
        const glm::vec3 delta = closest - renderCamPos;
        const bool visible = (c.mainIndexCount > 0 || c.particleCount > 0)
            && (camChunk < 0 || ((camPvsMask >> i) & 1ull))
            && glm::dot(delta, delta) <= cullDistanceSq
            && Culling::AabbInFrustum(c.aabbMin, c.aabbMax, frustumPlanes);
        m_chunkVisible[i] = visible ? 1 : 0;
        if (!visible)
            continue;

        ++visibleChunks;
        if (c.mainIndexCount > 0)
        {
            m_mainCounts.push_back(c.mainIndexCount);
            m_mainOffsets.push_back((const GLvoid*)(uintptr_t)(c.mainIndexFirst * sizeof(GLuint)));
            visibleTriangles += c.mainIndexCount / 3;
        }
        if (c.particleCount > 0)
        {
            m_particleFirsts.push_back(c.particleFirst);
            m_particleCounts.push_back(c.particleCount);
            visibleParticles += c.particleCount;
        }
    }

    m_lastVisibleTriangles = visibleTriangles;
    m_lastVisibleChunkCount = visibleChunks;
    m_lastVisibleParticles = visibleParticles;
    m_lastActiveTorchCount = torchCount;

    if (!m_mainCounts.empty())
    {
        glBindVertexArray(m_geometry.vao());

        // Depth pre-pass: the lighting shader then runs once per visible pixel instead of once per
        // overlapping wall. With depth writes off, the shading pass keeps early-Z despite discard.
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glUniform1i(m_uniDepthOnly, 1);
        glMultiDrawElements(GL_TRIANGLES, m_mainCounts.data(), GL_UNSIGNED_INT,
                            m_mainOffsets.data(), (GLsizei)m_mainCounts.size());
        glUniform1i(m_uniDepthOnly, 0);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glMultiDrawElements(GL_TRIANGLES, m_mainCounts.data(), GL_UNSIGNED_INT,
                            m_mainOffsets.data(), (GLsizei)m_mainCounts.size());
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);

        glBindVertexArray(0);
    }

    // Small dynamic meshes share the scene program; matId 0 = plain diffuse geometry
    auto drawMesh = [](GLuint vao, int indexCount) {
        if (indexCount <= 0)
            return;
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    };
    drawMesh(m_stonePickupVao, m_stonePickupIndexCount);
    drawMesh(m_thrownStoneVao, m_thrownStoneIndexCount);
    drawMesh(m_landmarkPropVao, m_landmarkPropIndexCount);
    drawMesh(m_exitDoorVao, m_exitDoorIndexCount);

    if (m_donutIndexCount > 0 && m_winSequenceState == WinSequenceState::Spinning)
    {
        if (m_donutAlpha < 0.999f)
        {
            // Depth first, then blend only the nearest surface:
            // without it the donut's far side would show through its near side. Destination alpha (the ASCII edge mask) is kept
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            drawMesh(m_donutVao, m_donutIndexCount);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            glUniform1f(m_uniAlpha, m_donutAlpha);
            drawMesh(m_donutVao, m_donutIndexCount);
            glUniform1f(m_uniAlpha, 1.0f);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glDisable(GL_BLEND);
        }
        else
        {
            drawMesh(m_donutVao, m_donutIndexCount);
        }
    }

    if (!m_particleFirsts.empty())
    {
        glEnable(GL_PROGRAM_POINT_SIZE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glBindVertexArray(m_geometry.particleVao());
        glMultiDrawArrays(GL_POINTS, m_particleFirsts.data(), m_particleCounts.data(),
                          (GLsizei)m_particleFirsts.size());
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_PROGRAM_POINT_SIZE);
    }

    updateViewmodelSway();

    bool anyEnemyToDraw = m_devDummyEnemyActive && m_testEnemy.isLoaded();
    for (int i = 0; i < kEnemyCount && !anyEnemyToDraw; ++i)
        anyEnemyToDraw = m_enemies[i].isLoaded();
    if (!anyEnemyToDraw)
        anyEnemyToDraw = m_silhouetteActiveTimer > 0.0f && m_glitchGhost.isLoaded();

    if (anyEnemyToDraw)
    {
        glUseProgram(m_enemyProgram);
        glUniformMatrix4fv(m_uEnemyView, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(m_uEnemyProjection, 1, GL_FALSE, glm::value_ptr(proj));
        glUniform3fv(m_uEnemyCamPos, 1, glm::value_ptr(renderCamPos));
        glUniform1f(m_uEnemyTime, now);
        glUniform1f(m_uEnemyRenderDistance, m_currentRenderDistance);

        glUniform3fv(m_uEnemyPlayerLightPos, 1, glm::value_ptr(renderCamPos));
        glUniform3fv(m_uEnemyPlayerLightDir, 1, glm::value_ptr(glm::normalize(front)));
        glUniform3fv(m_uEnemyPlayerLightColor, 1, glm::value_ptr(handLightColor));
        glUniform1f(m_uEnemyPlayerLightIntensity, handLightIntensity);

        uploadDevLights(m_uEnemyDevLightPos, m_uEnemyDevLightDir, m_uEnemyDevLightCount);
        glUniform1i(m_uEnemyDevPaletteOverride, m_devPaletteOverride);
        uploadTorches(m_uEnemyTorchPos, m_uEnemyTorchColor, m_uEnemyTorchIntensity,
                      m_uEnemyTorchShadowRow, m_uEnemyTorchCount);

        // Unit 0 still holds the torch shadow map. The model is drawn with vertex colors only:
        // enabling its diffuse texture changes the enemy's look
        glUniform1i(m_uEnemyTorchShadowMap, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1i(m_uEnemyDiffuseTex, 1);
        glUniform1f(m_uEnemyHasDiffuseTex, 0.0f);

        const double nowT = glfwGetTime();
        const float enemyDt = m_enemyUpdateInit ? (float)std::min(nowT - m_lastEnemyUpdateTime, 0.1) : 0.0f;
        m_lastEnemyUpdateTime = nowT;
        m_enemyUpdateInit = true;

        if (gameplayActive)
        {
            // Same interval as the AI's own perception (kPerceptionInterval in EnemyAI.cpp)
            const float kPlayerVisibilityCheckInterval = 0.2f;

            for (int i = 0; i < kEnemyCount; ++i)
            {
                if (!m_enemies[i].isLoaded())
                    continue;

                m_playerVisibilityCheckTimers[i] += enemyDt;
                if (m_playerVisibilityCheckTimers[i] >= kPlayerVisibilityCheckInterval)
                {
                    m_playerVisibilityCheckTimers[i] = 0.0f;
                    m_cachedPlayerCanSeeEnemy[i] = isEnemyVisibleToPlayer(m_enemyAIs[i].position());
                }

                m_enemyAIs[i].update(enemyDt, renderCamPos,
                                     m_player.isRunning(), m_player.isMoving(),
                                     m_player.invisibleToEnemy(), m_cachedPlayerCanSeeEnemy[i],
                                     m_player.lightLevel(), m_enemies[i]);

                if (m_enemyAIs[i].consumeJustCaughtPlayer())
                {
                    m_player.applyCaughtDebuff();
                    m_player.applyDamage(25.0f); // four hits kill at full health
                }
            }

            // After all enemies moved: stone collisions need this frame's positions
            updateThrownStones(enemyDt);
        }

        for (int i = 0; i < kEnemyCount; ++i)
            if (m_enemies[i].isLoaded())
                m_enemies[i].draw(m_uEnemyModel, m_uEnemyBoneMatrices);

        // The dev dummy does not think, but its clip clock must still advance
        if (m_devDummyEnemyActive)
        {
            if (gameplayActive)
                m_testEnemy.update(enemyDt);
            m_testEnemy.draw(m_uEnemyModel, m_uEnemyBoneMatrices);
        }

        if (m_silhouetteActiveTimer > 0.0f && m_glitchGhost.isLoaded())
            m_glitchGhost.draw(m_uEnemyModel, m_uEnemyBoneMatrices);

        glUseProgram(m_program);
    }

    // Hand torch last:
    // it has no depth test, so anything drawn after it would cover it.
    // The enemy pass unbound texture unit 1, so the wall texture is restored first
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_wallTex.id());
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(m_uniIsViewmodelDraw, 1);
    glUniform1f(m_uniViewmodelTorchFuel, m_player.torchFuel());
    m_playerTorch.draw();
    glUniform1i(m_uniIsViewmodelDraw, 0);

    glBindTexture(GL_TEXTURE_2D, 0);
}

// Viewmodel sway: turning inertia, idle breathing and a walk/run bob, uploaded as uViewmodelSway (scene.vert)
void DungeonScene::updateViewmodelSway()
{
    const double nowTime = glfwGetTime();
    // Clamped so a pause or a hitch cannot make the smoothing snap
    const float dt = m_playerTorchLagInit ? (float)std::min(nowTime - m_playerTorchLastSwayTime, 0.1) : 0.0f;
    m_playerTorchLastSwayTime = nowTime;

    const float currentYaw = m_player.yaw();
    if (!m_playerTorchLagInit)
    {
        m_playerTorchLagYaw = currentYaw;
        m_playerTorchLagInit = true;
    }

    // Shortest angular difference across the +-180 wrap
    float yawDiff = std::fmod(currentYaw - m_playerTorchLagYaw + 540.0f, 360.0f) - 180.0f;

    const float kLagSpeed = 8.0f;          // 1/s, lower = longer lag on sharp turns
    const float kSwayScale = -0.010f;      // negative: a left turn pushes the torch right
    const float kMaxSway = 0.08f;          // keeps a mouse flick from throwing it out of view
    const float kSwayVisualSpeed = 25.0f;  // 1/s, smooths jumps of the clamped target
    m_playerTorchLagYaw += yawDiff * std::min(1.0f, dt * kLagSpeed);
    const float targetSway = std::clamp(yawDiff * kSwayScale, -kMaxSway, kMaxSway);
    m_playerTorchSwayVisual += (targetSway - m_playerTorchSwayVisual) * std::min(1.0f, dt * kSwayVisualSpeed);

    const float idleBobX = std::sin((float)nowTime * 1.3f) * 0.006f;
    const float idleBobY = std::sin((float)nowTime * 1.7f + 1.0f) * 0.005f;

    // The bob phase advances with movement, so it follows footstep speed and stops with the player
    const bool isMoving = m_player.isMoving();
    const bool isRunning = m_player.isRunning();
    if (isMoving)
        m_playerTorchBobPhase += dt * (isRunning ? 8.0f : 6.0f);
    const float bobAmp = isRunning ? 0.016f : 0.010f;
    const float moveBlendSpeed = isRunning ? 12.0f : 10.0f;
    m_playerTorchMoveBlend += ((isMoving ? 1.0f : 0.0f) - m_playerTorchMoveBlend) * std::min(1.0f, dt * moveBlendSpeed);
    const float walkBobY = m_playerTorchMoveBlend * std::sin(m_playerTorchBobPhase * 2.0f) * bobAmp;

    // Lowered torch (torchBlend = 0) drops out of the frame
    const float raiseOffsetY = -(1.0f - m_player.torchBlend()) * 0.9f;

    glUniform2f(m_uniViewmodelSway, m_playerTorchSwayVisual + idleBobX, idleBobY + walkBobY + raiseOffsetY);
}

// Drawn on the window framebuffer after ascii.end()
// the post-process does not touch the compass glyphs
void DungeonScene::renderCompassOverlay(int viewportWidth, int viewportHeight)
{
    glViewport(0, 0, viewportWidth, viewportHeight);

    // Enemies the player has seen; offset in whole cells, Z flipped (the minimap is north-up)
    // The dev dummy is not shown
    const glm::vec3 playerWorldPos = m_player.camPos();

    std::vector<float> enemySpottedAlphas;
    std::vector<glm::vec2> enemyMinimapOffsets;
    enemySpottedAlphas.reserve(kEnemyCount);
    enemyMinimapOffsets.reserve(kEnemyCount);
    for (int i = 0; i < kEnemyCount; ++i)
    {
        const glm::vec3 enemyWorldPos = m_enemyAIs[i].position();
        enemySpottedAlphas.push_back(m_enemyAIs[i].spottedMarkerAlpha());
        enemyMinimapOffsets.emplace_back(
            std::floor(enemyWorldPos.x) - std::floor(playerWorldPos.x),
            std::floor(playerWorldPos.z) - std::floor(enemyWorldPos.z)
        );
    }

    m_compass.render(
        m_player.poseBlend(),
        m_player.yaw(),
        m_minimapFog.minimapTexture(),
        m_colorEnabled,
        enemySpottedAlphas,
        enemyMinimapOffsets
    );
}

void DungeonScene::renderDebugMap(int viewportWidth, int viewportHeight)
{
    // All real enemies plus the dev dummy
    std::vector<glm::vec3> enemyPositions;
    enemyPositions.reserve(kEnemyCount + 1);
    for (int i = 0; i < kEnemyCount; ++i)
        if (m_enemies[i].isLoaded())
            enemyPositions.push_back(m_enemyAIs[i].position());
    if (m_devDummyEnemyActive && m_testEnemy.isLoaded())
        enemyPositions.push_back(m_enemyAI.position());

    m_debugMapOverlay.render(
        viewportWidth, viewportHeight, m_player.debugMapVisible(),
        m_minimapFog.mapTexture(), m_mapW, m_mapH,
        m_player.camPos(), m_player.yaw(),
        m_columnCentersXZ, m_zoneGrid,
        enemyPositions,
        m_landmarkPropPositionsXZ
    );
}
