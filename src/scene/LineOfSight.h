#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "WallShapes.h"

// Line of sight between two world points on the map grid: grid DDA + corner-cut half-plane test +
// column ray-vs-circle test, the CPU counterpart of shadowedByWall() in scene.frag. Used by EnemyAI
// and DungeonScene::isEnemyVisibleToPlayer(). Despite the module name nothing is baked: it traces
// rays on demand.
namespace LineOfSight {

// Parameters mirror DungeonScene's arrays (map: 1 = wall; cornerCuts and diagonalChainMask indexed
// like map; columnCentersXZ/columnRadius). Computed on demand without caching: EnemyAI calls it at
// most a couple of times per second.
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
