#include "Culling.h"

namespace Culling {

void ExtractFrustumPlanes(const glm::mat4& vp, glm::vec4 outPlanes[6])
{
    // Rows of vp (glm is column-major, so we build rows from columns).
    glm::vec4 row0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
    glm::vec4 row1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
    glm::vec4 row2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    glm::vec4 row3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

    outPlanes[0] = row3 + row0; // left
    outPlanes[1] = row3 - row0; // right
    outPlanes[2] = row3 + row1; // bottom
    outPlanes[3] = row3 - row1; // top
    outPlanes[4] = row3 + row2; // near
    outPlanes[5] = row3 - row2; // far

    for (int i = 0; i < 6; ++i)
    {
        float len = glm::length(glm::vec3(outPlanes[i]));
        if (len > 1e-6f)
            outPlanes[i] /= len;
    }
}

bool AabbInFrustum(const glm::vec3& mn, const glm::vec3& mx, const glm::vec4 planes[6])
{
    for (int i = 0; i < 6; ++i)
    {
        const glm::vec4& pl = planes[i];

        glm::vec3 positive(
            pl.x >= 0.0f ? mx.x : mn.x,
            pl.y >= 0.0f ? mx.y : mn.y,
            pl.z >= 0.0f ? mx.z : mn.z
        );

        if (pl.x * positive.x + pl.y * positive.y + pl.z * positive.z + pl.w < 0.0f)
            return false;
    }
    return true;
}

} // namespace Culling
