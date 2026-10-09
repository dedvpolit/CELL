#pragma once
#include <array>
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include "Diaries.h"

// Maze generation (DFS, 2 cells per step) and torch placement
// Stateless; DungeonScene owns the data
namespace MapGenerator {

// Unpacked by DungeonScene::generateMap()
struct GenerateResult {
    int mapW = 0, mapH = 0;
    std::vector<int> map; // 1 = wall, 2 = floor (see DungeonScene::isWall/isFloor)

    // The same seed reproduces the maze and torches, so saves store only the seed
    unsigned int seed = 0;

    int endSafeX0 = 0, endSafeZ0 = 0, endSafeX1 = 0, endSafeZ1 = 0;
    glm::vec3 exitDoorPos{0.0f};

    std::vector<glm::ivec2> smallSafeZoneCenters;
    int smallSafeZoneRadius = 0;

    // One diary per pocket; the text comes from the read order and storyVariant
    std::vector<Diaries::PlacedDiary> diaries;
    std::array<int, Diaries::kStepCount> storyVariants{};
};

// Deterministic:
// the same seed always gives the same maze (same DFS traversal, same pockets);
// CONTINUE relies on it
GenerateResult Generate(unsigned int seed);

GenerateResult Generate();

// Torches: fixed sets in both safe zones, 3 per pocket, the rest along maze walls
// Parallel arrays
struct TorchPlacement {
    std::vector<glm::vec3> wallBase;
    std::vector<glm::vec3> normal;
    std::vector<glm::vec3> flamePos;
    std::vector<glm::vec3> color;
    std::vector<float> intensity;

    // See BuildTorchCellLookup() below;
    // computed at the end of PlaceTorches(), when wallBase and normal are final
    std::vector<unsigned char> torchCellLookup; // size mapW*mapH, 0/1
};

// Deterministic from the seed
// Chamfered cells are skipped: a mount point there could fall into the cut
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

// Used by PlaceTorches() and by DungeonScene::rebuildTorchCellLookupFromTaken(), which recomputes the lookup when torches are taken
std::vector<unsigned char> BuildTorchCellLookup(
    int mapW, int mapH,
    const std::vector<glm::vec3>& torchWallBase,
    const std::vector<glm::vec3>& torchNormal);

} // namespace MapGenerator
