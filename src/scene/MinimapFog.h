#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include "WallShapes.h"

// Fog of war (which cells the player has seen) and two textures built from it: the full map
// (mapTexture(), used by the debug overlay and scene.frag) and the minimap (minimapTexture(), an N
// x N area around the player drawn on the compass). It owns no map data: methods take the map,
// camera and predicates as parameters.
class MinimapFog {
public:
    static const int kMinimapSize = 21;

    // (Re)uploads m_mapTexture: the full map, ignoring fog. GL_RGBA8: R = wall (1.0) / floor (0.0);
    // G = corner-cut type (WallShapes::CornerCut) as an unscaled integer 0..4; B = floor made by
    // corridor widening; A = cut forced by a diagonal chain. scene.frag needs R and G,
    // debug_map.frag all four.
    void uploadMapTexture(int mapW, int mapH, const std::vector<int>& map,
                          const std::vector<WallShapes::CornerCut>& cornerCuts,
                          const std::vector<unsigned char>& corridorWidened,
                          const std::vector<unsigned char>& diagonalChainMask);

    // Reveals cells around the player and within the FOV cone into m_explored. Once seen, a cell
    // stays revealed (fog of war) until the map is regenerated.
    void revealVisibleCells(int mapW, int mapH,
                             const glm::vec3& camPos, float yaw,
                             const std::function<bool(int, int)>& isWall);

    // Reveals cells (revealVisibleCells()), then rebuilds m_minimapTexture from m_explored.
    // guideTorchCellLookup: wall cells with a landmark torch, drawn as a blue '*'; empty means
    // none. diaryReadWallLookup: walls around pockets whose diary was read, drawn purple; it
    // changes at runtime.
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

    // Save/load (see save/SaveSystem.h): lets the caller take the fog as a whole (to write to a
    // save) and restore it on CONTINUE; otherwise after loading, the minimap would show only what
    // is visible from the current frame, not everything already explored.
    const std::vector<unsigned char>& explored() const { return m_explored; }
    void setExplored(std::vector<unsigned char> explored) {
        m_explored = std::move(explored);
    }

private:
    GLuint m_mapTexture = 0;
    GLuint m_minimapTexture = 0;

    // 0 = not revealed, 1 = revealed. Indexed like the map: z*mapW+x.
    std::vector<unsigned char> m_explored;

    std::vector<unsigned char> m_minimapPixels;

    // Once the minimap texture exists at its fixed resolution (kMinimapSize), re-uploads use
    // glTexSubImage2D (overwrite the storage) instead of reallocating with glTexImage2D. True only
    // after the first successful upload.
    bool m_minimapTextureAllocated = false;
};
