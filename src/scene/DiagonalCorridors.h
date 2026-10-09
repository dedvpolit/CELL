#pragma once
#include <vector>
#include "WallShapes.h"

// Diagonal staircases:
// the generator only makes axis steps, but a staircase leaves a chain of same-type corner walls
// Found here and chamfered as a whole, they read as a diagonal wall
namespace DiagonalCorridors {

// Shortest chain that counts as a staircase
constexpr int kMinChainLength = 3;

// Mask of cells in chains of at least minChainLength; a pure function of the grid
std::vector<unsigned char> DetectChains(
    int mapW, int mapH,
    const std::vector<int>& map,
    int minChainLength = kMinChainLength);

} // namespace DiagonalCorridors
