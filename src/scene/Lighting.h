#pragma once
#include <glm/glm.hpp>
#include <vector>

// Cache of the active torches (nearest to the camera, within draw distance) uploaded to the scene
// shader's uniform arrays. Recomputed every kRecomputeInterval seconds or after moving
// kRecomputeMoveDist, since the nearest set cannot change faster; lighting looks identical.
class Lighting {
public:
    static constexpr double kRecomputeInterval = 1.0 / 20.0; // seconds
    static constexpr float  kRecomputeMoveDist = 0.75f;      // world units

    // now: usually glfwGetTime(). Recomputes the active set only if the cache has not been built
    // yet, enough time has passed or the camera has moved far enough; otherwise a no-op (the last
    // result is reused).
    void update(const glm::vec3& camPos, float renderDistance, int maxActiveTorches,
                const std::vector<glm::vec3>& torchFlamePos,
                const std::vector<glm::vec3>& torchColor,
                const std::vector<float>& torchIntensity,
                double now);

    const std::vector<glm::vec3>& activePositions() const { return m_cachedActiveTorchPos; }
    const std::vector<glm::vec3>& activeColors() const { return m_cachedActiveTorchColor; }
    const std::vector<float>& activeIntensities() const { return m_cachedActiveTorchIntensity; }
    int activeCount() const { return m_cachedActiveTorchCount; }


private:
    bool m_cacheValid = false;
    double m_lastRecomputeTime = -1.0;
    glm::vec3 m_lastRecomputeCamPos{ 0.0f };

    int m_cachedActiveTorchCount = 0;
    std::vector<glm::vec3> m_cachedActiveTorchPos;
    std::vector<glm::vec3> m_cachedActiveTorchColor;
    std::vector<float> m_cachedActiveTorchIntensity;
};
