#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "WallShapes.h"

// Line of sight between two world points:
// grid walk with the corner-cut half-plane test plus a ray & circle test for columns
// Used by EnemyAI and DungeonScene::isEnemyVisibleToPlayer()
namespace LineOfSight {

// map:
// 1 = wall; the other arrays are indexed like map
// Uncached; EnemyAI calls it a few times per second
bool HasLineOfSight(
    const glm::vec2& fromXZ,
    const glm::vec2& toXZ,
    int mapW, int mapH,
    const std::vector<int>& map,
    const std::vector<WallShapes::CornerCut>& cornerCuts,
    const std::vector<unsigned char>& diagonalChainMask,
    const std::vector<glm::vec2>& columnCentersXZ,
    float columnRadius);

} // namespace LineOfSight
