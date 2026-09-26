#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <functional>

// Chamfered wall corners: a per-cell silhouette variation, a separate pass over MapGenerator's
// finished grid. This header is the single source of truth for the cell shape: render
// (SceneGeometry) and collision (PlayerController) both use IsLocalPointSolid(). At most one
// chamfered corner per cell (other open-side counts belong to CorridorWidth and Columns); the
// choice is deterministic from the seed.
namespace WallShapes {

enum class CornerCut : uint8_t {
    None = 0,
    SW, // corner at local point (0,0) — chamfer faces -X,-Z
    SE, // corner (1,0) — chamfer faces +X,-Z
    NE, // corner (1,1) — chamfer faces +X,+Z
    NW, // corner (0,1) — chamfer faces -X,+Z
};

// Chamfer length along both corner edges in world units. 0.35 is the base value and what the shadow
// shaders assume for regular chamfers; shared by render and collision.
constexpr float kChamferSize = 0.35f;

// Range of the per-cell chamfer length for regular (non-chain) chamfers, deterministic from the
// seed (ComputeVariedChamferSize).
constexpr float kChamferSizeVariedMin = 0.20f;
constexpr float kChamferSizeVariedMax = 0.55f;

// Larger chamfer for diagonal staircases (DiagonalCorridors.h): at 0.35 only ~6% of a cell is cut,
// at 0.92 ~42%, so a chain reads as a connected diagonal wall instead of notches.
constexpr float kChamferSizeChain = 0.92f;

// Not every eligible corner is chamfered: otherwise the result would look designed that way rather
// than like variation on top of normal right angles. This is the fraction [0..1] of eligible
// candidates that fire.
constexpr float kChamferProbability = 0.45f;

// Seed-deterministic chamfer size for this cell, in [kChamferSizeVariedMin..kChamferSizeVariedMax].
// It uses a separate hash salt from the "chamfer or not" decision, so "chamfer?" and "how much?"
// are not tightly correlated for the same cell.
float ComputeVariedChamferSize(unsigned int seed, int x, int z);

// Deterministic pass over the finished grid, returning mapW*mapH (z*mapW+x). A wall cell is a
// candidate when exactly one diagonal case holds (both orthogonal neighbors on that corner are
// floor); cells matching several are skipped. seed matches MapGenerator's, so CONTINUE recreates
// the chamfers. chamferProbabilityAt optionally overrides the per-cell probability (Zoning.h).
std::vector<CornerCut> BuildCornerCuts(
    int mapW, int mapH,
    const std::vector<int>& map,
    unsigned int seed,
    const std::function<float(int, int)>& chamferProbabilityAt = {});

// Shared cell-shape math for render and collision, in cell-local coordinates (localX = worldX -
// cellX, both in [0,1]).

// The two chamfer points where the diagonal meets the corner's original edges; false for
// CornerCut::None. onEdgeCcwFrom/To are the points on the previous/next edge in counter-clockwise
// order.
struct ChamferPoints {
    glm::vec2 onEdgeCcwFrom;
    glm::vec2 onEdgeCcwTo;
};
bool GetChamferPoints(CornerCut cut, float chamferSize, ChamferPoints& out);

// Solid if the point is NOT inside the chamfer wedge; always true for CornerCut::None. Cheaper than
// a full point-in-polygon test.
bool IsLocalPointSolid(float localX, float localZ, CornerCut cut, float chamferSize);

// Eligible corner type for a cell (pure topology, no RNG). Exposed for the diagonal-staircase
// detector, which needs candidates before the random decision.
CornerCut GetEligibleCornerCut(int mapW, int mapH, const std::vector<int>& map, int x, int z);

} // namespace WallShapes
