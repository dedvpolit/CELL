#pragma once
#include <glm/glm.hpp>

// Pure frustum-culling math (no OpenGL, no scene state), pulled out of DungeonScene::render():
// ExtractFrustumPlanes() builds 6 planes from the view-projection matrix, AabbInFrustum() rejects
// invisible geometry chunks against them (see SceneGeometry::chunks()).
namespace Culling {

// Rows of vp give the planes in left/right/bottom/top/near/far order (glm stores matrices
// column-major, so rows are built from columns).
void ExtractFrustumPlanes(const glm::mat4& vp, glm::vec4 outPlanes[6]);

// Conservative test: for each plane take the AABB corner furthest along its normal (the "positive
// vertex"); if that corner is outside, the whole box is outside. An occasional false "visible" at
// frustum edges is fine: the test must never hide something actually on screen.
bool AabbInFrustum(const glm::vec3& mn, const glm::vec3& mx, const glm::vec4 planes[6]);

} // namespace Culling
