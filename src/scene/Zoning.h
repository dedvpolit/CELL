#pragma once
#include <vector>
#include <functional>
#include "CorridorWidth.h"

// Voronoi regions over the grid, each with a style profile (chamfer, column and widening probability)
// Other passes read it; zoning itself changes nothing
namespace Zoning {

struct ZoneStyle {
    float chamferProbability = 0.45f;

    float columnProbability = 1.0f;

    float widenProbability = CorridorWidth::kWidenProbability;

    // Palette index; colors live in SceneGeometry::GetZonePalette
    int paletteIndex = 0;
};

constexpr int kPaletteCount = 5;

// Wide ranges so neighboring regions feel different
constexpr float kChamferProbabilityMin = 0.05f;
constexpr float kChamferProbabilityMax = 0.75f;
constexpr float kColumnProbabilityMin = 0.0f;
constexpr float kColumnProbabilityMax = 1.0f;
// Wider spread than CorridorWidth's default in both directions
constexpr float kWidenProbabilityMin = 0.0f;
constexpr float kWidenProbabilityMax = 0.35f;

constexpr int kTargetRegionArea = 32 * 32;

// Blend width in cells around region borders
constexpr float kPaletteBlendWidth = 4.0f;

// A region's center in cell-world coordinates.
// Not glm::vec2: Zoning deliberately does not depend on glm, it needs only x/z and coordinate differences
struct RegionCenter {
    float x = 0.0f;
    float z = 0.0f;
};

// Region centers and their profiles; StyleAt() returns the nearest one
struct ZoneGrid {
    std::vector<RegionCenter> centers;
    std::vector<ZoneStyle> styles; // same index as centers
};

// Region count follows kTargetRegionArea; deterministic from the map seed
ZoneGrid BuildZoneGrid(int mapW, int mapH, unsigned int seed);

const ZoneStyle& StyleAt(const ZoneGrid& grid, int cellX, int cellZ);

} // namespace Zoning
