#pragma once
#include <vector>
#include "WallShapes.h"

// Diagonal "staircase" corridors without teaching the generator diagonal steps: a staircase leaves
// a chain of same-type corner walls at equal diagonal distances, which BuildCornerCuts would
// chamfer only by chance. This module finds such chains (pure topology, before any RNG) and flags
// them so they are chamfered with probability 1.0 (via a wrapped chamferProbabilityAt in
// generateMap()). It does not mutate the map.
namespace DiagonalCorridors {

// Minimum chain length (same-type chamfer cells stepping 2 cells diagonally, matching the
// generator's "1 step = 2 cells") to count as a staircase; single turns stay subject to the normal
// probability.
constexpr int kMinChainLength = 3;

// Finds chains of same-type eligible candidates (WallShapes::GetEligibleCornerCut) connected
// diagonally at (+-2,+-2). Returns a mapW*mapH mask: 1 for cells in a chain of length >=
// minChainLength. A pure function of the finished grid (no seed); call it after all mutating
// passes.
std::vector<unsigned char> DetectChains(
    int mapW, int mapH,
    const std::vector<int>& map,
    int minChainLength = kMinChainLength);

} // namespace DiagonalCorridors
