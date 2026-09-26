#pragma once
#include <glm/glm.hpp>
#include <vector>

// Plain BFS over the map grid: on a uniform-cost grid it finds the same shortest path as A*, with
// no heuristic to get wrong. 128x128 is a fraction of a millisecond, so periodic recomputation
// (EnemyAI.cpp) is plenty.
namespace GridPathfinding {

std::vector<glm::ivec2> FindPath(
    int mapW, int mapH,
    const std::vector<int>& map,
    glm::ivec2 start,
    glm::ivec2 goal);

// True if there is no wall on the straight line between cells a and b (supercover Bresenham over
// cell centers). A diagonal step is blocked when both side neighbors are walls (no squeezing
// through a point-thin gap).
bool HasGridLineOfSight(
    int mapW, int mapH,
    const std::vector<int>& map,
    glm::ivec2 a,
    glm::ivec2 b);

// BFS is 4-directional, so open space yields "staircase" paths. SmoothPath is string pulling: it
// drops intermediate points while the anchor still has a straight line of sight to a farther one,
// so staircases collapse into diagonals and only real turns around walls remain. It tests only the
// grid it is given (EnemyAI passes the path map with column cells marked as walls).
std::vector<glm::ivec2> SmoothPath(
    int mapW, int mapH,
    const std::vector<int>& map,
    const std::vector<glm::ivec2>& cellPath);

} // namespace GridPathfinding
