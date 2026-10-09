#pragma once

enum class Difficulty { Easy = 0, Normal = 1, Hard = 2 };

constexpr int kDifficultyCount = 3;

struct DifficultyRules {
    const char* name;
    int diariesToWin;  // read diaries required before the exit door opens
    bool oneHitKills;
};

constexpr DifficultyRules kDifficultyRules[kDifficultyCount] = {
    { "EASY",   0,  false },
    { "NORMAL", 4,  false },
    { "HARD",   12, true  },
};

inline const DifficultyRules& RulesFor(Difficulty d) { return kDifficultyRules[(int)d]; }

// Saves store the difficulty as an int;
// anything unknown falls back to Normal
inline Difficulty DifficultyFromInt(int value)
{
    return (value >= 0 && value < kDifficultyCount) ? (Difficulty)value : Difficulty::Normal;
}
