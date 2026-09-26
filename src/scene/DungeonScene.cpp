#include "DungeonScene.h"
#include "render/ShaderLoader.h"
#include "render/ShaderProgram.h"
#include "WallTexture.h"
#include "MapGenerator.h"
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
    // Empty until generateMap() fills it, so check the size before indexing.
    if (m_wallCornerCuts.size() != (size_t)m_mapW * (size_t)m_mapH) return WallShapes::CornerCut::None;
    return m_wallCornerCuts[(size_t)z * m_mapW + x];
}

float DungeonScene::wallChamferSize(int x, int z) const {
    if (x < 0 || x >= m_mapW || z < 0 || z >= m_mapH) return WallShapes::kChamferSize;
    if (m_chamferSizes.size() != (size_t)m_mapW * (size_t)m_mapH) return WallShapes::kChamferSize;
    return m_chamferSizes[(size_t)z * m_mapW + x];
}

void DungeonScene::setCompassUiFont(
    GLuint texture,
    int glyphCount
)
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

    // Zones are built first: everything below reads per-cell probabilities from m_zoneGrid, which
    // the debug map also uses to draw sector boundaries.
    m_zoneGrid = Zoning::BuildZoneGrid(m_mapW, m_mapH, seed);

    // Grid mutations run in a fixed order. Widening comes first because it changes which cells are
    // open, and columns/chamfers must see the widened grid.
    m_corridorWidened = CorridorWidth::ApplyWidening(
        m_mapW, m_mapH, m_map, seed,
        [this](int x, int z) { return Zoning::StyleAt(m_zoneGrid, x, z).widenProbability; });

    // Columns must run before BuildCornerCuts() below: candidate wall cells become floor here, and
    // neighboring walls need to see that before chamfer detection.
    m_columnCentersXZ = Columns::BuildColumns(
        m_mapW, m_mapH, m_map, seed,
        [this](int x, int z) { return Zoning::StyleAt(m_zoneGrid, x, z).columnProbability; });

    // Chamfers use the maze seed, so loading a save reproduces them without storing them. Long
    // diagonal chains are detected first and forced to probability 1.0 so the whole chain is
    // chamfered and reads as a diagonal.
    m_diagonalChainMask = DiagonalCorridors::DetectChains(m_mapW, m_mapH, m_map);
    m_wallCornerCuts = WallShapes::BuildCornerCuts(
        m_mapW, m_mapH, m_map, seed,
        [this](int x, int z) {
            const size_t idx = (size_t)z * m_mapW + x;
            if (idx < m_diagonalChainMask.size() && m_diagonalChainMask[idx]) return 1.0f;
            return Zoning::StyleAt(m_zoneGrid, x, z).chamferProbability;
        });

    // Chain cells use kChamferSizeChain so a diagonal reads as connected; other chamfered cells get
    // a size in [kChamferSizeVariedMin, kChamferSizeVariedMax] for silhouette variety.
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

    // Per-cell palette, blended across nearby zone borders to avoid a hard color jump. Geometry
    // receives ready-made per-cell colors and knows nothing about zoning. The spawn area is forced
    // to palette 0 so the start always looks the same.
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
    m_winButtonPos = result.winButtonPos;

    m_smallSafeZoneCenters = std::move(result.smallSafeZoneCenters);
    m_smallSafeZoneRadius = result.smallSafeZoneRadius;

    // Diaries arrive already placed one per pocket. Read flags start cleared; loadSlot() restores
    // them.
    m_diaries = std::move(result.diaries);
    m_diariesRead.assign(m_diaries.size(), false);
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

// Small decorative props (knee/waist height) at main-corridor forks. Plain diffuse geometry (matId
// 0), no collision.
static void AddCairnProp(std::vector<Vertex>& verts, std::vector<GLuint>& indices, const glm::vec3& base)
{
    // Low segment counts on purpose: extra smoothness is invisible in the ASCII output.
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

// Final win scene (see WinSequenceState in the header). The two functions below build geometry for
// the current phase; updateWinSequence() calls them.

// matId tag for geometry that takes part in the shared monument alpha fade. The 0.05..0.5 range
// overlaps no other matId (0.0 opaque, 0.5-1.5 torch handle, 1.5-2.5 flame, >2.5 particles).
static constexpr float kMonumentFadeMatId = 0.2f;

// matId tag for the torus. It also falls inside the fade range (harmless: the alpha stays 1.0 while
// it spins) and inside 0.3..0.4, which scene.frag excludes from wall/floor texturing. The pedestal
// (0.2) keeps its stone texture.
static constexpr float kTorusNoTextureMatId = 0.35f;

// Phase 1 (dissolving): the pedestal keeps full size and fades via uMonumentFadeAlpha (curve from
// updateWinSequence()). Sparks are derived from the timer and index, so no particle state is
// stored.
static void AddDissolvingMonument(std::vector<Vertex>& verts, std::vector<GLuint>& indices,
                                    const glm::vec3& base, float timer, bool spawnParticles)
{
    const glm::vec3 pedestalColor(0.55f, 0.42f, 0.12f);
    const glm::vec3 capColor(0.25f, 0.95f, 0.35f);

    AddCylinder(verts, indices, base, base + glm::vec3(0.0f, 0.9f, 0.0f),
                0.35f, 0.30f, 16, pedestalColor, kMonumentFadeMatId);
    AddCylinder(verts, indices,
                base + glm::vec3(0.0f, 0.9f, 0.0f), base + glm::vec3(0.0f, 1.05f, 0.0f),
                0.30f, 0.22f, 16, pedestalColor, kMonumentFadeMatId);
    AddSphere(verts, indices, base + glm::vec3(0.0f, 1.18f, 0.0f), 0.16f,
              14, 10, capColor, kMonumentFadeMatId);

    if (!spawnParticles)
        return; // pedestal not activated yet: no sparks

    const int kParticleCount = 18;
    for (int p = 0; p < kParticleCount; ++p)
    {
        const float seed = (float)p * 12.9898f;
        const float rx = std::sin(seed) * 0.5f + 0.5f;
        const float rz = std::sin(seed * 1.37f) * 0.5f + 0.5f;
        const float rSpeed = 0.6f + std::sin(seed * 2.11f) * 0.2f;
        const float startDelay = (float)p / (float)kParticleCount * 0.25f;

        const float localT = std::max(0.0f, timer - startDelay);
        const float height = localT * (1.2f + rSpeed);
        const float wobble = std::sin(localT * 6.0f + seed) * 0.08f;

        const float particleShrink = std::max(0.0f, 1.0f - localT / 0.6f);
        if (particleShrink <= 0.0f)
            continue;

        const glm::vec3 pos = base + glm::vec3(
            (rx - 0.5f) * 0.5f + wobble,
            0.3f + height,
            (rz - 0.5f) * 0.5f
        );
        AddSphere(verts, indices, pos, 0.035f * particleShrink, 4, 3, capColor, 0.0f);
    }
}

// Phase 2 (spinning): the donut tumbles on two axes. Rotation is applied on the CPU because the
// mesh is rebuilt every frame in this phase anyway. The shared ASCII post-process handles the look,
// so no special math is needed.
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

            verts.push_back({ center + rot * local, rot * normalLocal, color, kTorusNoTextureMatId });
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

// Builds tiny sphere "stones" from world positions. Shared by pickable stones (rebuilt rarely) and
// thrown stones (rebuilt every frame while any are in flight).
static void BuildStoneMesh(std::vector<Vertex>& verts, std::vector<GLuint>& indices,
                            const std::vector<glm::vec3>& positions)
{
    const glm::vec3 stoneColor(0.40f, 0.38f, 0.36f);
    for (const glm::vec3& p : positions)
        AddSphere(verts, indices, p, kStoneVisualRadius, 6, 4, stoneColor, 0.0f);
}

// Re-uploads a stone mesh; shared by the pickup and thrown-stone VAOs. GL_DYNAMIC_DRAW because both
// buffers are rewritten in full.
static void UploadDynamicStoneMesh(GLuint& vao, GLuint& vbo, GLuint& ebo, GLsizei& indexCount,
                                     const std::vector<Vertex>& verts, const std::vector<GLuint>& indices)
{
    // Attribute bindings belong to the VAO, so they are set up once on the first call. Only
    // glBufferData has to repeat.
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

    // Dedicated RNG with a per-feature salt: the same seed reproduces the same stone layout
    // regardless of the call order in other generators.
    std::mt19937 rng(seed ^ 0x53746F6Eu);
    std::uniform_int_distribution<int> countDist(1, 2);

    // Diagonal offsets inside the 3x3 pocket, away from the center where the diary sits.
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

    // Nearest-neighbor tour over pockets. Straight-line distance is enough to order the visits;
    // real paths are computed later, only between chosen neighbors.
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

    // For each chain link, follow the real corridor path (same GridPathfinding as EnemyAI) to the
    // next pocket and place 2-3 landmark torches (kMaxGuideTorches) along its first cells, with a
    // longer minimap highlight (kMinimapHintCells). Only the start of the path is used: it hints at
    // the direction without revealing the route.
    std::mt19937 rng(seed ^ 0x4C616E64u);

    const int kMinimapHintCells = 4;
    const int kMaxGuideTorches = 3;

    static const glm::ivec2 kNeighborDirs[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };

    // Props and torches share this loop so each link's path is computed once.
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

        // Landmark torches on the first path cells, mounted like MapGenerator's TryAddTorch().
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

                // Normal points from the wall to the floor, opposite of d.
                const glm::vec3 normal((float)-d.x, 0.0f, (float)-d.y);

                const glm::vec3 wallCenter((float)wx + 0.5f, 0.0f, (float)wz + 0.5f);
                const glm::vec3 wallBase = wallCenter + normal * 0.5f;

                // Same minimum spacing as TryAddTorch(), checked against all torches, not only
                // landmarks.
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
                // landmark torches are permanently "taken": not pickable, never extinguished
                m_torchTaken.push_back(1);

                // Minimap uses the wall cell (wx,wz), like m_torchCellLookup, not the floor cell.
                if (wx >= 0 && wx < m_mapW && wz >= 0 && wz < m_mapH)
                    m_guideTorchCellLookup[(size_t)wz * m_mapW + wx] = 1;

                ++placedTorches;
                break; // one wall per path cell
            }
        }

        // Decorative props on every other chain link, at roughly 1/3 and 2/3 of the full path. They
        // are a mid-corridor accent, not a direction hint, so unlike the torches they are not
        // limited to the first cells.
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
            pickedAny = true;
        }
    }

    // Rebuild the mesh once per frame, not once per picked-up stone.
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
    // Louder than the player's footsteps (see the EnemyAI hearing radii), otherwise it would not
    // work as a distraction.
    const float kNoiseImpactRadius = 9.0f;
    const float kMaxLifetime = 4.0f; // safety cap in case a stone never lands

    for (size_t i = 0; i < m_thrownStones.size(); )
    {
        ThrownStone& stone = m_thrownStones[i];
        stone.vel.y -= kGravity * deltaTime;
        const glm::vec3 nextPos = stone.pos + stone.vel * deltaTime;
        stone.life += deltaTime;

        bool consumed = false;

        // Enemies are checked first: a direct hit outranks a wall/floor impact. Circle test in XZ,
        // ignoring height.
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

        // Wall/floor impact: not a hit, but still makes noise (EnemyAI::notifyNoiseEvent()).
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

// "Unreliable vision" effects (see glitchStage() in the header), ticked every frame from
// processInput(). Each of the four effects has its own cooldown timer plus an active timer that
// runs while the event is perceivable.

void DungeonScene::updateWinSequence(float deltaTime)
{
    if (m_winSequenceState == WinSequenceState::None)
    {
        // The pedestal is a dynamic mesh, so it has to be built once per map. It does not change
        // until the sequence starts.
        if (!m_winMonumentBuiltOnce)
        {
            m_winMonumentBuiltOnce = true;
            m_winMonumentFadeAlpha = 1.0f;
            m_winMonumentVertsScratch.clear();
            m_winMonumentIndicesScratch.clear();
            AddDissolvingMonument(m_winMonumentVertsScratch, m_winMonumentIndicesScratch, m_winButtonPos, 0.0f, /*spawnParticles=*/false);
            UploadDynamicStoneMesh(m_winMonumentVao, m_winMonumentVbo, m_winMonumentEbo,
                                    m_winMonumentIndexCount, m_winMonumentVertsScratch, m_winMonumentIndicesScratch);
        }
        return;
    }

    m_winSequenceTimer += deltaTime;

    // Reused buffers: clear() keeps the capacity, so there is no allocation after the first frame.
    std::vector<Vertex>& verts = m_winMonumentVertsScratch;
    std::vector<GLuint>& indices = m_winMonumentIndicesScratch;
    verts.clear();
    indices.clear();

    if (m_winSequenceState == WinSequenceState::Dissolving)
    {
        // Nonlinear curve (t^2): stays nearly opaque, then vanishes quickly at the end.
        const float t = std::clamp(m_winSequenceTimer / kWinDissolveDuration, 0.0f, 1.0f);
        m_winMonumentFadeAlpha = 1.0f - t * t;
        AddDissolvingMonument(verts, indices, m_winButtonPos, m_winSequenceTimer, /*spawnParticles=*/true);

        if (m_winSequenceTimer >= kWinDissolveDuration)
        {
            m_winSequenceState = WinSequenceState::Spinning;
            m_winSequenceTimer = 0.0f;
            m_torusAngleA = 0.0f;
            m_torusAngleB = 0.0f;
        }
    }
    else
    {
        // Reset the alpha here, in sync with building the torus. Resetting it earlier drew the old,
        // near-transparent pedestal geometry at full alpha for one frame (a visible flash).
        m_winMonumentFadeAlpha = 1.0f;

        // Two angles with different speeds give the tumble; equal speeds would just look like
        // rotation around one diagonal axis.
        const float kSpinSpeedA = 1.1f; // rad/sec
        const float kSpinSpeedB = 0.7f;
        m_torusAngleA = std::fmod(m_torusAngleA + kSpinSpeedA * deltaTime, glm::two_pi<float>());
        m_torusAngleB = std::fmod(m_torusAngleB + kSpinSpeedB * deltaTime, glm::two_pi<float>());

        const glm::vec3 torusCenter = m_winButtonPos + glm::vec3(0.0f, 0.55f, 0.0f);
        AddSpinningTorus(verts, indices, torusCenter, m_torusAngleA, m_torusAngleB,
                          0.336f, 0.128f, 24, 12, glm::vec3(0.62f, 0.62f, 0.62f));
    }

    UploadDynamicStoneMesh(m_winMonumentVao, m_winMonumentVbo, m_winMonumentEbo,
                            m_winMonumentIndexCount, verts, indices);
}

void DungeonScene::updateGlitchEffects(float deltaTime)
{
    static std::mt19937 rng(std::random_device{}());

    // Drives glitchStage(). Advances only during real, unpaused gameplay (the caller gates that).
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

            // A spot rather than a single cell; radius in ASCII-grid cells (uGlitchRadiusCells in
            // ascii_post.frag).
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
            // Nearest torch, computed only at this rare moment; O(N) over all torches is fine.
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

            // Stage 4 is deliberately not more frequent than stage 3 for this effect.
            float lo = 12.0f, hi = 16.0f;
            if (stage >= 3) { lo = 8.0f; hi = 12.0f; }
            std::uniform_real_distribution<float> cdDist(lo, hi);
            m_torchGlitchCooldown = cdDist(rng);
        }
    }

    // Effect 4 (stage >= 4, 420 s+): flickering enemy silhouette
    if (m_silhouetteActiveTimer > 0.0f)
    {
        m_silhouetteActiveTimer -= deltaTime;
        // Static pose, but the clip clock must still tick: otherwise draw() renders clip time 0,
        // which is the animation's first frame rather than the idle stance.
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

                // no closer: a nearer silhouette reads as a teleport, not a glitch
                const float kMinDist = 4.5f;
                // slightly under the player's sight radius in isEnemyVisibleToPlayer()
                const float kMaxDist = 13.0f;
                // slightly narrower than the real FOV so it is not right at the frame edge
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

                    // Must pass the same visibility test (distance, FOV, line of sight) as a real
                    // enemy, or it could appear where a real one never could.
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
                // No valid spot found (rare): retry soon instead of waiting the full cooldown.
                m_silhouetteCooldown = 5.0f;
            }
        }
    }

    // Fake-footsteps sub-effect (shared by two of the three variants below). Ticks independently of
    // the trigger, so it plays out its full 2-4 s even if the stage changes. Intervals mirror the
    // real walk/run cadence (see PlayerController.cpp).
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
            // Kind 0: moan (always available). 1: fake walk steps (not while walking). 2: fake run
            // steps (not while running).
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

// Resets the lit-mask texture to "everything lit" (255). Used when a new map is generated; pickups
// use a point update instead (see pickupWallTorch()).
void DungeonScene::resetTorchLitMask()
{
    if (m_torchLitMaskTex == 0)
        return;

    std::vector<unsigned char> allLit(
        (size_t)kTorchLitMaskDim * kTorchLitMaskDim, 255);

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

    // Rebuild the lookup once for the whole list, not per torch as pickupWallTorch() does.
    rebuildTorchCellLookupFromTaken();
}

// Shared by pickupWallTorch() and restoreTorchPickups(): turns off the torch's light and visible
// flame and marks it taken. Leaves m_torchCellLookup and the player's inventory to the caller (a
// pickup credits +1, a save restore must not).
void DungeonScene::extinguishTorchVisualAndLight(int torchIndex)
{
    m_torchTaken[(size_t)torchIndex] = true;

    // Zeroing the intensity is enough: Lighting and the scene shader already filter on it.
    m_torchIntensity[(size_t)torchIndex] = 0.0f;

    // Visible flame: update one texel of the lit-mask texture (uTorchLitMask in scene.frag).
    if (m_torchLitMaskTex != 0)
    {
        const unsigned char off = 0;
        const int tx = torchIndex % kTorchLitMaskDim;
        const int ty = torchIndex / kTorchLitMaskDim;
        glBindTexture(GL_TEXTURE_2D, m_torchLitMaskTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, tx, ty, 1, 1,
                         GL_RED, GL_UNSIGNED_BYTE, &off);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

// Taken torches must not show a torch icon on the minimap (MinimapFog::updateMinimap()). Rebuilt in
// full from the untaken torches; O(N), but only on pickup and save load.
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

// Smooth cell color: near a border between the two nearest zone centers the palettes blend (50/50
// at the border, pure nearest when the distance difference is at least kPaletteBlendWidth),
// avoiding a hard jump.
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
    float nearestDistSq = 1e30f, secondDistSq = 1e30f;

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

    // Reset the "unreliable vision" state for each playthrough. Resetting m_glitchElapsedTime alone
    // is not enough: the active timers are checked before glitchStage() and are not stage-gated, so
    // an effect in progress (e.g. fake footsteps) would carry into the new run.
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

    // Built once and shared by all enemies by pointer, instead of one copy per enemy.
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

    // Dev dummy (K key): independent of the real enemies. It needs the map for its own
    // resolveWallCollision(). The spawn position is a placeholder; the first K press moves it to
    // where the player is looking.
    m_enemyAI.init(
        m_mapW, m_mapH,
        &m_map, &m_wallCornerCuts, &m_diagonalChainMask,
        &m_columnCentersXZ, Columns::kColumnRadius,
        &m_pathfindingMapWithColumns
    );
    const glm::vec3 dummySpawnPos(3.5f, 0.0f, 3.5f);
    m_enemyAI.setPosition(dummySpawnPos);

    // destroy() is safe on the first call (all handles are zero), so init(), newGame() and
    // loadSlot() share this path.
    m_geometry.destroy();

    // Diary world positions: pocket cell center (+0.5 offset, like winButtonPos).
    std::vector<glm::vec3> diaryPositions;
    diaryPositions.reserve(m_diaries.size());
    for (const Diaries::PlacedDiary& d : m_diaries) {
        diaryPositions.emplace_back((float)d.pocketCellX + 0.5f, 0.0f, (float)d.pocketCellZ + 0.5f);
    }

    m_geometry.build(m_mapW, m_mapH, m_map, m_winButtonPos,
                      m_torchWallBase, m_torchNormal, m_torchFlamePos,
                      m_wallCornerCuts, m_columnCentersXZ, m_chamferSizes,
                      m_paletteWallColors, m_paletteFloorColors,
                      diaryPositions,
                      m_firstGuideTorchIndex);

    // Perf diagnostic: size of the generated geometry. Chamfered cells break the greedy quad merge
    // (SceneGeometry::BuildFloorAndWallsGreedy), so a high chamfer probability inflates
    // vertex/index counts across the whole map. Printed so tuning it is visible without a profiler.
    std::printf(
        "[perf] map %dx%d -> %d vertices, %d indices (%d triangles)\n",
        m_mapW, m_mapH,
        (int)m_geometry.vertexCount(),
        (int)m_geometry.indexCount(),
        (int)m_geometry.indexCount() / 3
    );

    // Both steps depend on m_geometry.build() (chunk count, wall map), so they run after it.
    reserveRenderScratchBuffers();
    m_geometry.buildPVS(m_mapW, m_mapH, [this](int x, int z) { return isWall(x, z); });
}

void DungeonScene::generateFreshMapAndPlayer()
{
    std::random_device rd;
    const unsigned int seed = rd();
    loadMapAndGeometry(seed);

    // Spawn at the center of the starting safe zone. resetForNewGame() also resets look direction,
    // health, stamina and the compass.
    const glm::vec3 spawnPos(6.5f, 0.5f, 6.5f);
    m_player.resetForNewGame(spawnPos);
}

// Builds the decorative map behind the main menu. It never touches m_activeSlot/m_saveName and
// never saves (there is no real session; saving would create a file on every launch or overwrite
// the oldest save). saveActiveSlot() is a no-op while m_activeSlot is negative.
void DungeonScene::generateMenuBackgroundMaze()
{
    generateFreshMapAndPlayer();

    // Slight downward tilt so both the floor and the wall torches above eye level fit in frame. Yaw
    // stays at the regular spawn heading.
    m_player.setPitchDegrees(-6.0f);
}

void DungeonScene::newGame(int explicitSlot, const std::string& explicitName)
{
    generateFreshMapAndPlayer();

    // explicitSlot >= 0: the player already picked a slot (and maybe a name); use them as given.
    // Otherwise take the first empty slot or the oldest save, with an empty name (death path: a
    // real session that must be saved).
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

    // The seed rebuilds the exact same maze and torch layout, so geometry is not stored in the
    // save.
    loadMapAndGeometry(save.seed);

    glm::vec3 pos(save.posX, save.posY, save.posZ);
    if (!(pos.x > 0.0f && pos.x < (float)m_mapW && pos.z > 0.0f && pos.z < (float)m_mapH)) {
        const SaveSystem::SaveData defaults;
        pos = glm::vec3(defaults.posX, defaults.posY, defaults.posZ);
    }
    m_player.restoreState(pos, save.yaw, save.pitch, save.healthFraction, save.staminaFraction);

    // Fog of war is restored only if its size matches the map (hand-edited or older save);
    // otherwise it stays "nothing revealed".
    if (save.explored.size() == (size_t)m_mapW * m_mapH) {
        m_minimapFog.setExplored(std::move(save.explored));
    }

    // loadMapAndGeometry() rebuilt m_diaries from the seed and cleared the read flags; re-mark the
    // ones read at save time.
    for (int idx : save.diariesReadIndices) {
        if (idx >= 0 && idx < (int)m_diariesRead.size())
            m_diariesRead[(size_t)idx] = true;
    }
    rebuildDiaryReadWallLookup();

    // Torches: the same seed gives the same layout with none taken. Extinguish the ones taken at
    // save time. Fuel and inventory are restored separately (restoreTorchPickups() does not touch
    // the inventory).
    restoreTorchPickups(save.torchTakenIndices);
    m_player.restoreTorchState(save.torchFuel, save.torchInventoryCount);

    // Stones: mark the ones picked up at save time and restore the inventory count separately,
    // otherwise they would respawn on the floor while the inventory kept growing.
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

    // The snapshot is assembled synchronously (cheap); the disk write is handed to SaveSlotAsync()
    // so it does not stall the game loop.
    SaveSystem::SaveData data;
    data.seed = m_currentSeed;
    data.name = m_saveName;

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
    for (size_t i = 0; i < m_diariesRead.size(); ++i)
        if (m_diariesRead[i]) data.diariesReadIndices.push_back((int)i);

    data.torchFuel = m_player.torchFuel();
    data.torchInventoryCount = m_player.torchInventoryCount();
    data.torchTakenIndices.clear();
    for (size_t i = 0; i < m_torchTaken.size(); ++i)
    {
        // Landmark torches are always "taken" (merely non-interactive) and are excluded from the
        // list: regenerating the map from the seed marks them again, and a restore must not
        // extinguish them.
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
    // Enemy positions from the end of the previous frame (EnemyAI::update() runs in render(), after
    // processInput()): a one-frame lag that is imperceptible. The dev dummy is excluded: it must
    // not physically block the player.
    std::vector<glm::vec3> enemyPositions;
    enemyPositions.reserve(kEnemyCount);
    for (int i = 0; i < kEnemyCount; ++i)
        enemyPositions.push_back(m_enemyAIs[i].position());

    // Same pocket-center formula as in loadMapAndGeometry(); cheap enough not to cache.
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
        m_winButtonPos,
        enemyPositions,
        diaryPositions,
        diariesReadCount(),
        m_torchWallBase,
        m_torchTaken,
        winReadyToPressE()
    );

    if (m_player.consumeWinSequenceRequest())
    {
        m_winSequenceState = WinSequenceState::Dissolving;
        m_winSequenceTimer = 0.0f;
    }
    updateWinSequence(deltaTime);

    if (m_player.consumeWinBlockedRequest())
        m_winBlockedMessageTimer = 2.5f;
    if (m_winBlockedMessageTimer > 0.0f)
        m_winBlockedMessageTimer = std::max(0.0f, m_winBlockedMessageTimer - deltaTime);

    m_nearbyDiaryIndex = m_player.nearbyDiaryIndex();

    // Opening a diary with E works only from regular gameplay: while a reading screen is open,
    // Application does not call processInput().
    if (m_player.consumeDiaryOpenRequest() && m_nearbyDiaryIndex != -1)
    {
        m_openDiaryIndex = m_nearbyDiaryIndex;
        m_diariesRead[(size_t)m_openDiaryIndex] = true;
        rebuildDiaryReadWallLookup();

        // Sync the overlay's E tracker while the key is known to be held. Otherwise the next frame,
        // tickReadingOverlayInput() sees the still-held key as a new press and closes the diary it
        // just opened.
        m_overlayEKeyWasDown = true;
    }

    if (m_player.consumeJournalToggleRequest())
    {
        m_journalOpen = true;
        m_journalSelectedIndex = 0;

        // Same tracker sync as above, for Tab.
        m_overlayTabKeyWasDown = true;
    }

    m_nearbyWallTorchIndex = m_player.nearbyTorchIndex();
    if (m_player.consumeTorchPickupRequest() && m_nearbyWallTorchIndex != -1)
    {
        pickupWallTorch(m_nearbyWallTorchIndex);
        // Just taken: clear it so the HUD does not show the hint over an extinguished torch until
        // nearbyTorchIndex() is recomputed.
        m_nearbyWallTorchIndex = -1;
    }
    if (m_player.consumeTorchEmptyWarningRequest())
        m_torchEmptyMessageTimer = 2.0f;
    if (m_torchEmptyMessageTimer > 0.0f)
        m_torchEmptyMessageTimer = std::max(0.0f, m_torchEmptyMessageTimer - deltaTime);

    // Stones: auto-pickup (tryAutoPickupStones()) and throwing with G. Flight physics run in
    // updateThrownStones() from render() (it needs this frame's enemy positions); here a stone is
    // only spawned from the current camera position and direction.
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

// Diaries: reading screen and journal (see Diaries.h). Separate from processInput(): while
// isReadingOverlayOpen(), Application calls tickReadingOverlayInput() instead.

void DungeonScene::tickReadingOverlayInput(GLFWwindow* window)
{
    const bool eDown     = glfwGetKey(window, GLFW_KEY_E)      == GLFW_PRESS;
    const bool enterDown = glfwGetKey(window, GLFW_KEY_ENTER)  == GLFW_PRESS;
    const bool tabDown   = glfwGetKey(window, GLFW_KEY_TAB)    == GLFW_PRESS;
    const bool upDown    = glfwGetKey(window, GLFW_KEY_UP)     == GLFW_PRESS;
    const bool downDown  = glfwGetKey(window, GLFW_KEY_DOWN)   == GLFW_PRESS;

    // Confirm is E or Enter. Each is edge-triggered on its own, so holding one while releasing the
    // other does not fire twice.
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
            if (upDown && !m_overlayUpKeyWasDown)
                m_journalSelectedIndex = (m_journalSelectedIndex - 1 + (int)m_diaries.size()) % (int)m_diaries.size();
            if (downDown && !m_overlayDownKeyWasDown)
                m_journalSelectedIndex = (m_journalSelectedIndex + 1) % (int)m_diaries.size();

            const bool selectionIsRead =
                m_journalSelectedIndex >= 0 &&
                m_journalSelectedIndex < (int)m_diariesRead.size() &&
                m_diariesRead[(size_t)m_journalSelectedIndex];

            // Confirm opens the selected row only if it is already read: the journal reveals that
            // an unfound entry exists ("ENTRY N ---") but not its text.
            if (confirmPressed && selectionIsRead)
                m_openDiaryIndex = m_journalSelectedIndex;
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

    // Readable text (prose, "LOG", journal labels) is drawn by TextRenderer on top, after
    // ascii.end(), like Compass. This grid keeps only the fixed decorative parts (frame, blood
    // drips, small hints): the grid font cannot scale text, one character is always one cell.
    int boxX0, boxY0, boxX1, boxY1;
    getReadingBoxBounds(cols, rows, boxX0, boxY0, boxX1, boxY1);
    MainMenu::DrawBox(grid, cols, rows, boxX0, boxY0, boxX1, boxY1,
                       /*thickness=*/1, /*filled=*/false, /*seed=*/1);

    if (m_openDiaryIndex != -1 && m_openDiaryIndex < (int)m_diaries.size())
    {
        const Diaries::PlacedDiary& d = m_diaries[(size_t)m_openDiaryIndex];

        MainMenu::PutText(grid, cols, rows, cols / 2 - 4, boxY1 - 1, "E CLOSE");

        // Blood streaks along the frame's top and bottom edge; deterministic from poolIndex.
        for (int i = 0; i < 5; ++i)
        {
            const int span = std::max(1, (boxX1 - boxX0) - 4);
            const unsigned char dripGlyph = (i % 2 == 0) ? MainMenu::GLYPH_DRIP_BIG : MainMenu::GLYPH_DRIP_SMALL;

            const unsigned int hTop = MainMenu::Hash(d.poolIndex * 97 + i, 4242);
            MainMenu::PutGlyph(grid, cols, rows, boxX0 + 2 + (int)(hTop % (unsigned)span), boxY0 + 1, dripGlyph);

            const unsigned int hBot = MainMenu::Hash(d.poolIndex * 131 + i, 9001);
            MainMenu::PutGlyph(grid, cols, rows, boxX0 + 2 + (int)(hBot % (unsigned)span), boxY1 - 1, dripGlyph);
        }
    }
    else if (m_journalOpen)
    {
        // The selected-row highlight is drawn by TextRenderer (Application.cpp:
        // m_textRenderer.drawRect()) at the same pixel coordinates as the row label, so the two
        // cannot drift apart.
        MainMenu::PutText(grid, cols, rows, cols / 2 - 10, boxY1 - 1, "TAB CLOSE, E READ");
    }

    return true;
}

void DungeonScene::getReadingBoxBounds(int cols, int rows, int& x0, int& y0, int& x1, int& y1) const
{
    // One frame shared by both modes. Its bounds are computed here so Application can place
    // TextRenderer text exactly inside it.
    x0 = cols / 2 - 38; x1 = cols / 2 + 38;
    y0 = rows / 2 - 20; y1 = rows / 2 + 20;
}

void DungeonScene::spawnEnemiesAcrossMap(uint32_t seed)
{
    // One enemy per map sector so they do not clump (random points over the whole map could put
    // several in one room). The sector grid is derived from kEnemyCount. Each spawn stays at least
    // kMinDistFromPlayerSpawn away from the player's start.
    std::mt19937 rng(seed ^ 0x5EED0004u); // own RNG stream, independent of the other generators

    const glm::vec3 playerSpawnPos(6.5f, 0.0f, 6.5f);
    const float kMinDistFromPlayerSpawn = 8.0f;
    const int kAttemptsPerSector = 200;

    // Nearest-to-square grid that fits kEnemyCount sectors (e.g. 7 enemies -> 3x3 with two sectors
    // left empty); the sectors still cover the map evenly.
    const int gridCols = (int)std::ceil(std::sqrt((double)std::max(1, kEnemyCount)));
    const int gridRows = (int)std::ceil((double)std::max(1, kEnemyCount) / (double)gridCols);

    const int sectorW = std::max(1, m_mapW / gridCols);
    const int sectorH = std::max(1, m_mapH / gridRows);

    for (int i = 0; i < kEnemyCount; ++i)
    {
        const int sx = i % gridCols;
        const int sz = (i / gridCols) % gridRows;

        const int x0 = sx * sectorW;
        // the last column extends to the map edge when mapW does not divide evenly
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
            // Fallback: scan the sector in order. Rare (tiny map, or a sector that is mostly
            // walls), but deterministic and never leaves an enemy on a wall cell.
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

    // Roughly the AI's own running sight range (kSightRadiusRun in EnemyAI.cpp): the player should
    // not see an enemy from farther than the enemy could see the player.
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

            // Half view angle. The camera uses a 63° vertical FOV; 45° gives roughly 90° horizontal
            // on a widescreen ratio, plus some margin for peripheral vision.
            const float kPlayerFOVHalfAngleDeg = 45.0f;
            const float cosHalfFOV = std::cos(glm::radians(kPlayerFOVHalfAngleDeg));
            if (glm::dot(front, toEnemyDir) < cosHalfFOV)
                return false;
        }
    }

    // Same line-of-sight test as AI perception (LineOfSight::HasLineOfSight), with the camera as
    // the observer.
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
    // Silently refuse past the shader array limit (devLightPos[8] in scene.frag/enemy.frag); this
    // is a dev tool.
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

    // Place the dummy a reasonable distance ahead; at the camera point it would end up inside the
    // player.
    const glm::vec3 front = glm::normalize(m_player.getFront());
    const glm::vec3 spawnPos = m_player.camPos() + front * 2.0f;

    m_enemyAI.setPosition(spawnPos);
    m_testEnemy.setPosition(spawnPos);
    // Faces the player. Same axis convention as EnemyAI.cpp: atan2(dir.x, dir.z).
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
    // -1 (zone palette) -> 0 -> ... -> kPaletteCount-1 -> back to -1. -1 is part of the cycle, so
    // one key both cycles through palettes and returns to normal.
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

    // Standalone instance for the deceptive glitch sounds; not tied to any enemy.
    m_glitchAudio.init();

    // One-time GL init (scene shader, wall texture, compass, debug overlay). It is independent of
    // any map, so it runs once here, before the first maze. newGame() can run repeatedly and does
    // not touch any of this.
    const std::string sceneVertSrc = ShaderLoader::LoadSource("assets/shaders/scene.vert");
    const std::string sceneFragSrc = ShaderLoader::LoadSource("assets/shaders/scene.frag");
    GLuint vs = compileShader(GL_VERTEX_SHADER, sceneVertSrc.c_str());
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, sceneFragSrc.c_str());
    m_program = linkProgram(vs, fs);
    cacheUniformLocations();

    // If the wall texture fails to load, walls render untextured (see wallTexLoaded in scene.frag);
    // no crash.
    m_wallTex.load(WallTexture::kDefaultName);

    // Wall-torch lit mask: a small R8 texture (kTorchLitMaskDim squared), initially fully lit
    // (255). placeTorches()/resetTorchLitMask() fill it per map; this only allocates the GPU
    // resource once.
    {
        std::vector<unsigned char> allLit(
            (size_t)kTorchLitMaskDim * kTorchLitMaskDim, 255);
        glGenTextures(1, &m_torchLitMaskTex);
        glBindTexture(GL_TEXTURE_2D, m_torchLitMaskTex);
        // GL_UNPACK_ALIGNMENT defaults to 4, which misaligns rows of single-byte data unless the
        // width is a multiple of 4. Set it explicitly instead of relying on the size.
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

    // Enemy: skeletal animation (see EnemyCharacter.h/SkinnedModel.h) with its own shader
    // (enemy.vert/enemy.frag). Same lighting model as the scene (torch shadows, hand light, fog),
    // but vertex color instead of wall textures.
    {
        const std::string enemyVertSrc = ShaderLoader::LoadSource("assets/shaders/enemy.vert");
        const std::string enemyFragSrc = ShaderLoader::LoadSource("assets/shaders/enemy.frag");
        GLuint evs = compileShader(GL_VERTEX_SHADER, enemyVertSrc.c_str());
        GLuint efs = compileShader(GL_FRAGMENT_SHADER, enemyFragSrc.c_str());
        m_enemyProgram = linkProgram(evs, efs);
        cacheEnemyUniformLocations();
    }

    // One enemy model shared by the dummy and all real enemies, loaded once; each gets a pointer.
    // If the file is missing, no enemy is drawn; no crash. The diffuse texture is not loaded here
    // (load() defaults to loadDiffuseTexture=false); see the note where hasDiffuseTex is set.
    const bool enemyModelLoaded = m_enemySharedModel.load("assets/models/enemy/the_wrapped.glb");
    if (!enemyModelLoaded)
    {
        std::fprintf(stderr,
            "[EnemyCharacter] no model at assets/models/enemy/the_wrapped.glb — "
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

    // Dev dummy (K key): position and state are driven by EnemyAI (m_enemyAI.update() in render()).
    // Here only the shared model and the clip mapping are attached.
    m_testEnemy.attachSharedModel(enemyModelLoaded ? &m_enemySharedModel : nullptr);
    if (m_testEnemy.isLoaded())
        assignClipNames(m_testEnemy);

    // Flickering silhouette of the unreliable-vision effect: another standalone EnemyCharacter on
    // the shared model, driven by updateGlitchEffects() instead of AI.
    m_glitchGhost.attachSharedModel(enemyModelLoaded ? &m_enemySharedModel : nullptr);
    if (m_glitchGhost.isLoaded())
        assignClipNames(m_glitchGhost);

    for (int i = 0; i < kEnemyCount; ++i)
    {
        m_enemies[i].attachSharedModel(enemyModelLoaded ? &m_enemySharedModel : nullptr);
        if (m_enemies[i].isLoaded())
            assignClipNames(m_enemies[i]);
    }

    // The session's first map, used only as the main menu background (see
    // generateMenuBackgroundMaze()).
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
    m_uniMonumentFadeAlpha = glGetUniformLocation(m_program, "uMonumentFadeAlpha");
    m_uniMapTex            = glGetUniformLocation(m_program, "mapTex");
    m_uniWallTex           = glGetUniformLocation(m_program, "wallTex");
    m_uniWallTexEnabled    = glGetUniformLocation(m_program, "wallTexEnabled");
    m_uniWallTexContrast   = glGetUniformLocation(m_program, "wallTexContrast");
    m_uniTorchLitMask      = glGetUniformLocation(m_program, "uTorchLitMask");
    m_uniTorchPos          = glGetUniformLocation(m_program, "torchPos");
    m_uniTorchColor        = glGetUniformLocation(m_program, "torchColor");
    m_uniTorchIntensity    = glGetUniformLocation(m_program, "torchIntensity");
    m_uniTorchCount        = glGetUniformLocation(m_program, "torchCount");
    // Columns: the shader needs them for the ray-vs-circle shadow test, since a column cell is
    // floor in the map texture and would not cast a shadow by itself.
    m_uniColumnPos         = glGetUniformLocation(m_program, "columnPos");
    m_uniColumnCount       = glGetUniformLocation(m_program, "columnCount");
    m_uniColumnRadius      = glGetUniformLocation(m_program, "columnRadius");

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
    m_uEnemyMapTex               = glGetUniformLocation(m_enemyProgram, "mapTex");
    m_uEnemyColumnPos            = glGetUniformLocation(m_enemyProgram, "columnPos");
    m_uEnemyColumnCount          = glGetUniformLocation(m_enemyProgram, "columnCount");
    m_uEnemyColumnRadius         = glGetUniformLocation(m_enemyProgram, "columnRadius");
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

    m_playerTorch.destroy();

    m_testEnemy.destroy();
    for (int i = 0; i < kEnemyCount; ++i)
        m_enemies[i].destroy();
    // The only place the shared model's GPU resources are freed: neither m_testEnemy nor
    // m_enemies[] own them.
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

    // Stone buffers are created lazily. They may not exist if the player never went near or threw a
    // stone, hence the zero check.
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
    if (m_winMonumentVao)
    {
        glDeleteVertexArrays(1, &m_winMonumentVao);
        glDeleteBuffers(1, &m_winMonumentVbo);
        glDeleteBuffers(1, &m_winMonumentEbo);
        m_winMonumentVao = m_winMonumentVbo = m_winMonumentEbo = 0;
    }
    // A new map has an intact pedestal. Reset the win-sequence state, otherwise starting a new game
    // mid-sequence would leave the donut floating without a pedestal.
    m_winSequenceState = WinSequenceState::None;
    m_winSequenceTimer = 0.0f;
    m_winMonumentFadeAlpha = 1.0f;
    m_winMonumentBuiltOnce = false;

    if (m_program)
    {
        glDeleteProgram(
            m_program
        );

        m_program = 0;
    }
}

void DungeonScene::render(
    int viewportWidth,
    int viewportHeight,
    bool gameplayActive)
{
    m_minimapFog.updateMinimap(
        m_mapW, m_mapH, m_map, m_player.camPos(), m_player.yaw(),
        [this](int x, int z) { return isWall(x, z); },
        m_torchCellLookup,
        m_guideTorchCellLookup,
        m_diaryReadWallLookup
    );

    glEnable(GL_DEPTH_TEST);

    glm::vec3 front =
    getFront();

    glm::vec3 renderCamPos =
        m_player.getCameraRenderPosition();

    glm::vec3 renderCamUp =
        m_player.getCameraRenderUp();

    glm::mat4 view =
        glm::lookAt(
            renderCamPos,
            renderCamPos + front,
            renderCamUp
        );

    float aspect =
        (float)viewportWidth /
        (float)viewportHeight;

    // Draw/fog distance. Always 16 for a regular player: the multiplier is 1.0 without DevTools or
    // outside noclip. In noclip it is adjusted with +/- for cinematic shots.
    const float kBaseRenderDistance = 16.0f;
    m_currentRenderDistance = kBaseRenderDistance * getViewDistanceMultiplier();

    glm::mat4 proj =
        glm::perspective(
            glm::radians(63.0f),
            aspect,
            0.05f,
            std::max(50.0f, m_currentRenderDistance + 10.0f)
        );

    glUseProgram(m_program);

    glUniformMatrix4fv(
        m_uniView,
        1,
        GL_FALSE,
        glm::value_ptr(view)
    );

    glUniformMatrix4fv(
        m_uniProjection,
        1,
        GL_FALSE,
        glm::value_ptr(proj)
    );

    glUniform3fv(
        m_uniCamPos,
        1,
        glm::value_ptr(renderCamPos)
    );

    // The hand torch is static in camera space (see PlayerTorchViewmodel.h). scene.vert
    // (uIsViewmodelDraw) reconstructs world coordinates with the inverse view matrix, computed once
    // per frame here instead of once per vertex.

    const glm::mat4 invView = glm::inverse(view);
    glUniformMatrix4fv(
        m_uniInvView,
        1,
        GL_FALSE,
        glm::value_ptr(invView)
    );

    // Light originates from the camera position, not from the viewmodel's flame point. That point
    // sits ~0.46 units ahead and can end up inside a wall when the player stands close to it, which
    // made the light vanish near walls. The camera position can never be inside a wall.
    glUniform3fv(
        m_uniPlayerLightPos,
        1,
        glm::value_ptr(renderCamPos)
    );
    glUniform3fv(
        m_uniPlayerLightDir,
        1,
        glm::value_ptr(glm::normalize(front))
    );
    // Warm orange like the torch flames; a neutral white would read like a flashlight.
    glUniform3f(
        m_uniPlayerLightColor,
        1.0f, 0.60f, 0.15f
    );
    glUniform1f(
        m_uniPlayerLightIntensity,
        // torchBlend fades the light out while the torch is lowered, on the same curve as the model
        // animation.
        1.0f * m_player.torchBlend()
    );

    if (!m_devSpotlights.empty())
    {
        std::vector<glm::vec3> devLightPositions;
        std::vector<glm::vec3> devLightDirections;
        devLightPositions.reserve(m_devSpotlights.size());
        devLightDirections.reserve(m_devSpotlights.size());
        for (const DevSpotlight& light : m_devSpotlights)
        {
            devLightPositions.push_back(light.position);
            devLightDirections.push_back(light.direction);
        }
        glUniform3fv(m_uniDevLightPos, (int)devLightPositions.size(), glm::value_ptr(devLightPositions[0]));
        glUniform3fv(m_uniDevLightDir, (int)devLightDirections.size(), glm::value_ptr(devLightDirections[0]));
    }
    glUniform1i(m_uniDevLightCount, (int)m_devSpotlights.size());
    glUniform1i(m_uniDevPaletteOverride, m_devPaletteOverride);

    glUniform1f(
        m_uniTime,
        (float)glfwGetTime()
    );

    // Unreliable-vision torch tremble: index -1 means none. The shader's torchId == -1.0 test never
    // matches a real index (always >= 0).
    glUniform1f(m_uniGlitchTorchIndex, (float)m_torchGlitchIndex);
    glUniform1f(m_uniGlitchTorchUntilTime, (float)m_torchGlitchUntilTime);

    glUniform1f(
        m_uniRenderDistance,
        m_currentRenderDistance
    );

    glActiveTexture(
        GL_TEXTURE0
    );

    glBindTexture(
        GL_TEXTURE_2D,
        m_minimapFog.mapTexture()
    );

    glUniform1i(
        m_uniMapTex,
        0
    );

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_wallTex.id());
    glUniform1i(m_uniWallTex, 1);
    glUniform1f(m_uniWallTexEnabled, m_wallTex.id() != 0 ? 1.0f : 0.0f);
    glUniform1f(m_uniWallTexContrast, m_wallTex.contrast);
    glActiveTexture(GL_TEXTURE0);

    // Taken-torch lit mask on texture unit 2 (0 = map, 1 = wall texture).
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_torchLitMaskTex);
    glUniform1i(m_uniTorchLitMask, 2);
    glActiveTexture(GL_TEXTURE0);

    m_lighting.update(m_player.camPos(), m_currentRenderDistance, MAX_ACTIVE_TORCHES,
                       m_torchFlamePos, m_torchColor, m_torchIntensity,
                       glfwGetTime());

    const std::vector<glm::vec3>& activeTorchPos = m_lighting.activePositions();
    const std::vector<glm::vec3>& activeTorchColor = m_lighting.activeColors();
    const std::vector<float>& activeTorchIntensity = m_lighting.activeIntensities();
    int count = m_lighting.activeCount();

    if (count > 0)
    {
        glUniform3fv(
            m_uniTorchPos,
            count,
            glm::value_ptr(
                activeTorchPos[0]
            )
        );

        glUniform3fv(
            m_uniTorchColor,
            count,
            glm::value_ptr(
                activeTorchColor[0]
            )
        );

        glUniform1fv(
            m_uniTorchIntensity,
            count,
            activeTorchIntensity.data()
        );
    }

    glUniform1i(
        m_uniTorchCount,
        count
    );

    // Columns for the ray-vs-circle shadow test. They are rare, so the whole list is uploaded
    // (capped at the shader array size, MAX_COLUMNS) instead of picking the nearest N.

    {
        const int columnCount = std::min((int)m_columnCentersXZ.size(), 16); // MAX_COLUMNS in scene.frag
        if (columnCount > 0)
        {
            glUniform2fv(
                m_uniColumnPos,
                columnCount,
                glm::value_ptr(m_columnCentersXZ[0])
            );
        }
        glUniform1i(m_uniColumnCount, columnCount);
        glUniform1f(m_uniColumnRadius, Columns::kColumnRadius);
    }

    // Enemy XZ positions go into a small uniform array (no extra pass or texture), culled by
    // distance: each enemy multiplies the per-torch shadow cost, so distant ones that cannot affect
    // the frame are not uploaded.
    {
        const glm::vec3 camPosXZ = m_player.camPos();
        // margin for a torch right at the draw-distance edge
        const float cullRadius = m_currentRenderDistance + 2.0f;
        const float cullRadiusSq = cullRadius * cullRadius;

        auto withinCullRadius = [&](const glm::vec3& p) {
            const float dx = p.x - camPosXZ.x;
            const float dz = p.z - camPosXZ.z;
            return (dx * dx + dz * dz) <= cullRadiusSq;
        };

        m_enemyOccluderScratch.clear();
        for (int i = 0; i < kEnemyCount; ++i)
        {
            if (m_enemies[i].isLoaded())
            {
                const glm::vec3 p = m_enemyAIs[i].position();
                if (withinCullRadius(p))
                    m_enemyOccluderScratch.push_back(glm::vec2(p.x, p.z));
            }
        }
        if (m_devDummyEnemyActive && m_testEnemy.isLoaded())
        {
            const glm::vec3 p = m_testEnemy.position();
            if (withinCullRadius(p))
                m_enemyOccluderScratch.push_back(glm::vec2(p.x, p.z));
        }

        // MAX_ENEMY_OCCLUDERS in scene.frag
        const int enemyOccluderCount = std::min((int)m_enemyOccluderScratch.size(), 8);
        if (enemyOccluderCount > 0)
        {
            glUniform2fv(m_uniEnemyOccluderPosXZ, enemyOccluderCount,
                         glm::value_ptr(m_enemyOccluderScratch[0]));
        }
        glUniform1i(m_uniEnemyOccluderCount, enemyOccluderCount);

        // Radius is the silhouette's shoulder width; height is the in-world enemy height (bind pose
        // 1.75 * kModelScale 0.6 = 1.05). Keep in sync if kModelScale changes.
        const float kEnemyOccluderRadius = 0.30f;
        const float kEnemyOccluderHeight = 1.05f;
        glUniform1f(m_uniEnemyOccluderRadius, kEnemyOccluderRadius);
        glUniform1f(m_uniEnemyOccluderHeight, kEnemyOccluderHeight);
    }

    // Frustum and distance culling: each chunk's AABB (see buildGeometry()) is tested against the
    // camera frustum and the render/fog distance, so chunks behind the player or beyond the fog
    // cost no GPU work.

    glm::vec4 frustumPlanes[6];
    Culling::ExtractFrustumPlanes(proj * view, frustumPlanes);

    // Slack beyond the fog distance so chunks do not pop out while still fading in.
    const float cullDistance = m_currentRenderDistance + (float)SceneGeometry::kChunkSize;
    const float cullDistanceSq = cullDistance * cullDistance;

    auto chunkWithinRange = [&](const GeoChunk& c) {
        glm::vec3 closest = glm::clamp(renderCamPos, c.aabbMin, c.aabbMax);
        glm::vec3 delta = closest - renderCamPos;
        return glm::dot(delta, delta) <= cullDistanceSq;
    };

    // Visibility is computed once per chunk and shared by both draw passes.
    // m_chunkPvsMask[camChunk] is a conservative superset of possibly visible chunks
    // (buildChunkPVS(), computed at load); it complements the frustum/distance test. Point-sampled
    // occlusion culling was tried and removed: it produced black holes through narrow or diagonal
    // gaps.
    int camChunk = -1;
    if (m_geometry.pvsEnabled() && m_geometry.chunksX() > 0 && m_mapW > 0 && m_mapH > 0)
    {
        int camCellX = std::clamp((int)std::floor(renderCamPos.x), 0, m_mapW - 1);
        int camCellZ = std::clamp((int)std::floor(renderCamPos.z), 0, m_mapH - 1);
        camChunk = (camCellZ / SceneGeometry::kChunkSize) * m_geometry.chunksX() + (camCellX / SceneGeometry::kChunkSize);
        if (camChunk < 0 || camChunk >= (int)m_geometry.pvsMask().size())
            camChunk = -1;
    }
    const bool pvsUsable = (camChunk >= 0);
    const uint64_t camPvsMask = pvsUsable ? m_geometry.pvsMask()[(size_t)camChunk] : ~0ull;

    // Per-frame scratch vectors (m_chunkVisible, m_mainCounts, ...) are sized once by
    // reserveRenderScratchBuffers(); clear() keeps their capacity, so the push_backs below do not
    // allocate.
    for (size_t i = 0; i < m_geometry.chunks().size(); ++i)
    {
        const GeoChunk& c = m_geometry.chunks()[i];

        if ((c.mainIndexCount <= 0) && (c.particleCount <= 0))
        {
            m_chunkVisible[i] = 0;
            continue;
        }

        if (pvsUsable && !((camPvsMask >> i) & 1ull))
        {
            m_chunkVisible[i] = 0;
            continue;
        }

        if (!chunkWithinRange(c))
        {
            m_chunkVisible[i] = 0;
            continue;
        }

        if (!Culling::AabbInFrustum(c.aabbMin, c.aabbMax, frustumPlanes))
        {
            m_chunkVisible[i] = 0;
            continue;
        }

        m_chunkVisible[i] = 1;
    }

    // One glMultiDrawElements for all visible chunks instead of one glDrawElements per chunk, to
    // cut draw-call overhead.

    m_mainCounts.clear();
    m_mainOffsets.clear();

    for (size_t i = 0; i < m_geometry.chunks().size(); ++i)
    {
        const GeoChunk& chunk = m_geometry.chunks()[i];
        if (!m_chunkVisible[i] || chunk.mainIndexCount <= 0)
            continue;

        m_mainCounts.push_back(chunk.mainIndexCount);
        m_mainOffsets.push_back(
            (const GLvoid*)(uintptr_t)(chunk.mainIndexFirst * sizeof(GLuint))
        );
    }

    if (!m_mainCounts.empty())
    {
        glBindVertexArray(m_geometry.vao());

        glMultiDrawElements(
            GL_TRIANGLES,
            m_mainCounts.data(),
            GL_UNSIGNED_INT,
            m_mainOffsets.data(),
            (GLsizei)m_mainCounts.size()
        );

        glBindVertexArray(0);
    }

    // Stones (lying and flying) use the same program as the main geometry; the uniforms are already
    // set this frame. matId 0.0: plain diffuse geometry.
    if (m_stonePickupIndexCount > 0)
    {
        glBindVertexArray(m_stonePickupVao);
        glDrawElements(GL_TRIANGLES, m_stonePickupIndexCount, GL_UNSIGNED_INT, (const GLvoid*)0);
        glBindVertexArray(0);
    }
    if (m_thrownStoneIndexCount > 0)
    {
        glBindVertexArray(m_thrownStoneVao);
        glDrawElements(GL_TRIANGLES, m_thrownStoneIndexCount, GL_UNSIGNED_INT, (const GLvoid*)0);
        glBindVertexArray(0);
    }
    if (m_landmarkPropIndexCount > 0)
    {
        glBindVertexArray(m_landmarkPropVao);
        glDrawElements(GL_TRIANGLES, m_landmarkPropIndexCount, GL_UNSIGNED_INT, (const GLvoid*)0);
        glBindVertexArray(0);
    }
    if (m_winMonumentIndexCount > 0)
    {
        // Monument dissolve / spinning torus (WinSequenceState). Same VAO and shader as the props
        // above, so it gets the ASCII post-process. Blending and depth-write-off are enabled only
        // while alpha < 1: without depth writes the tube's far side shows through its near side.
        const bool actuallyFading = m_winMonumentFadeAlpha < 0.999f;
        glUniform1f(m_uniMonumentFadeAlpha, m_winMonumentFadeAlpha);
        if (actuallyFading)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }

        glBindVertexArray(m_winMonumentVao);
        glDrawElements(GL_TRIANGLES, m_winMonumentIndexCount, GL_UNSIGNED_INT, (const GLvoid*)0);
        glBindVertexArray(0);

        if (actuallyFading)
        {
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }

    // Perf diagnostics: triangles, chunks and active torches submitted this frame, read by
    // Application::tick()'s [perf] log so fps dips can be matched to what was on screen.
    {
        int totalIndices = 0;
        for (GLsizei c : m_mainCounts)
            totalIndices += (int)c;
        m_lastVisibleTriangles = totalIndices / 3;

        int visibleChunks = 0;
        for (char v : m_chunkVisible)
            if (v) visibleChunks++;
        m_lastVisibleChunkCount = visibleChunks;

        m_lastActiveTorchCount = count;
    }

    m_particleFirsts.clear();
    m_particleCounts.clear();

    for (size_t i = 0; i < m_geometry.chunks().size(); ++i)
    {
        const GeoChunk& chunk = m_geometry.chunks()[i];
        if (!m_chunkVisible[i] || chunk.particleCount <= 0)
            continue;

        m_particleFirsts.push_back(chunk.particleFirst);
        m_particleCounts.push_back(chunk.particleCount);
    }

    {
        int totalParticles = 0;
        for (GLsizei c : m_particleCounts)
            totalParticles += (int)c;
        m_lastVisibleParticles = totalParticles;
    }

    if (!m_particleFirsts.empty())
    {
        glEnable(
            GL_PROGRAM_POINT_SIZE
        );

        glEnable(
            GL_BLEND
        );

        glBlendFunc(
            GL_SRC_ALPHA,
            GL_ONE
        );

        glDepthMask(
            GL_FALSE
        );

        glBindVertexArray(
            m_geometry.particleVao()
        );

        glMultiDrawArrays(
            GL_POINTS,
            m_particleFirsts.data(),
            m_particleCounts.data(),
            (GLsizei)m_particleFirsts.size()
        );

        glBindVertexArray(
            0
        );

        glDepthMask(
            GL_TRUE
        );

        glDisable(
            GL_BLEND
        );

        glDisable(
            GL_PROGRAM_POINT_SIZE
        );
    }

    // Torch sway: inertia on turning plus an idle breathing motion (uViewmodelSway in scene.vert).
    // Computed from the difference between the current yaw and a lagging, smoothed copy of it.

    {
        const double nowTime = glfwGetTime();
        float dt = 0.0f;
        if (m_playerTorchLagInit)
        {
            // Clamp dt so one huge step after a pause or lag cannot make the smoothing snap.
            dt = (float)std::min(nowTime - m_playerTorchLastSwayTime, 0.1);
        }
        m_playerTorchLastSwayTime = nowTime;

        const float currentYaw = m_player.yaw();
        if (!m_playerTorchLagInit)
        {
            m_playerTorchLagYaw = currentYaw;
            m_playerTorchLagInit = true;
        }

        // Shortest angular difference (correct across the ±180° wrap).
        float yawDiff = currentYaw - m_playerTorchLagYaw;
        yawDiff = std::fmod(yawDiff + 540.0f, 360.0f) - 180.0f;

        // The lagging yaw chases the real one; a lower kLagSpeed means a longer, more visible lag
        // on sharp turns.
        const float kLagSpeed = 8.0f; // 1/sec
        m_playerTorchLagYaw += yawDiff * std::min(1.0f, dt * kLagSpeed);

        // Sign chosen so a left turn moves the torch right (+X in model space); flip it if the sway
        // goes the wrong way.
        const float kSwayScale = -0.010f;
        float rawTargetSway = yawDiff * kSwayScale;

        // Clamp the target amplitude: a sharp mouse flick (tens of degrees in one frame) would
        // otherwise push the torch out of view.
        const float kMaxSway = 0.08f;
        rawTargetSway = std::clamp(rawTargetSway, -kMaxSway, kMaxSway);

        // Second smoothing stage: the clamp bounds the target but yawDiff can still jump within one
        // frame on a sharp mouse move, so the visible offset chases the target through its own
        // continuous variable.
        const float kSwayVisualSpeed = 25.0f; // 1/sec: how fast the visible offset catches up to the target
        m_playerTorchSwayVisual +=
            (rawTargetSway - m_playerTorchSwayVisual) * std::min(1.0f, dt * kSwayVisualSpeed);

        const float swayX = m_playerTorchSwayVisual;

        // Idle breathing: a small time-based sine, independent of turning, so the torch is never
        // completely frozen.
        const float idleBobX = std::sin((float)nowTime * 1.3f) * 0.006f;
        const float idleBobY = std::sin((float)nowTime * 1.7f + 1.0f) * 0.005f;

        // Walk/run bob uses its own phase accumulator (m_playerTorchBobPhase) instead of time, so
        // the frequency follows footstep speed and the bob stops the moment the player does.
        // Running has a higher frequency and a larger amplitude.
        const bool isMoving = m_player.isMoving();
        const bool isRunning = m_player.isRunning();

        const float walkBobFreq = 6.0f;
        const float runBobFreq = 8.0f;
        const float bobFreq = isRunning ? runBobFreq : walkBobFreq;

        if (isMoving)
            m_playerTorchBobPhase += dt * bobFreq;

        const float walkBobAmp = 0.010f;
        const float runBobAmp = 0.016f;
        const float bobAmp = isRunning ? runBobAmp : walkBobAmp;

        const float targetMoveBlend = isMoving ? 1.0f : 0.0f;
        const float moveBlendSpeed = isRunning ? 12.0f : 10.0f;
        m_playerTorchMoveBlend += (targetMoveBlend - m_playerTorchMoveBlend) * std::min(1.0f, dt * moveBlendSpeed);

        const float walkRunBobY = m_playerTorchMoveBlend * std::sin(m_playerTorchBobPhase * 2.0f) * bobAmp;

        // Raise/lower on LMB (PlayerController::torchBlend()): shifts the model far enough down to
        // leave the frame when put away (0), rising smoothly toward 1.
        const float torchBlend = m_player.torchBlend();
        const float raiseOffsetY = -(1.0f - torchBlend) * 0.9f;

        glUniform2f(m_uniViewmodelSway, swayX + idleBobX, idleBobY + walkRunBobY + raiseOffsetY);
    }

    // The viewmodel geometry is static, but uIsViewmodelDraw must be set while drawing it so
    // scene.vert treats its coordinates as camera-local. Turn it off right after: otherwise the
    // rest of the geometry would also go through inverse(view), which is slower and wrong.

    glUniform1i(m_uniIsViewmodelDraw, 1);
    // Torch fuel (uViewmodelTorchFuel): set before draw() because the flame animation reads it in
    // the shader.
    glUniform1f(m_uniViewmodelTorchFuel, m_player.torchFuel());
    m_playerTorch.draw();
    glUniform1i(m_uniIsViewmodelDraw, 0);

    // Enemy pass: a separate shader (enemy.vert/enemy.frag), so glUseProgram is called again and
    // the shared values (camera, torches, columns, hand light, fog) are uploaded to that program's
    // uniform locations.

    // Whether anything needs drawing this frame: any real enemy or the dev dummy. Shared uniforms
    // (camera, lighting, torches, palette) are set once here, before the enemy loop.
    bool anyEnemyToDraw = m_devDummyEnemyActive && m_testEnemy.isLoaded();
    for (int i = 0; i < kEnemyCount && !anyEnemyToDraw; ++i)
        anyEnemyToDraw = m_enemies[i].isLoaded();
    // The glitch silhouette uses the same enemy pass; if it is the only thing active, the pass
    // still has to run.
    if (!anyEnemyToDraw)
        anyEnemyToDraw = m_silhouetteActiveTimer > 0.0f && m_glitchGhost.isLoaded();

    if (anyEnemyToDraw)
    {
        glUseProgram(m_enemyProgram);

        glUniformMatrix4fv(m_uEnemyView, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(m_uEnemyProjection, 1, GL_FALSE, glm::value_ptr(proj));
        glUniform3fv(m_uEnemyCamPos, 1, glm::value_ptr(renderCamPos));
        glUniform1f(m_uEnemyTime, (float)glfwGetTime());
        glUniform1f(m_uEnemyRenderDistance, m_currentRenderDistance);

        glUniform3fv(m_uEnemyPlayerLightPos, 1, glm::value_ptr(renderCamPos));
        glUniform3fv(m_uEnemyPlayerLightDir, 1, glm::value_ptr(glm::normalize(front)));
        glUniform3f(m_uEnemyPlayerLightColor, 1.0f, 0.60f, 0.15f);
        glUniform1f(m_uEnemyPlayerLightIntensity, 1.0f * m_player.torchBlend());

        if (!m_devSpotlights.empty())
        {
            std::vector<glm::vec3> devLightPositions;
            std::vector<glm::vec3> devLightDirections;
            devLightPositions.reserve(m_devSpotlights.size());
            devLightDirections.reserve(m_devSpotlights.size());
            for (const DevSpotlight& light : m_devSpotlights)
            {
                devLightPositions.push_back(light.position);
                devLightDirections.push_back(light.direction);
            }
            glUniform3fv(m_uEnemyDevLightPos, (int)devLightPositions.size(), glm::value_ptr(devLightPositions[0]));
            glUniform3fv(m_uEnemyDevLightDir, (int)devLightDirections.size(), glm::value_ptr(devLightDirections[0]));
        }
        glUniform1i(m_uEnemyDevLightCount, (int)m_devSpotlights.size());
        glUniform1i(m_uEnemyDevPaletteOverride, m_devPaletteOverride);

        if (count > 0)
        {
            glUniform3fv(m_uEnemyTorchPos, count, glm::value_ptr(activeTorchPos[0]));
            glUniform3fv(m_uEnemyTorchColor, count, glm::value_ptr(activeTorchColor[0]));
            glUniform1fv(m_uEnemyTorchIntensity, count, activeTorchIntensity.data());
        }
        glUniform1i(m_uEnemyTorchCount, count);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_minimapFog.mapTexture());
        glUniform1i(m_uEnemyMapTex, 0);

        // Enemy is drawn with vertex color only: the model is loaded without its diffuse texture
        // (SkinnedModel::load defaults to loadDiffuseTexture=false) and hasDiffuseTex is forced to
        // 0. Enabling the texture changes the enemy's look, so check it visually first. (Diffuse
        // unit 1; unit 0 is mapTex.)
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1i(m_uEnemyDiffuseTex, 1);
        glUniform1f(m_uEnemyHasDiffuseTex, 0.0f);

        const int enemyColumnCount = std::min((int)m_columnCentersXZ.size(), 16);
        if (enemyColumnCount > 0)
            glUniform2fv(m_uEnemyColumnPos, enemyColumnCount, glm::value_ptr(m_columnCentersXZ[0]));
        glUniform1i(m_uEnemyColumnCount, enemyColumnCount);
        glUniform1f(m_uEnemyColumnRadius, Columns::kColumnRadius);

        double nowT = glfwGetTime();
        float enemyDt = m_enemyUpdateInit ? (float)std::min(nowT - m_lastEnemyUpdateTime, 0.1) : 0.0f;
        m_lastEnemyUpdateTime = nowT;
        m_enemyUpdateInit = true;

        // Each of the kEnemyCount enemies has an independent AI instance. playerCanSeeThisEnemy is
        // computed per enemy (isEnemyVisibleToPlayer()) and only affects the minimap.
        if (gameplayActive)
        {
            // Same interval as the AI's own perception (kPerceptionInterval in EnemyAI.cpp).
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
                const bool playerSeesThis = m_cachedPlayerCanSeeEnemy[i];

                m_enemyAIs[i].update(
                    enemyDt, renderCamPos,
                    m_player.isRunning(), m_player.isMoving(),
                    m_player.invisibleToEnemy(), playerSeesThis,
                    m_player.lightLevel(),
                    m_enemies[i]);

                if (m_enemyAIs[i].consumeJustCaughtPlayer())
                {
                    m_player.applyCaughtDebuff();
                    // Fixed damage per Attack_Lunge hit: four hits kill at full health (100).
                    m_player.applyDamage(25.0f);
                }
            }

            // Tick stone physics after all enemies are updated: collisions need this frame's
            // positions.
            updateThrownStones(enemyDt);
        }

        for (int i = 0; i < kEnemyCount; ++i)
        {
            if (m_enemies[i].isLoaded())
                m_enemies[i].draw(m_uEnemyModel, m_uEnemyBoneMatrices);
        }

        // Dev dummy (K key): it does not move or think, but its clip clock must tick for Idle to
        // animate. EnemyAI::update() normally does that as its last step, so it is called directly.
        if (m_devDummyEnemyActive && gameplayActive)
        {
            m_testEnemy.update(enemyDt);
        }

        if (m_devDummyEnemyActive)
        {
            m_testEnemy.draw(m_uEnemyModel, m_uEnemyBoneMatrices);
        }

        // Flickering silhouette: position and pose are set in updateGlitchEffects(); it is drawn
        // here like the other enemies.
        if (m_silhouetteActiveTimer > 0.0f && m_glitchGhost.isLoaded())
        {
            m_glitchGhost.draw(m_uEnemyModel, m_uEnemyBoneMatrices);
        }

        glUseProgram(m_program);
    }

    glActiveTexture(
        GL_TEXTURE0
    );

    glBindTexture(
        GL_TEXTURE_2D,
        0
    );
}

// Called after ascii.end(), directly onto the window framebuffer, so the ASCII post-process never
// reprocesses the compass and blurs its crisp glyphs.
void DungeonScene::renderCompassOverlay(int viewportWidth, int viewportHeight)
{
    glViewport(0, 0, viewportWidth, viewportHeight);

    // Minimap enemy markers (enemies the player has seen): offset = enemy whole cell minus player
    // whole cell on X, reversed on Z (the minimap is north-up). Real enemies only; the dev dummy
    // has nothing to give away.
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
    // Debug map (M): positions of all real enemies, plus the dev dummy if spawned, to see where it
    // was placed.
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
