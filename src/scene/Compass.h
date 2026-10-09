#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>

// Screen-space 3D compass with an ASCII minimap on its top (V)
// Own shader after AsciiEffect::end()
class Compass {
public:
    void create();
    void destroy();

    void setUiFont(GLuint texture, int glyphCount) {
        m_uiFontTex = texture;
        m_uiGlyphCount = glyphCount;
    }

    // poseBlend 0..1 (no-op at 0), yaw for orientation, minimap texture from MinimapFog
    bool isReady() const { return m_program != 0; }

    // One entry per enemy: marker alpha and cell offset from the player (Z flipped)
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
