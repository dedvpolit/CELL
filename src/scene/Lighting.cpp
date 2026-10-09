#include "Lighting.h"
#include <algorithm>

void Lighting::update(const glm::vec3& camPos, float renderDistance, int maxActiveTorches,
                      const std::vector<glm::vec3>& torchFlamePos,
                      const std::vector<glm::vec3>& torchColor,
                      const std::vector<float>& torchIntensity,
                      double now)
{
    const glm::vec3 moved = camPos - m_lastRecomputeCamPos;
    const bool movedEnough = glm::dot(moved, moved) > kRecomputeMoveDist * kRecomputeMoveDist;
    const bool timeElapsed = (now - m_lastRecomputeTime) >= kRecomputeInterval;
    if (m_cacheValid && !timeElapsed && !movedEnough)
        return;

    // Torches beyond the draw distance are fogged out anyway
    const float maxDistSq = renderDistance * renderDistance;
    m_candidates.clear();
    for (int i = 0; i < (int)torchFlamePos.size(); ++i)
    {
        // Extinguished torches contribute nothing; do not spend a slot on them
        if (torchIntensity[i] <= 0.0f)
            continue;
        const glm::vec3 d = torchFlamePos[i] - camPos;
        const float distSq = glm::dot(d, d);
        if (distSq <= maxDistSq)
            m_candidates.push_back({ i, distSq });
    }

    const int count = std::min((int)m_candidates.size(), maxActiveTorches);
    std::partial_sort(m_candidates.begin(), m_candidates.begin() + count, m_candidates.end(),
                      [](const Candidate& a, const Candidate& b) {
                          return a.distanceSquared < b.distanceSquared;
                      });

    m_pos.clear();
    m_color.clear();
    m_intensity.clear();
    m_index.clear();
    for (int i = 0; i < count; ++i)
    {
        const int idx = m_candidates[i].index;
        m_pos.push_back(torchFlamePos[idx]);
        m_color.push_back(torchColor[idx]);
        m_intensity.push_back(torchIntensity[idx]);
        m_index.push_back(idx);
    }

    m_cacheValid = true;
    m_lastRecomputeTime = now;
    m_lastRecomputeCamPos = camPos;
}
