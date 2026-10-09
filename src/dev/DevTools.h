#pragma once
// Developer keys:
// N noclip, M debug map, +/- view distance (noclip), H/J damage/heal,
// I invisible to enemies, L/K/U light / dummy enemy / undo, P palettes, T render view
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

// View distance multiplier; changes only in noclip, so it cannot affect gameplay
inline float s_viewDistanceMul = 1.0f;

constexpr float kViewDistanceMin = 1.0f;
constexpr float kViewDistanceMax = 8.0f;
constexpr float kViewDistanceStep = 2.0f; // per second while held

// Starting multiplier on entering noclip.
constexpr float kNoclipDefaultViewDistanceMul = 2.5f;

inline float GetViewDistanceMultiplier() {
    return s_viewDistanceMul;
}

// Edge-triggered N; the state belongs to the caller
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

// Edge-triggered I: enemies neither see nor hear the player
inline void ToggleInvisibleToEnemy(GLFWwindow* window, bool& outInvisible, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS;
    if (down && !keyWasDown) {
        outInvisible = !outInvisible;
    }
    keyWasDown = down;
}

// Press detectors only; DungeonScene owns the logic
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

// Press detector for P
inline bool ConsumeCyclePaletteKey(GLFWwindow* window, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS;
    const bool fire = down && !keyWasDown;
    keyWasDown = down;
    return fire;
}

// +/- while held; '=' shares the '+' key on most layouts, numpad keys too
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

// H/J: -10 / +10 HP
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

// T cycles what the scene area shows:
// 0 = ASCII
// 1 = raw 3D scene
// 2 = edge mask
inline int s_renderView = 0;
constexpr int kRenderViewCount = 3;

inline void CycleRenderView(GLFWwindow* window) {
    static bool keyWasDown = false;
    const bool down = glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS;
    if (down && !keyWasDown) {
        s_renderView = (s_renderView + 1) % kRenderViewCount;
    }
    keyWasDown = down;
}

} // namespace DevTools

#define DEV_TOOLS_ACTIVE 1

#endif // DEV_TOOLS_ENABLED
