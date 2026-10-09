#include "TorchShadowMap.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979f; // OH YEEEEEEEAAAAAAAAAAAAAAAAAH

// Distance along the ray to the first solid point, or kMaxRange
// Amanatides-Woo walk over the grid;
// wall cells are entered with a refined point test so chamfers cast the right shadow
float TraceRay(glm::vec2 origin, glm::vec2 dir,
               const std::function<bool(int, int)>& isWallCell,
               const std::function<bool(float, float)>& isSolidPoint)
{
    const float maxT = TorchShadowMap::kMaxRange;
    glm::ivec2 cell((int)std::floor(origin.x), (int)std::floor(origin.y));
    const glm::ivec2 step(dir.x > 0.0f ? 1 : -1, dir.y > 0.0f ? 1 : -1);
    const float big = 1e30f;
    const float tDeltaX = std::abs(dir.x) > 1e-6f ? std::abs(1.0f / dir.x) : big;
    const float tDeltaY = std::abs(dir.y) > 1e-6f ? std::abs(1.0f / dir.y) : big;
    float tMaxX = std::abs(dir.x) > 1e-6f ? ((float)(cell.x + (step.x > 0)) - origin.x) / dir.x : big;
    float tMaxY = std::abs(dir.y) > 1e-6f ? ((float)(cell.y + (step.y > 0)) - origin.y) / dir.y : big;

    auto solidAt = [&](float t) {
        const glm::vec2 p = origin + dir * t;
        return isSolidPoint(p.x, p.y);
    };

    float tEnter = 0.0f;
    while (tEnter < maxT)
    {
        const float tExit = std::min(std::min(tMaxX, tMaxY), maxT);
        if (isWallCell(cell.x, cell.y))
        {
            // Most walls are solid where the ray enters them; only chamfered cells need the search
            const float tEntry = tEnter + 1e-4f;
            if (solidAt(tEntry))
                return tEntry;

            // Sample the crossing, then bisect between the last free and the first solid sample
            constexpr int kSamples = 16;
            float prevFree = tEnter;
            for (int s = 0; s <= kSamples; ++s)
            {
                const float t = tEnter + (tExit - tEnter) * ((float)s / kSamples);
                if (solidAt(t))
                {
                    float lo = prevFree, hi = t;
                    for (int it = 0; it < 10; ++it)
                    {
                        const float mid = 0.5f * (lo + hi);
                        (solidAt(mid) ? hi : lo) = mid;
                    }
                    return hi;
                }
                prevFree = t;
            }
        }
        if (tExit >= maxT)
            break;
        tEnter = tExit;
        if (tMaxX < tMaxY) { cell.x += step.x; tMaxX += tDeltaX; }
        else               { cell.y += step.y; tMaxY += tDeltaY; }
    }
    return maxT;
}

float RayCircle(glm::vec2 origin, glm::vec2 dir, glm::vec2 center, float radius)
{
    const glm::vec2 oc = origin - center;
    const float b = glm::dot(oc, dir);
    const float c = glm::dot(oc, oc) - radius * radius;
    const float disc = b * b - c;
    if (disc < 0.0f)
        return TorchShadowMap::kMaxRange;
    const float t = -b - std::sqrt(disc);
    return t > 0.0f ? t : TorchShadowMap::kMaxRange;
}

} // namespace

void TorchShadowMap::build(const std::function<bool(int, int)>& isWallCell,
                           const std::function<bool(float, float)>& isSolidPoint,
                           const std::vector<glm::vec2>& columns, float columnRadius,
                           const std::vector<glm::vec3>& torchPos)
{
    const int rows = std::max(1, (int)torchPos.size());
    std::vector<float> dist((size_t)kAngles * rows, kMaxRange);
    std::vector<glm::vec2> nearColumns;

    for (size_t ti = 0; ti < torchPos.size(); ++ti)
    {
        const glm::vec2 origin(torchPos[ti].x, torchPos[ti].z);

        nearColumns.clear();
        for (const glm::vec2& c : columns)
            if (glm::distance(c, origin) < kMaxRange + columnRadius)
                nearColumns.push_back(c);

        for (int a = 0; a < kAngles; ++a)
        {
            // Texel a covers the angle at its center;
            // the shader maps atan2 to the same range
            const float angle = ((float)a + 0.5f) / kAngles * 2.0f * kPi - kPi;
            const glm::vec2 dir(std::cos(angle), std::sin(angle));
            float d = TraceRay(origin, dir, isWallCell, isSolidPoint);
            for (const glm::vec2& c : nearColumns)
                d = std::min(d, RayCircle(origin, dir, c, columnRadius));
            dist[ti * kAngles + (size_t)a] = d;
        }
    }

    if (!m_tex)
        glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, kAngles, rows, 0, GL_RED, GL_FLOAT, dist.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TorchShadowMap::destroy()
{
    if (m_tex)
        glDeleteTextures(1, &m_tex);
    m_tex = 0;
}
