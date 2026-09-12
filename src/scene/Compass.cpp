#include "Compass.h"
#include "MinimapFog.h"
#include "render/ShaderLoader.h"
#include "render/ShaderProgram.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstddef>

struct CompassVertexCPU {
    glm::vec3 pos;
    glm::vec3 normal;
    float type;
};

static void compassAddCylinder(
    std::vector<CompassVertexCPU>& out,
    const glm::vec3& a,
    const glm::vec3& b,
    float radius,
    float type,
    int segments,
    const glm::vec3& tintNormal)
{
    glm::vec3 axis = b - a;
    float len = glm::length(axis);
    if (len < 0.0001f) return;

    glm::vec3 w = glm::normalize(axis);
    glm::vec3 helper = (std::abs(w.y) < 0.9f)
        ? glm::vec3(0,1,0)
        : glm::vec3(1,0,0);
    glm::vec3 u = glm::normalize(glm::cross(w, helper));
    glm::vec3 v = glm::normalize(glm::cross(w, u));

    for (int i = 0; i < segments; ++i) {
        float a0 = glm::two_pi<float>() * (float)i / (float)segments;
        float a1 = glm::two_pi<float>() * (float)(i + 1) / (float)segments;

        glm::vec3 n0 = glm::normalize(u * std::cos(a0) + v * std::sin(a0));
        glm::vec3 n1 = glm::normalize(u * std::cos(a1) + v * std::sin(a1));

        glm::vec3 p00 = a + n0 * radius;
        glm::vec3 p01 = a + n1 * radius;
        glm::vec3 p10 = b + n0 * radius;
        glm::vec3 p11 = b + n1 * radius;

        out.push_back({p00, n0, type});
        out.push_back({p10, n0, type});
        out.push_back({p11, n1, type});

        out.push_back({p00, n0, type});
        out.push_back({p11, n1, type});
        out.push_back({p01, n1, type});
    }
}

static void compassAddSphere(
    std::vector<CompassVertexCPU>& out,
    const glm::vec3& c,
    float r,
    float type,
    int segments,
    int rings)
{
    for (int y = 0; y < rings; ++y) {
        float v0 = (float)y / (float)rings;
        float v1 = (float)(y + 1) / (float)rings;
        float phi0 = glm::pi<float>() * v0 - glm::half_pi<float>();
        float phi1 = glm::pi<float>() * v1 - glm::half_pi<float>();

        for (int x = 0; x < segments; ++x) {
            float u0 = glm::two_pi<float>() * (float)x / (float)segments;
            float u1 = glm::two_pi<float>() * (float)(x + 1) / (float)segments;

            glm::vec3 n00(std::cos(phi0)*std::cos(u0), std::sin(phi0), std::cos(phi0)*std::sin(u0));
            glm::vec3 n01(std::cos(phi0)*std::cos(u1), std::sin(phi0), std::cos(phi0)*std::sin(u1));
            glm::vec3 n10(std::cos(phi1)*std::cos(u0), std::sin(phi1), std::cos(phi1)*std::sin(u0));
            glm::vec3 n11(std::cos(phi1)*std::cos(u1), std::sin(phi1), std::cos(phi1)*std::sin(u1));

            out.push_back({c + n00*r, n00, type});
            out.push_back({c + n10*r, n10, type});
            out.push_back({c + n11*r, n11, type});
            out.push_back({c + n00*r, n00, type});
            out.push_back({c + n11*r, n11, type});
            out.push_back({c + n01*r, n01, type});
        }
    }
}

void Compass::create()
{
    std::vector<CompassVertexCPU> verts;
    verts.reserve(4096);

    // Compass body: very thin cylinder, like a pocket compass.
    compassAddCylinder(
        verts,
        glm::vec3(0.0f, -0.030f, 0.0f),
        glm::vec3(0.0f,  0.030f, 0.0f),
        0.250f,
        0.0f,
        32,
        glm::vec3(0,1,0)
    );

    // Top disk is rendered as ASCII map by the compass fragment shader.
    const int diskSegments = 64;
    const float r = 0.235f;
    for (int i = 0; i < diskSegments; ++i) {
        const float a0 = glm::two_pi<float>() * (float)i / diskSegments;
        const float a1 = glm::two_pi<float>() * (float)(i + 1) / diskSegments;

        glm::vec3 c(0.0f, 0.033f, 0.0f);
        glm::vec3 p0(r * std::cos(a0), 0.033f, r * std::sin(a0));
        glm::vec3 p1(r * std::cos(a1), 0.033f, r * std::sin(a1));

        verts.push_back({c,  glm::vec3(0,1,0), 2.0f});
        verts.push_back({p0, glm::vec3(0,1,0), 2.0f});
        verts.push_back({p1, glm::vec3(0,1,0), 2.0f});
    }

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        (GLsizeiptr)(verts.size() * sizeof(CompassVertexCPU)),
        verts.data(),
        GL_STATIC_DRAW
    );
    m_vertexCount = static_cast<GLsizei>(verts.size());

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE,
        sizeof(CompassVertexCPU),
        (void*)offsetof(CompassVertexCPU, pos)
    );

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE,
        sizeof(CompassVertexCPU),
        (void*)offsetof(CompassVertexCPU, normal)
    );

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2, 1, GL_FLOAT, GL_FALSE,
        sizeof(CompassVertexCPU),
        (void*)offsetof(CompassVertexCPU, type)
    );

    glBindVertexArray(0);

    // Compile the dedicated compass shader.
    const std::string compassVertSrc = ShaderLoader::LoadSource("assets/shaders/compass.vert");
    const std::string compassFragSrc = ShaderLoader::LoadSource("assets/shaders/compass.frag");
    GLuint vs = ShaderProgram::CompileShader(GL_VERTEX_SHADER, compassVertSrc.c_str(), "Compass");
    GLuint fs = ShaderProgram::CompileShader(GL_FRAGMENT_SHADER, compassFragSrc.c_str(), "Compass");
    m_program = ShaderProgram::LinkProgram(vs, fs, "Compass");

    cacheUniformLocations();
}

void Compass::cacheUniformLocations()
{
    if (!m_program)
        return;

    m_uniScreenCenter      = glGetUniformLocation(m_program, "screenCenter");
    m_uniScreenScale       = glGetUniformLocation(m_program, "screenScale");
    m_uniView              = glGetUniformLocation(m_program, "view");
    m_uniProjection        = glGetUniformLocation(m_program, "projection");
    m_uniMinimapTex        = glGetUniformLocation(m_program, "minimapTex");
    m_uniMinimapYawDeg     = glGetUniformLocation(m_program, "minimapYawDeg");
    m_uniMinimapFontTex    = glGetUniformLocation(m_program, "minimapFontTex");
    m_uniMinimapGlyphCount = glGetUniformLocation(m_program, "minimapGlyphCount");
    m_uniColorEnabled      = glGetUniformLocation(m_program, "colorEnabled");
    m_uniEnemySpottedAlpha   = glGetUniformLocation(m_program, "enemySpottedAlpha");
    m_uniEnemyMinimapOffset  = glGetUniformLocation(m_program, "enemyMinimapOffset");
    m_uniEnemyCount          = glGetUniformLocation(m_program, "enemyCount");
}

void Compass::destroy()
{
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }

    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }

    m_vertexCount = 0;

    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

void Compass::render(float poseBlend, float yawDeg,
                     GLuint minimapTexture, bool colorEnabled,
                     std::vector<float> enemySpottedAlphas,
                     std::vector<glm::vec2> enemyMinimapOffsets)
{
    if (!m_program || !m_vao || !minimapTexture)
        return;

    // Nothing is rendered when the compass is fully put away.
    if (poseBlend <= 0.0001f)
        return;

    // Compass is a held first-person object.
    // It must never be occluded by dungeon walls/floor.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    // The compass fades/slides in with a small delay. Both directions use
    // smootherstep, so taking it out and putting it away are visibly
    // non-linear and never snap.
    const float handT = glm::clamp((poseBlend - 0.14f) / 0.86f, 0.0f, 1.0f);
    const float e =
        handT * handT * handT *
        (handT * (handT * 6.0f - 15.0f) + 10.0f);

    // Screen-space coordinates.
    // X > 0  = right
    // Y < 0  = down
    const float hiddenX = 1.35f;
    const float hiddenY = -1.35f;

    const float visibleX = 1.15f;
    const float visibleY = -0.6f;

    const float screenX = glm::mix(hiddenX, visibleX, e);
    const float screenY = glm::mix(hiddenY, visibleY, e);

    glUseProgram(m_program);

    glUniform2f(
        m_uniScreenCenter,
        screenX,
        screenY
    );

    glUniform1f(
        m_uniScreenScale,
        1.5f
    );

    // View/projection are already available in the active frame; reconstruct
    // them here so the compass can be rendered after the dungeon geometry.
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    const float aspect = (float)viewport[2] / (float)viewport[3];

    glm::mat4 view = glm::mat4(1.0f);

    glm::mat4 proj =
        glm::ortho(
            -aspect,
            aspect,
            -1.0f,
            1.0f,
            -10.0f,
            10.0f
        );

    glUniformMatrix4fv(
        m_uniView,
        1, GL_FALSE, glm::value_ptr(view)
    );
    glUniformMatrix4fv(
        m_uniProjection,
        1, GL_FALSE, glm::value_ptr(proj)
    );

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, minimapTexture);
    glUniform1i(
        m_uniMinimapTex,
        0
    );
    glUniform1f(
        m_uniMinimapYawDeg,
        yawDeg
    );

    // Same glyph atlas the old flat ASCII minimap used, shared via
    // AsciiEffect so the two views never drift apart visually.
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_minimapFontTex);
    glUniform1i(
        m_uniMinimapFontTex,
        1
    );
    glUniform1f(
        m_uniMinimapGlyphCount,
        (float)m_minimapGlyphCount
    );

    glUniform1f(
        m_uniColorEnabled,
        colorEnabled ? 1.0f : 0.0f
    );

    // УЛУЧШЕНИЕ ("4 врага по всей карте", "на мини-карте отображать
    // врага, если игрок его уже видел", "плавное затухание") — массивы
    // вместо одного значения, тот же приём заливки, что и у
    // devLightPos[8] в DungeonScene.cpp (glUniform*fv на базовый
    // location индекса 0). enemySpottedAlphas уже посчитаны на CPU (см.
    // EnemyAI::spottedMarkerAlpha() для каждого врага), шейдеру
    // остаётся только смешать со своим обычным цветом клетки (см.
    // compass.frag).
    const int enemyCount = (int)std::min(enemySpottedAlphas.size(), enemyMinimapOffsets.size());
    glUniform1i(m_uniEnemyCount, enemyCount);
    if (enemyCount > 0) {
        glUniform1fv(m_uniEnemySpottedAlpha, enemyCount, enemySpottedAlphas.data());
        glUniform2fv(m_uniEnemyMinimapOffset, enemyCount, glm::value_ptr(enemyMinimapOffsets[0]));
    }

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    // Restore normal dungeon depth state.
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
