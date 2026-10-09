#pragma once
#include <vector>
#include <functional>

// Variable corridor width:
// randomly removes walls with floor on both opposite sides, merging corridors and adding loops (a braided maze)
namespace CorridorWidth {

// Default fraction of candidates removed: noticeable but occasional
constexpr float kWidenProbability = 0.12f;

// Mutates map and returns a mask of the cells it opened (debug map only)
std::vector<unsigned char> ApplyWidening(
    int mapW, int mapH,
    std::vector<int>& map,
    unsigned int seed,
    const std::function<float(int, int)>& widenProbabilityAt = {});

} // namespace CorridorWidth
