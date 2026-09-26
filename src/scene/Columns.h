#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <functional>

// Free-standing low-poly columns: a separate pass over the finished grid, currently disabled
// (generateMap() does not call BuildColumns(): a column's collision left the enemy AI stuck). A
// candidate wall cell becomes floor with a round column that collides as a circle-vs-point test
// (like the win pedestal). Candidates: isolated walls (4 open sides, rare) and T-shaped dead-end
// stubs (3 open sides, common, so with a lower probability).
namespace Columns {

// Column mesh radius: under 0.5 (half a cell) so walkable floor remains around the column within
// the cell; otherwise the column would fill the whole cell and be indistinguishable from a regular
// wall.
constexpr float kColumnRadius = 0.28f;

// Low-poly cylinder segment count: deliberately few (like the torch handle, see
// SceneGeometry::AddTorchMesh), not a smooth circle.
constexpr int kColumnSegments = 8;

// T-shaped candidates are far more common than isolated ones; the multiplier dampens their
// acceptance so the map does not get denser in columns than Zoning's range intends.

constexpr float kTShapeAcceptanceMultiplier = 0.08f;

// Finds isolated or T-shaped wall cells and turns them into floor (map[] = 2), so everything built
// on isWall/isFloor sees plain floor. Call it before BuildCornerCuts() so neighbors see the opened
// faces. seed matches MapGenerator's; columnProbabilityAt optionally sets the per-cell probability
// (Zoning.h). Returns column centers in world XZ.
std::vector<glm::vec2> BuildColumns(
    int mapW, int mapH,
    std::vector<int>& map,
    unsigned int seed,
    const std::function<float(int, int)>& columnProbabilityAt = {});

// Circle-vs-point test shared by collision and debug drawing; margin is added to kColumnRadius
// (mesh radius plus a little headroom).
bool IsPointInsideColumn(float x, float z, const glm::vec2& columnCenterXZ,
                          float radius);

} // namespace Columns
