#pragma once
#include <glm/glm.hpp>
#include <algorithm>

// The exit: a free-standing door in the middle of the finish zone
// The mesh (DungeonScene.cpp) and the player collider (PlayerController.cpp) share these dimensions
namespace ExitDoor {

constexpr float kOpeningWidth = 0.72f;
constexpr float kOpeningHeight = 1.15f;
constexpr float kFrameWidth = 0.07f;    // jambs and lintel, seen from the front
constexpr float kFrameDepth = 0.14f;
constexpr float kPanelThickness = 0.04f;
constexpr float kOpenAngleDeg = 100.0f;

// Footprint of the closed door, used as the collider
constexpr float kHalfWidth = kOpeningWidth * 0.5f + kFrameWidth;
constexpr float kHalfDepth = kFrameDepth * 0.5f;

// The front faces the finish zone's two entrances, which open toward the map origin
inline glm::vec2 Front() { return glm::normalize(glm::vec2(-1.0f, -1.0f)); }
inline glm::vec2 Right() { const glm::vec2 f = Front(); return glm::vec2(-f.y, f.x); }

// Closest point of the footprint to p, in world XZ
inline glm::vec2 ClosestPoint(glm::vec2 doorXZ, glm::vec2 p)
{
    const glm::vec2 d = p - doorXZ;
    const float x = std::clamp(glm::dot(d, Right()), -kHalfWidth, kHalfWidth);
    const float z = std::clamp(glm::dot(d, Front()), -kHalfDepth, kHalfDepth);
    return doorXZ + Right() * x + Front() * z;
}

} // namespace ExitDoor
