#pragma once
#include <vector>
#include <functional>

// Variable corridor width within the grid model: the maze's "partition walls" (walls with floor on
// both opposite sides) are knocked down at random, merging neighboring corridors into wider
// passages and adding loops (a "braided" maze). A separate pass over the finished grid; in
// generateMap() it runs first, before Columns and BuildCornerCuts, which must see the opened faces.
namespace CorridorWidth {

// Fraction of candidates actually knocked down when widenProbabilityAt is not given. Small by
// default: the goal is a noticeable "sometimes wider", not merging the whole maze into one room.
constexpr float kWidenProbability = 0.12f;

// Mutates map, turning some "partition walls" into floor. Returns a bit mask of the cells that
// became floor in this pass specifically (needed only for debug visualization, DebugMapOverlay).
// seed: the same seed as MapGenerator::Generate(), for a deterministic result. widenProbabilityAt:
// optional per-cell probability (see Zoning.h); falls back to the flat kWidenProbability if empty.
std::vector<unsigned char> ApplyWidening(
    int mapW, int mapH,
    std::vector<int>& map,
    unsigned int seed,
    const std::function<float(int, int)>& widenProbabilityAt = {});

} // namespace CorridorWidth
