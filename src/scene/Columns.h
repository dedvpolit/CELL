#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <functional>

// Free-standing columns:
// a candidate wall cell becomes floor with a round column in it
// The collider is the circle; enemy pathfinding blocks the whole cell (see EnemyAI::nearestWalkableCell)
namespace Columns {

// Under half a cell, so there is floor around the column
constexpr float kColumnRadius = 0.28f;

// Low-poly cylinder segment count:
// deliberately few (like the torch handle, see SceneGeometry::AddTorchMesh)
constexpr int kColumnSegments = 8;

// Dampens T-shaped candidates

constexpr float kTShapeAcceptanceMultiplier = 0.08f;

// Turns isolated and T-shaped wall cells into floor (map = 2) and returns column centers
// Call before BuildCornerCuts()
std::vector<glm::vec2> BuildColumns(
    int mapW, int mapH,
    std::vector<int>& map,
    unsigned int seed,
    const std::function<float(int, int)>& columnProbabilityAt = {});

// Test shared by collision and debug drawing;
// margin is added to kColumnRadius
// (mesh radius plus a little headroom)
bool IsPointInsideColumn(float x, float z, const glm::vec2& columnCenterXZ,
                          float radius);

} // namespace Columns
