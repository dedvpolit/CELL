#include "DebugMapOverlay.h"
#include "render/ShaderLoader.h"
#include "render/ShaderProgram.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <string>

void DebugMapOverlay::create()
{
    const std::string debugVertSrc = ShaderLoader::LoadSource("assets/shaders/debug_map.vert");
    const std::string debugFragSrc = ShaderLoader::LoadSource("assets/shaders/debug_map.frag");
    GLuint vs = ShaderProgram::CompileShader(GL_VERTEX_SHADER, debugVertSrc.c_str(), "DebugMap");
    GLuint fs = ShaderProgram::CompileShader(GL_FRAGMENT_SHADER, debugFragSrc.c_str(), "DebugMap");
    m_program = ShaderProgram::LinkProgram(vs, fs, "DebugMap");
    cacheUniformLocations();

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // The buffer is reused every frame for three different shapes (background, textured map, player
    // marker), so this only reserves a small size for at most 6 vertices; the data is re-uploaded
    // in renderDebugMap() before each draw call.
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

void DebugMapOverlay::cacheUniformLocations()
{
    if (!m_program)
        return;

    m_uniMode  = glGetUniformLocation(m_program, "uMode");
    m_uniColor = glGetUniformLocation(m_program, "uColor");
    m_uniAlpha = glGetUniformLocation(m_program, "uAlpha");
    m_uniTex   = glGetUniformLocation(m_program, "uMapTex");
}

void DebugMapOverlay::destroy()
{
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

void DebugMapOverlay::render(int viewportWidth, int viewportHeight, bool visible,
                              GLuint mapTexture, int mapW, int mapH,
                              const glm::vec3& camPos, float yaw,
                              const std::vector<glm::vec2>& columnCentersXZ,
                              const Zoning::ZoneGrid& zoneGrid,
                              const std::vector<glm::vec3>& enemyPositions,
                              const std::vector<glm::vec2>& landmarkPropPositionsXZ)
{
    if (!visible)
        return;

    if (!m_program || !m_vao || !mapTexture)
        return;

    if (viewportWidth <= 0 || viewportHeight <= 0)
        return;

    glViewport(0, 0, viewportWidth, viewportHeight);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const float vw = (float)viewportWidth;
    const float vh = (float)viewportHeight;

    auto toNdcX = [&](float px) { return (px / vw) * 2.0f - 1.0f; };
    auto toNdcY = [&](float py) { return 1.0f - (py / vh) * 2.0f; };

    const float margin = 24.0f;
    const float availW = vw * 0.5f - margin * 2.0f;
    const float availH = vh - margin * 2.0f;
    const float boxSize = std::max(50.0f, std::min(availW, availH));
    const float boxLeftPx = margin;
    const float boxTopPx = (vh - boxSize) * 0.5f;

    glUseProgram(m_program);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    const GLint locMode  = m_uniMode;
    const GLint locColor = m_uniColor;
    const GLint locAlpha = m_uniAlpha;
    const GLint locTex   = m_uniTex;

    auto drawQuadNdc = [&](float x0, float y0, float x1, float y1,
                            float u0, float v0, float u1, float v1)
    {
        float verts[6 * 4] = {
            x0, y0, u0, v0,
            x1, y0, u1, v0,
            x1, y1, u1, v1,

            x0, y0, u0, v0,
            x1, y1, u1, v1,
            x0, y1, u0, v1,
        };
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };

    glUniform1i(locMode, 1);
    glUniform3f(locColor, 0.02f, 0.02f, 0.03f);
    glUniform1f(locAlpha, 0.72f);
    {
        const float pad = 8.0f;
        float x0 = toNdcX(boxLeftPx - pad);
        float y0 = toNdcY(boxTopPx - pad);
        float x1 = toNdcX(boxLeftPx + boxSize + pad);
        float y1 = toNdcY(boxTopPx + boxSize + pad);
        drawQuadNdc(x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f);
    }

    glUniform1i(locMode, 0);
    glUniform1f(locAlpha, 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mapTexture);
    glUniform1i(locTex, 0);
    {
        float x0 = toNdcX(boxLeftPx);
        float y0 = toNdcY(boxTopPx);
        float x1 = toNdcX(boxLeftPx + boxSize);
        float y1 = toNdcY(boxTopPx + boxSize);
        drawQuadNdc(x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    // Zoning: a marker at each region center colored by chamferProbability (blue low, red high).
    // Drawing exact Voronoi borders would need a quad per cell or another texture channel; centers
    // are enough to show that regions exist and differ.
    if (mapW > 0 && mapH > 0 && !zoneGrid.centers.empty())
    {
        glUniform1i(locMode, 1);
        glUniform1f(locAlpha, 0.9f);

        // Marker radius is proportional to a region's "radius" under even coverage, so neighboring
        // markers do not merge and roughly hint at the region scale.
        const float regionRadiusCells = std::sqrt((float)Zoning::kTargetRegionArea / 3.14159265f);
        const float markerSize = std::max(4.0f, (regionRadiusCells / (float)mapW) * boxSize * 0.5f);

        for (size_t i = 0; i < zoneGrid.centers.size(); ++i)
        {
            const Zoning::ZoneStyle& style = zoneGrid.styles[i];
            const float t = glm::clamp(
                (style.chamferProbability - Zoning::kChamferProbabilityMin) /
                    (Zoning::kChamferProbabilityMax - Zoning::kChamferProbabilityMin),
                0.0f, 1.0f);
            glUniform3f(locColor, t, 0.25f, 1.0f - t);

            const float u = glm::clamp(zoneGrid.centers[i].x / (float)mapW, 0.0f, 1.0f);
            const float v = glm::clamp(zoneGrid.centers[i].z / (float)mapH, 0.0f, 1.0f);
            const float cxPx = boxLeftPx + u * boxSize;
            const float cyPx = boxTopPx + v * boxSize;

            drawQuadNdc(
                toNdcX(cxPx - markerSize * 0.5f), toNdcY(cyPx - markerSize * 0.5f),
                toNdcX(cxPx + markerSize * 0.5f), toNdcY(cyPx + markerSize * 0.5f),
                0, 0, 0, 0);
        }
    }

    // Columns: separate markers; a column cell in mapTex is indistinguishable from a regular floor,
    // so without them columns would not be visible on the debug map at all.
    if (mapW > 0 && mapH > 0 && !columnCentersXZ.empty())
    {
        glUniform1i(locMode, 1);
        glUniform3f(locColor, 0.25f, 0.95f, 0.35f); // green — same tone as the win button
        glUniform1f(locAlpha, 1.0f);

        const float markerSize = std::max(3.0f, std::min(boxSize / mapW, boxSize / mapH) * 0.9f);
        for (const glm::vec2& col : columnCentersXZ)
        {
            const float u = glm::clamp(col.x / (float)mapW, 0.0f, 1.0f);
            const float v = glm::clamp(col.y / (float)mapH, 0.0f, 1.0f);
            const float cx = boxLeftPx + u * boxSize;
            const float cy = boxTopPx + v * boxSize;
            drawQuadNdc(
                toNdcX(cx - markerSize * 0.5f), toNdcY(cy - markerSize * 0.5f),
                toNdcX(cx + markerSize * 0.5f), toNdcY(cy + markerSize * 0.5f),
                0, 0, 0, 0);
        }
    }

    if (mapW > 0 && mapH > 0 && !enemyPositions.empty())
    {
        glUniform1i(locMode, 1);
        glUniform3f(locColor, 0.95f, 0.20f, 0.20f);
        glUniform1f(locAlpha, 1.0f);

        const float markerSize = std::max(4.0f, std::min(boxSize / mapW, boxSize / mapH) * 1.3f);
        for (const glm::vec3& enemyPos : enemyPositions)
        {
            const float u = glm::clamp(enemyPos.x / (float)mapW, 0.0f, 1.0f);
            const float v = glm::clamp(enemyPos.z / (float)mapH, 0.0f, 1.0f);
            const float cx = boxLeftPx + u * boxSize;
            const float cy = boxTopPx + v * boxSize;
            drawQuadNdc(
                toNdcX(cx - markerSize * 0.5f), toNdcY(cy - markerSize * 0.5f),
                toNdcX(cx + markerSize * 0.5f), toNdcY(cy + markerSize * 0.5f),
                0, 0, 0, 0);
        }
    }

    if (mapW > 0 && mapH > 0 && !landmarkPropPositionsXZ.empty())
    {
        glUniform1i(locMode, 1);
        glUniform3f(locColor, 0.95f, 0.85f, 0.15f);
        glUniform1f(locAlpha, 1.0f);

        const float markerSize = std::max(4.0f, std::min(boxSize / mapW, boxSize / mapH) * 1.1f);
        for (const glm::vec2& p : landmarkPropPositionsXZ)
        {
            const float u = glm::clamp(p.x / (float)mapW, 0.0f, 1.0f);
            const float v = glm::clamp(p.y / (float)mapH, 0.0f, 1.0f);
            const float cx = boxLeftPx + u * boxSize;
            const float cy = boxTopPx + v * boxSize;
            drawQuadNdc(
                toNdcX(cx - markerSize * 0.5f), toNdcY(cy - markerSize * 0.5f),
                toNdcX(cx + markerSize * 0.5f), toNdcY(cy + markerSize * 0.5f),
                0, 0, 0, 0);
        }
    }

    if (mapW > 0 && mapH > 0)
    {
        const float u = glm::clamp(camPos.x / (float)mapW, 0.0f, 1.0f);
        const float v = glm::clamp(camPos.z / (float)mapH, 0.0f, 1.0f);

        const float centerPx = boxLeftPx + u * boxSize;
        const float centerPy = boxTopPx + v * boxSize;

        const float yawRad = glm::radians(yaw);
        const glm::vec2 frontDir(std::cos(yawRad), std::sin(yawRad));
        const glm::vec2 rightDir(-frontDir.y, frontDir.x);

        const float s = 8.0f; // pixels
        glm::vec2 tip    = frontDir * s;
        glm::vec2 backL  = -frontDir * (s * 0.6f) + rightDir * (s * 0.6f);
        glm::vec2 backR  = -frontDir * (s * 0.6f) - rightDir * (s * 0.6f);

        float verts[3 * 4] = {
            toNdcX(centerPx + tip.x),   toNdcY(centerPy + tip.y),   0.0f, 0.0f,
            toNdcX(centerPx + backL.x), toNdcY(centerPy + backL.y), 0.0f, 0.0f,
            toNdcX(centerPx + backR.x), toNdcY(centerPy + backR.y), 0.0f, 0.0f,
        };

        glUniform1i(locMode, 1);
        glUniform3f(locColor, 1.0f, 0.15f, 0.15f);
        glUniform1f(locAlpha, 1.0f);

        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glBindVertexArray(0);
    glUseProgram(0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
