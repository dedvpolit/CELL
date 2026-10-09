#pragma once
#include <glm/glm.hpp>
#include <vector>

// BFS: on a uniform grid it finds shortest paths like A* without a heuristic
// 128x128 takes a fraction of a millisecond
namespace GridPathfinding {

std::vector<glm::ivec2> FindPath(
    int mapW, int mapH,
    const std::vector<int>& map,
    glm::ivec2 start,
    glm::ivec2 goal);

// No wall on the line between cell centers (supercover Bresenham);
// diagonal steps between two walls are blocked
bool HasGridLineOfSight(
    int mapW, int mapH,
    const std::vector<int>& map,
    glm::ivec2 a,
    glm::ivec2 b);

// String pulling: drops points while the anchor sees a farther one
// turning BFS staircases into diagonals
std::vector<glm::ivec2> SmoothPath(
    int mapW, int mapH,
    const std::vector<int>& map,
    const std::vector<glm::ivec2>& cellPath);

} // namespace GridPathfinding
