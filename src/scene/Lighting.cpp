#include "Lighting.h"
#include <algorithm>

void Lighting::update(const glm::vec3& camPos, float renderDistance, int maxActiveTorches,
                       const std::vector<glm::vec3>& torchFlamePos,
                       const std::vector<glm::vec3>& torchColor,
                       const std::vector<float>& torchIntensity,
                       double now)
{
    glm::vec3 camMoveDelta = camPos - m_lastRecomputeCamPos;
    bool movedEnough = glm::dot(camMoveDelta, camMoveDelta) >
        (kRecomputeMoveDist * kRecomputeMoveDist);

    bool timeElapsed = (now - m_lastRecomputeTime) >= kRecomputeInterval;

    if (!m_cacheValid || timeElapsed || movedEnough)
    {
        struct TorchCandidate
        {
            int index;
            float distanceSquared;
        };

        std::vector<TorchCandidate> candidates;
        candidates.reserve(torchFlamePos.size());

        for (int i = 0; i < (int)torchFlamePos.size(); ++i)
        {
            glm::vec3 delta = torchFlamePos[i] - camPos;
            float distanceSquared = glm::dot(delta, delta);

            // A candidate is dropped if it is beyond the draw distance: it is hidden by fog/discard
            // in the shader anyway, so there is no point spending an active-torch slot on it.
            // renderDistance is usually 16.0 but can be increased (see DevTools.h), so the cutoff
            // radius is taken dynamically instead of being a constant.
            if (distanceSquared > renderDistance * renderDistance)
                continue;

            candidates.push_back({ i, distanceSquared });
        }

        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const TorchCandidate& a, const TorchCandidate& b) {
                return a.distanceSquared < b.distanceSquared;
            }
        );

        int count = std::min((int)candidates.size(), maxActiveTorches);

        m_cachedActiveTorchPos.clear();
        m_cachedActiveTorchColor.clear();
        m_cachedActiveTorchIntensity.clear();

        m_cachedActiveTorchPos.reserve(count);
        m_cachedActiveTorchColor.reserve(count);
        m_cachedActiveTorchIntensity.reserve(count);

        for (int i = 0; i < count; ++i)
        {
            int index = candidates[i].index;
            m_cachedActiveTorchPos.push_back(torchFlamePos[index]);
            m_cachedActiveTorchColor.push_back(torchColor[index]);
            m_cachedActiveTorchIntensity.push_back(torchIntensity[index]);
        }

        m_cachedActiveTorchCount = count;
        m_cacheValid = true;
        m_lastRecomputeTime = now;
        m_lastRecomputeCamPos = camPos;
    }
}
