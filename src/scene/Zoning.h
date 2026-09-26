#pragma once
#include <vector>
#include <functional>
#include "CorridorWidth.h"

// Divides the finished grid into organic Voronoi-like regions (N random centers) and gives each a
// style profile (chamfer / column / corridor-width probability). A separate pass over the grid like
// WallShapes/Columns: it does not decide which cells are eligible (topology stays with those
// modules), it only sets the probability that an eligible candidate fires.
namespace Zoning {

struct ZoneStyle {
    float chamferProbability = 0.45f;

    float columnProbability = 1.0f;

    float widenProbability = CorridorWidth::kWidenProbability;

    // Wall/floor palette index (kPaletteCount; the colors live in SceneGeometry.cpp::GetZonePalette
    // so Zoning.h stays glm-free). A discrete set of tuned pairs avoids the muddy tints of
    // independent per-channel multipliers.
    int paletteIndex = 0;
};

constexpr int kPaletteCount = 5;

// Ranges a region's profile is picked from (see BuildZoneGrid): deliberately wide, so neighboring
// regions can feel noticeably different: somewhere almost always a right angle (a "classic"
// corridor), somewhere almost always chamfered ("ragged").
constexpr float kChamferProbabilityMin = 0.05f;
constexpr float kChamferProbabilityMax = 0.75f;
constexpr float kColumnProbabilityMin = 0.0f;
constexpr float kColumnProbabilityMax = 1.0f;
// A wider spread than the default CorridorWidth::kWidenProbability in both directions: some regions
// stay almost always narrow "classic" corridors, others are noticeably wider/roomier.
constexpr float kWidenProbabilityMin = 0.0f;
constexpr float kWidenProbabilityMax = 0.35f;

constexpr int kTargetRegionArea = 32 * 32;

// Width in cells of the color blend around region borders (DungeonScene.cpp: BlendedZoneColor()).
// StyleAt() returns only the nearest region, so without blending the color would jump at the
// border.
constexpr float kPaletteBlendWidth = 4.0f;

// A region's center in cell-world coordinates. Not glm::vec2: Zoning deliberately does not depend
// on glm, it needs only x/z and coordinate differences.
struct RegionCenter {
    float x = 0.0f;
    float z = 0.0f;
};

// Zoning result: a list of region centers plus each one's style profile (same index). StyleAt()
// looks up the nearest center (Voronoi). The map itself is not stored: zoning decides a cell's
// region from coordinates, not from cell content.
struct ZoneGrid {
    std::vector<RegionCenter> centers;
    std::vector<ZoneStyle> styles; // same index as centers
};

// Scatters a number of region centers proportional to kTargetRegionArea across the map
// (deterministic from the seed) and assigns each its own style profile. Same seed as
// MapGenerator::Generate()/WallShapes::BuildCornerCuts(), for a consistent CONTINUE.
ZoneGrid BuildZoneGrid(int mapW, int mapH, unsigned int seed);

const ZoneStyle& StyleAt(const ZoneGrid& grid, int cellX, int cellZ);

} // namespace Zoning
