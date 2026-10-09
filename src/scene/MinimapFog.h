#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include "WallShapes.h"

// Fog of war and two textures: the full map (debug overlay) and the minimap drawn on the compass
class MinimapFog {
public:
    static const int kMinimapSize = 21;

    // Uploads the full map without fog. RGBA8:
    // R wall, G corner-cut type (0..4), B widened corridor, A diagonal chain
    void uploadMapTexture(int mapW, int mapH, const std::vector<int>& map,
                          const std::vector<WallShapes::CornerCut>& cornerCuts,
                          const std::vector<unsigned char>& corridorWidened,
                          const std::vector<unsigned char>& diagonalChainMask);

    // Reveals cells around the player and within the FOV cone into m_explored
    // Once seen, a cell stays revealed (fog of war) until the map is regenerated
    void revealVisibleCells(int mapW, int mapH,
                             const glm::vec3& camPos, float yaw,
                             const std::function<bool(int, int)>& isWall);

    // Reveals visible cells and rebuilds the minimap
    // Guide torch walls draw as a blue '*', read-diary walls in purple
    void updateMinimap(int mapW, int mapH, const std::vector<int>& map,
                        const glm::vec3& camPos, float yaw,
                        const std::function<bool(int, int)>& isWall,
                        const std::vector<unsigned char>& torchCellLookup,
                        const std::vector<unsigned char>& guideTorchCellLookup,
                        const std::vector<unsigned char>& diaryReadWallLookup);

    void destroy();

    GLuint mapTexture() const { return m_mapTexture; }
    GLuint minimapTexture() const { return m_minimapTexture; }

    void resetExplored(int mapW, int mapH) {
        m_explored.assign((size_t)mapW * mapH, 0);
    }

    // Fog of war for saves
    const std::vector<unsigned char>& explored() const { return m_explored; }
    void setExplored(std::vector<unsigned char> explored) {
        m_explored = std::move(explored);
    }

private:
    GLuint m_mapTexture = 0;
    GLuint m_minimapTexture = 0;

    // 0 = not revealed, 1 = revealed indexed like the map: z*mapW+x
    std::vector<unsigned char> m_explored;

    std::vector<unsigned char> m_minimapPixels;

    // True after the first upload; later uploads go through glTexSubImage2D
    bool m_minimapTextureAllocated = false;
};
