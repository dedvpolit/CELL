#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <functional>

// Chamfered wall corners. The single definition of a cell's shape for geometry, collision and torch shadows
namespace WallShapes {

enum class CornerCut : uint8_t {
    None = 0,
    SW, // corner at local point (0,0): chamfer faces -X,-Z
    SE, // corner (1,0): chamfer faces +X,-Z
    NE, // corner (1,1): chamfer faces +X,+Z
    NW, // corner (0,1): chamfer faces -X,+Z
};

// Default chamfer length along both corner edges, in world units
constexpr float kChamferSize = 0.35f;

// Range of seeded per-cell chamfer lengths for regular chamfers
constexpr float kChamferSizeVariedMin = 0.20f;
constexpr float kChamferSizeVariedMax = 0.55f;

// Diagonal chains:
// 0.92 cuts ~42% of a cell (vs ~6% at 0.35), so the chain reads as one diagonal wall
constexpr float kChamferSizeChain = 0.92f;

// Fraction of eligible corners that get chamfered, so they read as variation rather than a rule
constexpr float kChamferProbability = 0.45f;

// Seeded size in [kChamferSizeVariedMin, kChamferSizeVariedMax], hashed independently of the chamfer decision
float ComputeVariedChamferSize(unsigned int seed, int x, int z);

// A wall cell is a candidate when exactly one corner has both orthogonal neighbors open
// Returns z*mapW+x
std::vector<CornerCut> BuildCornerCuts(
    int mapW, int mapH,
    const std::vector<int>& map,
    unsigned int seed,
    const std::function<float(int, int)>& chamferProbabilityAt = {});

// Cell-local shape math (0..1 within the cell)

// The two points where the diagonal meets the original edges;
// false for None. From/To follow counter-clockwise order
struct ChamferPoints {
    glm::vec2 onEdgeCcwFrom;
    glm::vec2 onEdgeCcwTo;
};
bool GetChamferPoints(CornerCut cut, float chamferSize, ChamferPoints& out);

// True outside the chamfer wedge; always true for None
bool IsLocalPointSolid(float localX, float localZ, CornerCut cut, float chamferSize);

// Eligible corner by topology alone, before the random decision
CornerCut GetEligibleCornerCut(int mapW, int mapH, const std::vector<int>& map, int x, int z);

} // namespace WallShapes
