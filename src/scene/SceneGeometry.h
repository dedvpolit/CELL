#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <functional>
#include "WallShapes.h"

// Builds and owns the dungeon geometry (floor, walls, torch meshes) with its GL buffers, split into
// chunks for culling (Culling.h) plus a PVS (build()/buildPVS()). Unlike MapGenerator/MinimapFog it
// owns GL buffers because DungeonScene::render() reads them every frame.

// File-level (not nested in a class) because both DungeonScene::render() (chunks()) and the build
// code need them.
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    float matId;
};

// Small reusable mesh builders (cylinder/sphere), exposed so other modules can use them:
// PlayerTorchViewmodel.h builds the hand torch from the same primitives as the wall torches,
// recomputing vertices per frame in world coordinates relative to the camera.
void AddCylinder(std::vector<Vertex>& verts, std::vector<GLuint>& indices, glm::vec3 base, glm::vec3 tip,
                 float radiusBase, float radiusTip, int segments,
                 glm::vec3 color, float matId);
void AddSphere(std::vector<Vertex>& verts, std::vector<GLuint>& indices, glm::vec3 center, float radius,
               int segments, int rings, glm::vec3 color, float matId);

struct GeoChunk {
    // Main geometry (walls/floor/torches) is indexed: these are offsets/counts into the shared EBO,
    // not into the vertex buffer.
    GLint   mainIndexFirst = 0;
    GLsizei mainIndexCount = 0;
    // Particles stay non-indexed GL_POINTS (indexing buys nothing for a point cloud), so these are
    // vertex offsets into the particle VBO.
    GLint   particleFirst = 0;
    GLsizei particleCount = 0;
    glm::vec3 aabbMin{ 0.0f };
    glm::vec3 aabbMax{ 0.0f };
};

class SceneGeometry {
public:
    static const int kChunkSize = 16;

    // Builds the geometry from the map and torch data, split into kChunkSize x kChunkSize chunks,
    // and uploads it (plus a separate VAO/VBO for flame particles). cornerCuts/chamferSizes are
    // per-cell, mapW*mapH (WallShapes; kChamferSizeChain for diagonal chains); columnCentersXZ
    // cells must already be floor; wallColors/floorColors are final per-cell colors (empty =
    // palette 0).
    void build(int mapW, int mapH, const std::vector<int>& map,
               const glm::vec3& winButtonPos,
               const std::vector<glm::vec3>& torchWallBase,
               const std::vector<glm::vec3>& torchNormal,
               const std::vector<glm::vec3>& torchFlamePos,
               const std::vector<WallShapes::CornerCut>& cornerCuts,
               const std::vector<glm::vec2>& columnCentersXZ,
               const std::vector<float>& chamferSizes,
               const std::vector<glm::vec3>& wallColors,
               const std::vector<glm::vec3>& floorColors,
               const std::vector<glm::vec3>& diaryPositions = {},
               // Indices >= this are landmark torches (see DungeonScene::placeLandmarks()): the
               // same handle and flame but with a blue flame (and no ember particles), a visual "go
               // here" signal separate from regular lighting. -1 (default) = none, all torches are
               // regular.
               int firstGuideTorchIndex = -1);

    // Wall/floor colors of the pre-tuned palettes by index. Public static because DungeonScene also
    // reads them directly when blending neighboring zones, before calling build().
    static void GetZonePalette(int paletteIndex, glm::vec3& outWallColor, glm::vec3& outFloorColor);

    // Builds a conservative PVS: flood-fill across floor cells from each chunk. Disabled
    // (pvsEnabled() == false) with more than 64 chunks (a chunk mask is one uint64_t); render()
    // then skips the PVS prefilter and relies on frustum + distance culling.
    void buildPVS(int mapW, int mapH, const std::function<bool(int, int)>& isWall);

    void destroy();

    GLuint vao() const { return m_vao; }
    GLsizei indexCount() const { return m_indexCount; }

    GLsizei vertexCount() const { return m_vertexCount; }

    GLuint particleVao() const { return m_particleVao; }

    const std::vector<GeoChunk>& chunks() const { return m_chunks; }
    int chunksX() const { return m_chunksX; }

    const std::vector<uint64_t>& pvsMask() const { return m_chunkPvsMask; }
    bool pvsEnabled() const { return m_chunkPvsEnabled; }

private:
    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    GLsizei m_vertexCount = 0;
    GLsizei m_indexCount = 0;

    GLuint m_particleVao = 0, m_particleVbo = 0;
    GLsizei m_particleVertexCount = 0;

    std::vector<GeoChunk> m_chunks;
    int m_chunksX = 0;

    std::vector<uint64_t> m_chunkPvsMask;
    bool m_chunkPvsEnabled = false;
};
