#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <functional>
#include "WallShapes.h"

// Dungeon geometry (floor, walls, torch meshes) and its GL buffers
// split into chunks for culling, plus a chunk PVS

// File-level (not nested in a class) because both DungeonScene::render() (chunks()) and the build code need them
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    float matId;
};

// Mesh builders shared with the hand torch
void AddCylinder(std::vector<Vertex>& verts, std::vector<GLuint>& indices, glm::vec3 base, glm::vec3 tip,
                 float radiusBase, float radiusTip, int segments,
                 glm::vec3 color, float matId);
void AddSphere(std::vector<Vertex>& verts, std::vector<GLuint>& indices, glm::vec3 center, float radius,
               int segments, int rings, glm::vec3 color, float matId);

struct GeoChunk {
    // Main geometry (walls/floor/torches) is indexed:
    // these are offsets/counts into the shared EBO, not into the vertex buffer
    GLint   mainIndexFirst = 0;
    GLsizei mainIndexCount = 0;
    // Particles stay non-indexed GL_POINTS (indexing buys nothing for a point cloud)
    // these are vertex offsets into the particle VBO
    GLint   particleFirst = 0;
    GLsizei particleCount = 0;
    glm::vec3 aabbMin{ 0.0f };
    glm::vec3 aabbMax{ 0.0f };
};

class SceneGeometry {
public:
    static const int kChunkSize = 16;

    // Builds chunked geometry and uploads it, with a separate VAO for flame particles
    // cornerCuts/chamferSizes are per cell
    void build(int mapW, int mapH, const std::vector<int>& map,
               const glm::vec3& exitDoorPos,
               const std::vector<glm::vec3>& torchWallBase,
               const std::vector<glm::vec3>& torchNormal,
               const std::vector<glm::vec3>& torchFlamePos,
               const std::vector<WallShapes::CornerCut>& cornerCuts,
               const std::vector<glm::vec2>& columnCentersXZ,
               const std::vector<float>& chamferSizes,
               const std::vector<glm::vec3>& wallColors,
               const std::vector<glm::vec3>& floorColors,
               const std::vector<glm::vec3>& diaryPositions = {},
               // Indices >= this are guide torches:
               // blue flame, no sparks. -1 = none
               int firstGuideTorchIndex = -1);

    // Palette colors; DungeonScene reads them to blend zones
    static void GetZonePalette(int paletteIndex, glm::vec3& outWallColor, glm::vec3& outFloorColor);

    // Conservative PVS by flood fill; disabled above 64 chunks
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
// CELL-CELL-CELL-CELL-CELL-CELL-CELL-CELL-CELL-CELL-CELL-CELL-CELL-CELL-CELL-CELL-CELL-CELL-CELL-CELL-CELL
