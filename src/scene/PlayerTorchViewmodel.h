#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include "SceneGeometry.h" // Vertex, AddCylinder, AddSphere

// The hand torch: a handle with a flame that follows the camera
// Camera-space geometry, placed in the world by scene.vert (uIsViewmodelDraw)
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

    // Spark points follow the handle/flame vertices in the same VBO and are drawn with glDrawArrays
    GLint m_particleFirstVertex = 0;
    GLsizei m_particleCount = 0;

    glm::vec3 m_localFlamePos{ 0.0f };
};
