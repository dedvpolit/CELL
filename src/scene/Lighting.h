#pragma once
#include <glm/glm.hpp>
#include <vector>

// The nearest torches within draw distance, uploaded to the scene shaders' uniform arrays
// The set is recomputed at most every kRecomputeInterval seconds or after the camera moves
// kRecomputeMoveDist, which is far more often than it can visibly change
class Lighting {
public:
    static constexpr double kRecomputeInterval = 1.0 / 20.0; // seconds
    static constexpr float  kRecomputeMoveDist = 0.75f;      // world units

    void update(const glm::vec3& camPos, float renderDistance, int maxActiveTorches,
                const std::vector<glm::vec3>& torchFlamePos,
                const std::vector<glm::vec3>& torchColor,
                const std::vector<float>& torchIntensity,
                double now);

    const std::vector<glm::vec3>& activePositions() const { return m_pos; }
    const std::vector<glm::vec3>& activeColors() const { return m_color; }
    const std::vector<float>& activeIntensities() const { return m_intensity; }
    // Global torch index of each active slot: the row in TorchShadowMap
    const std::vector<int>& activeIndices() const { return m_index; }
    int activeCount() const { return (int)m_pos.size(); }

private:
    struct Candidate { int index; float distanceSquared; };

    bool m_cacheValid = false;
    double m_lastRecomputeTime = -1.0;
    glm::vec3 m_lastRecomputeCamPos{ 0.0f };

    std::vector<Candidate> m_candidates;
    std::vector<glm::vec3> m_pos;
    std::vector<glm::vec3> m_color;
    std::vector<float> m_intensity;
    std::vector<int> m_index;
};
