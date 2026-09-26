#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include "SceneGeometry.h" // Vertex, AddCylinder, AddSphere

// The hand torch: a handle with a flame that follows the camera like a shooter viewmodel. Geometry
// is built once in camera space; scene.vert reconstructs world position/normal via the inverse view
// matrix (uIsViewmodelDraw), which is always well defined, unlike a cross()-based basis that
// degenerated at extreme angles. It therefore follows yaw, pitch and bob without extra animation
// code; the VBO/EBO are never re-uploaded.
class PlayerTorchViewmodel {
public:
    void init();
    void destroy();

    void draw() const;

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLsizei m_indexCount = 0;

    // Ash particles: the same 3 slots per torch as wall torches (SceneGeometry.cpp::AddTorchMesh),
    // living in the same VBO right after the handle/flame vertices but drawn separately: not via
    // EBO/indices (glDrawElements) but directly over a vertex range (glDrawArrays(GL_POINTS, ...)),
    // so only a start index and a count are needed.
    GLint m_particleFirstVertex = 0;
    GLsizei m_particleCount = 0;

    glm::vec3 m_localFlamePos{ 0.0f };
};
