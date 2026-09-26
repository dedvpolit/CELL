#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include "Zoning.h"

// Debug panel (M key, beta testing): the whole maze without fog of war in a screen corner, with the
// player's position and direction. Own shader (debug_map.{vert,frag}) after AsciiEffect::end(). It
// owns no map data (render() takes it as parameters); visibility is DungeonScene input state.
// Chamfers are in mapTex's G channel; columns and zones are not in mapTex, so they get separate
// draw passes.
class DebugMapOverlay {
public:
    void create();
    void destroy();

    // No-op if visible == false. mapTexture is the full map. columnCentersXZ: column markers (a
    // column cell in mapTex is plain floor). zoneGrid: a swatch at each region center colored by
    // chamferProbability (blue low, red high). enemyPositions: red markers, distinct from the green
    // of columns and the win button.
    void render(int viewportWidth, int viewportHeight, bool visible,
                GLuint mapTexture, int mapW, int mapH,
                const glm::vec3& camPos, float yaw,
                const std::vector<glm::vec2>& columnCentersXZ,
                const Zoning::ZoneGrid& zoneGrid,
                const std::vector<glm::vec3>& enemyPositions,
                const std::vector<glm::vec2>& landmarkPropPositionsXZ = {});

private:
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    GLint m_uniMode = -1;
    GLint m_uniColor = -1;
    GLint m_uniAlpha = -1;
    GLint m_uniTex = -1;
    void cacheUniformLocations();
};
