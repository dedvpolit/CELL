#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include "Diaries.h"

// Procedural maze generation (DFS over a grid where 2 cells = 1 step) and torch placement on the
// finished map. A one-shot generator: it keeps no state and only fills what it is given or returns;
// DungeonScene owns the data.
namespace MapGenerator {

// Maze generation result. DungeonScene::generateMap() unpacks it into its own fields
// (m_map/m_mapW/m_mapH/m_endSafeX0.../m_winButtonPos/m_smallSafeZoneCenters/
// m_smallSafeZoneRadius).
struct GenerateResult {
    int mapW = 0, mapH = 0;
    std::vector<int> map; // 1 = wall, 2 = floor (see DungeonScene::isWall/isFloor)

    // The seed this map was built from, stored so the caller can write it to a save: the same seed
    // passed to Generate() + PlaceTorches() reproduces the exact maze and torch layout, so CONTINUE
    // stores one number instead of the geometry.
    unsigned int seed = 0;

    int endSafeX0 = 0, endSafeZ0 = 0, endSafeX1 = 0, endSafeZ1 = 0;
    glm::vec3 winButtonPos{0.0f};

    std::vector<glm::ivec2> smallSafeZoneCenters;
    int smallSafeZoneRadius = 0;

    // Diaries placed 1:1 into smallSafeZoneCenters (see Diaries.h::SelectForSeed) from the same
    // seed, so CONTINUE gets the same set of diaries in the same pockets.
    std::vector<Diaries::PlacedDiary> diaries;
};

// Deterministic: the same seed always gives the same maze (same DFS traversal, same pockets);
// CONTINUE relies on it.
GenerateResult Generate(unsigned int seed);

GenerateResult Generate();

// Torch placement: a fixed set in the starting safe zone, several in the finish zone, 3 per pocket
// (smallSafeZoneCenters) and the rest along the main maze walls. The result is parallel arrays
// (wallBase/normal/flamePos/color/intensity) where the same index i is the same torch.
// isWall/isFloor are predicates because the map data stays in DungeonScene.
struct TorchPlacement {
    std::vector<glm::vec3> wallBase;
    std::vector<glm::vec3> normal;
    std::vector<glm::vec3> flamePos;
    std::vector<glm::vec3> color;
    std::vector<float> intensity;

    // See BuildTorchCellLookup() below; computed at the end of PlaceTorches(), when wallBase and
    // normal are final.
    std::vector<unsigned char> torchCellLookup; // size mapW*mapH, 0/1
};

// seed as in Generate(): the same seed gives the same torch layout, so CONTINUE needs no torch
// seed. isChamfered marks cells with a chamfered corner: torches are placed at a face center, which
// can land in the cut wedge and leave the torch floating, so those cells are excluded.
TorchPlacement PlaceTorches(int mapW, int mapH,
                             int endSafeX0, int endSafeZ0, int endSafeX1, int endSafeZ1,
                             const std::vector<glm::ivec2>& smallSafeZoneCenters,
                             int smallSafeZoneRadius,
                             const std::function<bool(int, int)>& isWall,
                             const std::function<bool(int, int)>& isFloor,
                             const std::function<bool(int, int)>& isChamfered,
                             int maxTorches,
                             unsigned int seed);

TorchPlacement PlaceTorches(int mapW, int mapH,
                             int endSafeX0, int endSafeZ0, int endSafeX1, int endSafeZ1,
                             const std::vector<glm::ivec2>& smallSafeZoneCenters,
                             int smallSafeZoneRadius,
                             const std::function<bool(int, int)>& isWall,
                             const std::function<bool(int, int)>& isFloor,
                             const std::function<bool(int, int)>& isChamfered,
                             int maxTorches);

// Used by PlaceTorches() and by DungeonScene::rebuildTorchCellLookupFromTaken(), which recomputes
// the lookup when torches are taken.
std::vector<unsigned char> BuildTorchCellLookup(
    int mapW, int mapH,
    const std::vector<glm::vec3>& torchWallBase,
    const std::vector<glm::vec3>& torchNormal);

} // namespace MapGenerator
