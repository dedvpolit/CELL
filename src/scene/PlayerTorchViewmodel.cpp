#include "PlayerTorchViewmodel.h"
#include <cstddef> // offsetof

void PlayerTorchViewmodel::init()
{
    // Built once in camera space (+X right, +Y up, -Z forward);
    // scene.vert transforms it with inverse(view)

    // Left hand, below eye level, slightly ahead. The handle is lengthened at the grip so the flame stays where it reads well
    const glm::vec3 grip(-0.30f, -0.60f, -0.38f);
    const glm::vec3 tip(-0.30f, -0.10f, -0.46f);

    const glm::vec3 handleColor(0.34f, 0.25f, 0.16f); // same color as wall torches
    const glm::vec3 flameColor(1.0f, 0.60f, 0.15f);

    // Reserved torch index 1536: above MAX_TORCHES (1024) and below 2048, where matId would spill into the next material
    const int kViewmodelFlameIndex = 1536;
    const float HANDLE_ID_SCALE = 4096.0f; // must match HANDLE_ID_SCALE in SceneGeometry.cpp::AddTorchMesh()
    const float handleMatId = 1.0f + (float)kViewmodelFlameIndex / HANDLE_ID_SCALE;

    std::vector<Vertex> verts;
    std::vector<GLuint> indices;

    AddCylinder(
        verts, indices,
        grip, tip,
        0.045f, 0.025f,
        4, // reduced vertex count: a diamond cross-section handle instead of a round one
        handleColor,
        handleMatId
    );

    const float FLAME_ID_SCALE = 4096.0f;
    const float flameMatId = 2.0f + (float)kViewmodelFlameIndex / FLAME_ID_SCALE;

    const glm::vec3 handleDir = glm::normalize(tip - grip);
    // Fuel shrinks the flame around its center, so the center sits at the handle tip
    m_localFlamePos = tip + handleDir * 0.012f;

    AddSphere(verts, indices, m_localFlamePos, 0.070f, 8, 5, flameColor, flameMatId);

    // Three spark slots like wall torches; key 1536 * 10 + slot stays below 16384
    m_particleFirstVertex = (GLint)verts.size();
    {
        const int torchIndex = kViewmodelFlameIndex;
        const float PARTICLE_ID_SCALE = 16384.0f;
        const glm::vec3 particleOrigin = m_localFlamePos;

        auto particleMatId = [&](int slot) {
            return 3.0f + (float)(torchIndex * 10 + slot) / PARTICLE_ID_SCALE;
        };

        verts.push_back({ particleOrigin, glm::vec3(-0.030f, 0.40f, 0.010f), glm::vec3(1.0f, 0.48f, 0.08f), particleMatId(0) });
        verts.push_back({ particleOrigin, glm::vec3(0.020f, 0.48f, -0.025f), glm::vec3(1.0f, 0.58f, 0.10f), particleMatId(1) });
        verts.push_back({ particleOrigin, glm::vec3(-0.045f, 0.43f, -0.020f), glm::vec3(1.0f, 0.52f, 0.07f), particleMatId(2) });
    }
    m_particleCount = (GLsizei)verts.size() - m_particleFirstVertex;

    // matId + 10 marks camera-space geometry for scene.vert
    for (Vertex& v : verts)
        v.matId += 10.0f;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(Vertex)), verts.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, matId));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices.size() * sizeof(GLuint)), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);

    m_indexCount = (GLsizei)indices.size();
}

void PlayerTorchViewmodel::destroy()
{
    if (m_ebo) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    m_indexCount = 0;
    m_particleFirstVertex = 0;
    m_particleCount = 0;
}

void PlayerTorchViewmodel::draw() const
{
    if (m_indexCount <= 0)
        return;

    // "The torch should always draw over walls", like a weapon viewmodel in shooters:
    // depth testing is disabled only for this draw call
    glDisable(GL_DEPTH_TEST);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);

    // Additive sparks, scoped to this draw
    if (m_particleCount > 0)
    {
        glEnable(GL_PROGRAM_POINT_SIZE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);

        glDrawArrays(GL_POINTS, m_particleFirstVertex, m_particleCount);

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_PROGRAM_POINT_SIZE);
    }

    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
}
