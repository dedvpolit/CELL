#pragma once
#include <vector>
#include <string>

// Three save slots
// The maze is regenerated from the seed
// file stores the seed plus player state, fog of war and pickups
// MEOW MEOW
namespace SaveSystem {

constexpr int kSlotCount = 3;

constexpr int kNameMaxLen = 5;

struct SaveData {
    bool valid = false; // false = slot empty/file corrupted: nothing to load

    unsigned int seed = 0; // see MapGenerator::GenerateResult::seed

    // At most kNameMaxLen chars; empty shows as SLOT<n>
    std::string name;

    int difficulty = 1; // Difficulty enum; saves without it are Normal

    float posX = 6.5f, posY = 0.5f, posZ = 6.5f;
    float yaw = -90.0f;
    float pitch = 0.0f;

    float healthFraction = 1.0f;
    float staminaFraction = 1.0f;

    // Seen cells; may be empty, then nothing is revealed
    std::vector<unsigned char> explored;

    // Read diaries in read order;
    // the order decides which story step each one shows
    std::vector<int> diariesReadIndices;

    float torchFuel = 1.0f;
    int torchInventoryCount = 0;

    // Taken wall torches, or they would respawn on load
    std::vector<int> torchTakenIndices;

    // Stones: absolute count plus taken indices, like torches
    int stoneCount = 2; // must match PlayerController::kInitialStoneCount
    std::vector<int> stoneTakenIndices;
};

// Existence check only, for the CONTINUE screen
// slotIndex 0..2
bool SlotExists(int slotIndex);

// valid stays false for an empty slot or a missing SEED
// Lenient: bad or missing fields fall back to defaults
SaveData LoadSlot(int slotIndex);

// Blocking full overwrite through a temporary file that replaces the slot only when complete
bool SaveSlot(int slotIndex, const SaveData& data);

// Non-blocking SaveSlot(); the only path the game uses
void SaveSlotAsync(int slotIndex, const SaveData& data);

// First empty slot, otherwise the least recently written one
int PickSlotForNewGame();

} // namespace SaveSystem
