#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include "Zoning.h"

// Debug panel (M): the unfogged maze with the player marker
// Own shader after AsciiEffect::end(); owns no map data
class DebugMapOverlay {
public:
    void create();
    void destroy();

    // No-op when hidden
    // Draws the map, column and zone markers, enemies and props
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
