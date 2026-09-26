#pragma once
#include <vector>
#include <string>

// Three independent save files (SLOT1..SLOT3). The maze is not serialized: MapGenerator is
// deterministic from a seed, so a file stores the seed plus player state, fog of war and pickups,
// and DungeonScene::loadSlot() regenerates the rest. Plain text (KEY=VALUE per line) in a "saves"
// folder next to the .exe, created on first write.
namespace SaveSystem {

constexpr int kSlotCount = 3;

constexpr int kNameMaxLen = 5;

struct SaveData {
    bool valid = false; // false = slot empty/file corrupted — nothing to load

    unsigned int seed = 0; // see MapGenerator::GenerateResult::seed

    // Name typed when saving (at most kNameMaxLen chars, truncated on read/write). Empty is
    // legitimate: an autosave from a fresh NEW GAME has no name and the picker shows a generic
    // SLOT<n>.
    std::string name;

    float posX = 6.5f, posY = 0.5f, posZ = 6.5f;
    float yaw = -90.0f;
    float pitch = 0.0f;

    float healthFraction = 1.0f;
    float staminaFraction = 1.0f;

    // Fog of war (see MinimapFog::explored()): which map cells the player has already seen. It can
    // be empty (e.g. the very first autosave right after generation, before the first step);
    // DungeonScene::loadSlot() then leaves the fog unrevealed.
    std::vector<unsigned char> explored;

    // Indices of read diaries. The text is rebuilt from the seed (Diaries::SelectForSeed); a plain
    // list because there are only a dozen.
    std::vector<int> diariesReadIndices;

    float torchFuel = 1.0f;
    int torchInventoryCount = 0;

    // Indices of wall torches already picked up. Without them picked-up torches would respawn on
    // load (placement is deterministic from the seed) while the inventory kept the count, allowing
    // infinite pickups.
    std::vector<int> torchTakenIndices;

    // Throwable stones: an absolute inventory count plus the indices already picked up, for the
    // same reason as the torches.
    int stoneCount = 2; // must match PlayerController::kInitialStoneCount
    std::vector<int> stoneTakenIndices;
};

// Cheap "does a file exist in this slot at all" check for drawing the CONTINUE screen: it does not
// parse the fog of war, it only checks that a non-empty file exists. slotIndex: 0..2.
bool SlotExists(int slotIndex);

// Full slot load; SaveData::valid stays false if the slot is empty or has no SEED. Parsing is
// lenient: missing fields default, and non-finite or out-of-range values fall back to their
// defaults (the fog run lengths and list sizes are capped).
SaveData LoadSlot(int slotIndex);

// Fully overwrites the slot (no merging with old contents). It blocks the calling thread while
// writing; see SaveSlotAsync() for the non-blocking version. The data goes to a temporary file that
// replaces the slot only after a successful write, so a failed write keeps the previous save.
bool SaveSlot(int slotIndex, const SaveData& data);

// Same as SaveSlot(), but the write happens on a short-lived thread so the caller does not wait on
// disk. It is the only save path used by the engine (DungeonScene::saveActiveSlot(): autosave and
// manual SAVE).
void SaveSlotAsync(int slotIndex, const SaveData& data);

// Candidate slot for a new game without an explicit slot (the death path): the first empty slot,
// otherwise the least recently written one.
int PickSlotForNewGame();

} // namespace SaveSystem
