#pragma once
// Developer/beta-tester keys: N noclip, M debug map, +/- view distance (noclip), H/J debug
// damage/heal, C cinematic resolution boost, I invisibility to enemies, L/K/U spawn light / dummy
// enemy / undo, P cycle palettes. This file only maps keys to flags and holds tuning constants; the
// logic lives in DungeonScene. The keys work in normal gameplay. They are compiled in unless NDEBUG
// is defined (the Release target defines it); define DEV_TOOLS_ENABLED explicitly to override.
// Deleting this file also removes them (DungeonScene.h includes it via __has_include()).
#ifndef DEV_TOOLS_ENABLED
#ifdef NDEBUG
#define DEV_TOOLS_ENABLED 0
#else
#define DEV_TOOLS_ENABLED 1
#endif
#endif

#if DEV_TOOLS_ENABLED

#include <GLFW/glfw3.h>
#include <algorithm>

namespace DevTools {

// View distance multiplier on top of the normal draw/fog distance. 1.0 = same as a regular player.
// It changes only via +/- and only while noclip is active, so it is always 1.0 outside noclip and
// cannot affect game balance.
inline float s_viewDistanceMul = 1.0f;

constexpr float kViewDistanceMin = 1.0f;
constexpr float kViewDistanceMax = 8.0f;
constexpr float kViewDistanceStep = 2.0f; // per second while held

// Starting multiplier on entering noclip: a decent frame for a trailer right away, fine-tuned
// further with +/-.
constexpr float kNoclipDefaultViewDistanceMul = 2.5f;

inline float GetViewDistanceMultiplier() {
    return s_viewDistanceMul;
}

// Edge-triggered noclip toggle on N. outNoclipEnabled is state owned by DungeonScene (its
// m_noclipEnabled); this function only decides when to flip it and sets the starting view distance
// for cinematic shots.
inline void ToggleNoclip(GLFWwindow* window, bool& outNoclipEnabled, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS;
    if (down && !keyWasDown) {
        outNoclipEnabled = !outNoclipEnabled;
        s_viewDistanceMul = outNoclipEnabled ? kNoclipDefaultViewDistanceMul : 1.0f;
    }
    keyWasDown = down;
}

inline void ToggleDebugMap(GLFWwindow* window, bool& outVisible, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS;
    if (down && !keyWasDown) {
        outVisible = !outVisible;
    }
    keyWasDown = down;
}

// Edge-triggered "invisible to enemy" toggle on I. While on, EnemyAI::update() forces canSee =
// false for line-of-sight detection regardless of actual FOV/LOS/radius, and mutes hearing (the
// enemy does not react to footstep/run noise): full stealth, handy for testing level/enemy behavior
// without AI interference.
inline void ToggleInvisibleToEnemy(GLFWwindow* window, bool& outInvisible, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS;
    if (down && !keyWasDown) {
        outInvisible = !outInvisible;
    }
    keyWasDown = down;
}

// Test-scene composition: light (L) / dummy enemy (K) / undo (U). These functions are only "was it
// just pressed" detectors; the real logic (where to spawn, the undo history) lives in DungeonScene.
inline bool ConsumeSpawnLightKey(GLFWwindow* window, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS;
    const bool fire = down && !keyWasDown;
    keyWasDown = down;
    return fire;
}

inline bool ConsumeSpawnDummyEnemyKey(GLFWwindow* window, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS;
    const bool fire = down && !keyWasDown;
    keyWasDown = down;
    return fire;
}

inline bool ConsumeUndoKey(GLFWwindow* window, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS;
    const bool fire = down && !keyWasDown;
    keyWasDown = down;
    return fire;
}

// Cycle environment color palettes on P. Like the functions above, only a press detector: the
// palette list and the current index live in DungeonScene.
inline bool ConsumeCyclePaletteKey(GLFWwindow* window, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS;
    const bool fire = down && !keyWasDown;
    keyWasDown = down;
    return fire;
}

// Cinematic render boost for trailer shots: FBO resolution and ASCII cell size in noclip. The
// regular game is always 1280x720 / cellSize = 11. kCinematicCellSize must scale proportionally
// with kCinematicSceneW/H, otherwise the on-screen glyph size (~windowWidth * cellSize / sceneW)
// changes along with the mode: 1920/1280 = 1080/720 = 1.5, so 11 * 1.5 = 16.5 -> 17.
constexpr int kCinematicSceneW    = 1920;
constexpr int kCinematicSceneH    = 1080;
constexpr int kCinematicCellSize  = 17; // scales with the scene (11 * 1.5), not shrunk

// A separate toggle, not part of noclip itself: recreating the FBO and font atlases can lag on weak
// hardware, so C toggles the boost independently, at any point, even while already in noclip. On by
// default.
inline bool s_cinematicResolutionEnabled = true;

inline bool IsCinematicResolutionEnabled() {
    return s_cinematicResolutionEnabled;
}

// Edge-triggered resolution-boost toggle on C. The key state (keyWasDown) is a function-local
// static: this toggle does not need DungeonScene to own it, unlike noclip/debug map, which the
// scene's other logic hooks into.
inline void ToggleCinematicResolution(GLFWwindow* window) {
    static bool keyWasDown = false;
    const bool down = glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
    if (down && !keyWasDown) {
        s_cinematicResolutionEnabled = !s_cinematicResolutionEnabled;
    }
    keyWasDown = down;
}

// Smooth view-distance adjustment via +/- while held. '=' is the same physical key as '+' on most
// layouts without Shift, so both are handled, plus numpad +/-.
inline void UpdateViewDistance(GLFWwindow* window, bool noclipEnabled, float deltaTime) {
    if (!noclipEnabled) {
        s_viewDistanceMul = 1.0f;
        return;
    }

    const bool plusDown =
        glfwGetKey(window, GLFW_KEY_EQUAL)  == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS;
    const bool minusDown =
        glfwGetKey(window, GLFW_KEY_MINUS)       == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS;

    if (plusDown)  s_viewDistanceMul += kViewDistanceStep * deltaTime;
    if (minusDown) s_viewDistanceMul -= kViewDistanceStep * deltaTime;

    s_viewDistanceMul = std::clamp(s_viewDistanceMul, kViewDistanceMin, kViewDistanceMax);
}

// Edge-triggered debug damage/heal by 10 HP via H/J: a temporary stand-in for a real combat system,
// see DungeonScene::m_health. Works in regular gameplay, not only in noclip.
inline void ApplyHealthDebugKeys(
    GLFWwindow* window,
    float& health,
    float maxHealth,
    bool& hKeyWasDown,
    bool& jKeyWasDown)
{
    const bool hDown = glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS;
    if (hDown && !hKeyWasDown) {
        health = std::max(health - 10.0f, 0.0f);
    }
    hKeyWasDown = hDown;

    const bool jDown = glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS;
    if (jDown && !jKeyWasDown) {
        health = std::min(health + 10.0f, maxHealth);
    }
    jKeyWasDown = jDown;
}

} // namespace DevTools

#define DEV_TOOLS_ACTIVE 1

#endif // DEV_TOOLS_ENABLED
