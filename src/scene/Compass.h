#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>

// Screen-space 3D compass with an ASCII minimap on its top disc (V key; DungeonScene owns the
// toggle and the appear/hide animation). Own shader (compass.{vert,frag}) after AsciiEffect::end(),
// so it skips the ASCII post-process. render() takes no camera position: it is a pure screen
// overlay.
class Compass {
public:
    void create();
    void destroy();

    void setUiFont(GLuint texture, int glyphCount) {
        m_uiFontTex = texture;
        m_uiGlyphCount = glyphCount;
    }

    // poseBlend: 0..1, how "drawn out" the compass is (no-op at poseBlend <= 0). yawDeg: player
    // rotation, for the minimap orientation. minimapTexture: see MinimapFog::minimapTexture().
    bool isReady() const { return m_program != 0; }

    // enemySpottedAlphas/enemyMinimapOffsets: one entry per enemy (kEnemyCount). Alpha is
    // EnemyAI::spottedMarkerAlpha(); the offset is the enemy's cell minus the player's on X and
    // reversed on Z, computed on the CPU in renderCompassOverlay(). Passed by value (small, fixed
    // size).
    void render(float poseBlend, float yawDeg,
                GLuint minimapTexture, bool colorEnabled,
                std::vector<float> enemySpottedAlphas,
                std::vector<glm::vec2> enemyMinimapOffsets);

private:
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLsizei m_vertexCount = 0;

    GLuint m_uiFontTex = 0;
    int m_uiGlyphCount = 13;

    GLint m_uniScreenCenter = -1;
    GLint m_uniScreenScale = -1;
    GLint m_uniView = -1;
    GLint m_uniProjection = -1;
    GLint m_uniMinimapTex = -1;
    GLint m_uniMinimapYawDeg = -1;
    GLint m_uniUiFontTex = -1;
    GLint m_uniUiGlyphCount = -1;
    GLint m_uniColorEnabled = -1;
    GLint m_uniEnemySpottedAlpha = -1;
    GLint m_uniEnemyMinimapOffset = -1;
    GLint m_uniEnemyCount = -1;
    void cacheUniformLocations();
};
