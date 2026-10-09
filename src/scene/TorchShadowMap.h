#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <functional>
#include <vector>

// Baked 2D shadows for wall torches
// Walls, columns and wall torches never move, so for every torch the distance from the flame
// to the first occluder is precomputed in kAngles directions (a polar shadow map)
// Row i of the texture belongs to torch i. The shaders replace a per-pixel grid
// raymarch with two texel fetches (see torchShadowed() in scene.frag)
class TorchShadowMap {
public:
    static constexpr int kAngles = 1024;    // must match kShadowAngles in scene.frag/enemy.frag
    static constexpr float kMaxRange = 8.5f; // light cutoff in the shaders is 8.0

    // isWallCell must return true outside the map
    // isSolidPoint is queried only inside wall cells and must account for chamfered corners
    void build(const std::function<bool(int, int)>& isWallCell,
               const std::function<bool(float, float)>& isSolidPoint,
               const std::vector<glm::vec2>& columns, float columnRadius,
               const std::vector<glm::vec3>& torchPos);
    void destroy();

    GLuint texture() const { return m_tex; }

private:
    GLuint m_tex = 0;
};
