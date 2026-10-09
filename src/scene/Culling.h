#pragma once
#include <glm/glm.hpp>

// Frustum culling math:
// planes from the view-projection matrix and an AABB test (AI reccomded to use AABB so why not?)
namespace Culling {

// Rows of vp give the planes in left/right/bottom/top/near/far order
// (glm stores matrices column-major, so rows are built from columns)
void ExtractFrustumPlanes(const glm::mat4& vp, glm::vec4 outPlanes[6]);

// Positive-vertex test: conservative, may keep a box that is actually outside
bool AabbInFrustum(const glm::vec3& mn, const glm::vec3& mx, const glm::vec4 planes[6]);

} // namespace Culling
