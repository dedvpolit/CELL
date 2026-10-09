#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <functional>
#include <vector>
#include <cstdio>
#include <algorithm>
#include "audio/FootstepAudio.h"
#include "WallShapes.h"
#include "Columns.h"

// The player: camera, movement and collision, health, stamina, footsteps and input requests
// The map belongs to DungeonScene
class PlayerController {
public:
    // Shared with EnemyAI
    // Both radii together must stay under half a corridor width, or the player cannot squeeze past an enemy
    static constexpr float kCollisionRadius = 0.10f;

    void init() { m_footstepAudio.init(); }
    void shutdown() { m_footstepAudio.shutdown(); }

    // Footsteps not tied to movement, for the glitch effects
    void playFakeRunFootstep() { m_footstepAudio.playRun(); }
    void playFakeWalkFootstep() { m_footstepAudio.playWalk(); }

    // Set from the difficulty on every new game and load
    void setWinRules(int diariesToWin, bool oneHitKills)
    {
        m_diariesToWin = diariesToWin;
        m_oneHitKills = oneHitKills;
    }
    int diariesToWin() const { return m_diariesToWin; }

    // Per-frame input and movement
    // getCornerCut/getChamferSize must match the geometry
    // Columns the exit door and this frame's enemy positions are obstacles on top of the grid
    void processInput(GLFWwindow* window, float deltaTime,
                       const std::function<bool(int, int)>& isFloor,
                       const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                       const std::function<float(int, int)>& getChamferSize,
                       const std::vector<glm::vec2>& columnCentersXZ,
                       const glm::vec3& exitDoorPos,
                       const std::vector<glm::vec3>& enemyPositions,
                       const std::vector<glm::vec3>& diaryPositions,
                       int diariesReadCount,
                       const std::vector<glm::vec3>& torchPositions,
                       const std::vector<unsigned char>& torchTaken,
                       bool winReadyToPressE);

    // Diary within reach, -1 if none; recomputed every processInput()
    int nearbyDiaryIndex() const { return m_nearbyDiaryIndex; }

    // One frame after E near a diary
    // The consume* requests below reset themselves when read, so one press is handled once
    bool consumeDiaryOpenRequest() {
        bool r = m_diaryOpenRequested;
        m_diaryOpenRequested = false;
        return r;
    }

    bool consumeJournalToggleRequest() {
        bool r = m_journalToggleRequested;
        m_journalToggleRequested = false;
        return r;
    }

    bool consumeWinBlockedRequest() {
        bool r = m_winBlockedRequested;
        m_winBlockedRequested = false;
        return r;
    }

    // First E at the exit door with enough diaries: starts the win sequence
    // Winning takes a second E at the spinning donut
    bool consumeWinSequenceRequest() {
        bool r = m_winSequenceRequested;
        m_winSequenceRequested = false;
        return r;
    }

    bool consumeCreditsRequest() {
        bool r = m_creditsRequested;
        m_creditsRequested = false;
        return r;
    }

    void processMouse(double xpos, double ypos);

    // Resets only the mouse-look baseline, re-capturing the cursor after a pause does not produce a huge jump
    void resetMouseLook() { m_firstMouse = true; }

    void tickPauseCameraIdle(float deltaTime);

    glm::vec3 getFront() const;
    glm::vec3 getCameraRenderPosition() const;
    glm::vec3 getCameraRenderUp() const;

    void setSpawnPosition(const glm::vec3& pos) { m_camPos = pos; }

    glm::vec3 camPos() const { return m_camPos; }
    float yaw() const { return m_yaw; }
    float pitch() const { return m_pitch; }

    // One-off, for the menu background camera
    void setPitchDegrees(float pitch) { m_pitch = pitch; }
    float poseBlend() const { return m_poseBlend; }

    float torchBlend() const { return m_torchBlend; }

    // Fuel drains only while the torch is raised
    // At zero it is lowered; the next LMB lights a spare, or warns if there is none
    static constexpr float kTorchFuelDrainPerSecond = 1.0f / 75.0f; // ~75s of burn time per torch

    float torchFuel() const { return m_torchFuel; }
    int torchInventoryCount() const { return m_torchInventoryCount; }

    // 0..1 for EnemyAI: follows the raise animation and dims over the last 30% of fuel
    // Narrows the detection radius only
    float lightLevel() const
    {
        const float kFuelShrinkStartsBelow = 0.3f;  // must match scene.frag/scene.vert
        const float kMinVisibleFlameFuel = 0.22f;   // must match scene.frag/scene.vert
        const float fuelBrightness =
            (m_torchFuel >= kFuelShrinkStartsBelow)
                ? 1.0f
                : glm::mix(kMinVisibleFlameFuel, 1.0f,
                           glm::clamp(m_torchFuel / kFuelShrinkStartsBelow, 0.0f, 1.0f));
        return m_torchBlend * fuelBrightness;
    }

    void addTorchToInventory() { ++m_torchInventoryCount; }

    // Throwable stones: the starting count plus auto-pickups from pockets (no key, no hint)
    static constexpr int kInitialStoneCount = 2;
    int stoneCount() const { return m_stoneCount; }
    void addStoneToInventory() { ++m_stoneCount; }

    void restoreStoneCount(int count) { m_stoneCount = std::max(0, count); }

    // G with at least one stone; otherwise the press is ignored
    // DungeonScene does the throw
    bool consumeThrowStoneRequest()
    {
        bool r = m_throwStoneRequested;
        m_throwStoneRequested = false;
        return r;
    }

    // Save restore: absolute values
    // The taken-torch restore does not credit the inventory, nothing is counted twice
    void restoreTorchState(float fuel, int inventoryCount)
    {
        m_torchFuel = glm::clamp(fuel, 0.0f, 1.0f);
        m_torchInventoryCount = std::max(0, inventoryCount);
    }

    int nearbyTorchIndex() const { return m_nearbyTorchIndex; }

    // E near an untaken torch when no higher-priority E action (exit door, diary) took the press
    bool consumeTorchPickupRequest()
    {
        bool r = m_torchPickupRequested;
        m_torchPickupRequested = false;
        return r;
    }

    bool consumeTorchEmptyWarningRequest()
    {
        bool r = m_torchEmptyWarningRequested;
        m_torchEmptyWarningRequested = false;
        return r;
    }

    bool isMoving() const { return m_isMoving; }
    bool isRunning() const { return m_isRunning; }
    bool debugMapVisible() const { return m_debugMapVisible; }

    // Caught: no running for kCaughtPhase1Duration (the Attack_Lunge length)
    // kCaughtDebuffSpeed for kCaughtPhase2Duration
    void applyCaughtDebuff()
    {
        m_caughtTimer = kCaughtPhase1Duration + kCaughtPhase2Duration;
    }
    bool noclipEnabled() const { return m_noclipEnabled; }
    bool invisibleToEnemy() const { return m_invisibleToEnemy; }
    bool hasWon() const { return m_gameWon; }

    // Fixed damage per hit; zero health starts the death sequence
    void applyDamage(float amount)
    {
        if (m_gameOver || m_noclipEnabled) // noclip is a debug tool and must not kill the player
            return;

        m_health = m_oneHitKills ? 0.0f : m_health - amount;
        if (m_health <= 0.0f)
        {
            m_health = 0.0f;
            m_gameOver = true;
            // Death: the camera falls, the screen fades, then the game returns to the menu
            m_deathSequenceActive = true;
            m_deathTime = 0.0f;
            // Only the intent:
            // the regular blend is skipped during the death sequence, updateDeathSequence() lowers the torch
            m_torchRaised = false;
            std::fprintf(stderr, "PlayerController: health reached 0 - starting death sequence.\n");
        }
    }

    bool isDying() const { return m_deathSequenceActive; }

    // Pulses once when the fall ends and the fade to the menu should start
    bool consumeDeathFadeTrigger()
    {
        const bool result = m_deathFadeTriggered;
        m_deathFadeTriggered = false;
        return result;
    }

    // processInput() stops during the menu fade;
    // Application ticks this instead so stamina keeps draining in step
    void tickDeathFade(float deltaTime)
    {
        if (m_deathSequenceActive)
            updateDeathSequence(deltaTime);
    }

    float staminaFraction() const { return m_stamina / m_maxStamina; }
    float healthFraction() const { return m_health / m_maxHealth; }

    static constexpr float kMinMouseSensitivity = 0.02f;
    static constexpr float kMaxMouseSensitivity = 0.40f;
    float mouseSensitivity() const { return m_mouseSensitivity; }
    void setMouseSensitivity(float sensitivity) {
        m_mouseSensitivity = glm::clamp(sensitivity, kMinMouseSensitivity, kMaxMouseSensitivity);
    }

    // Fresh-start state at spawnPos
    // Mouse sensitivity and dev flags survive
    void resetForNewGame(const glm::vec3& spawnPos) {
        m_camPos = spawnPos;
        m_yaw = -90.0f;
        m_pitch = 0.0f;
        m_health = m_maxHealth;
        m_stamina = m_maxStamina;
        m_staminaExhausted = false;
        m_gameWon = false;
        m_winSequenceStarted = false;
        m_winSequenceRequested = false;
        m_gameOver = false;
        m_compassVisible = false;
        m_poseBlend = 0.0f;
        m_poseTime = 0.0f;
        m_torchRaised = false;
        m_torchBlend = 0.0f;
        // The controller outlives sessions, so the torch stash must be reset too
        m_torchFuel = 1.0f;
        m_torchInventoryCount = 0;
        m_stoneCount = kInitialStoneCount;
        m_caughtTimer = 0.0f;
        // Otherwise the new game would immediately fade back to the menu
        m_deathSequenceActive = false;
        m_deathTime = 0.0f;
        m_deathFadeTriggered = false;
    }

    // Credits: move the camera outside the map so nothing is rendered. Cosmetic only
    void teleportCameraOutOfBounds()
    {
        m_camPos = glm::vec3(-500.0f, 0.0f, -500.0f);
        m_pitch = 0.0f;
    }

    // Position, rotation and health/stamina fractions from a save
    void restoreState(const glm::vec3& pos, float yaw, float pitch,
                       float healthFraction, float staminaFraction) {
        m_camPos = pos;
        m_yaw = yaw;
        m_pitch = pitch;
        m_health = glm::clamp(healthFraction, 0.0f, 1.0f) * m_maxHealth;
        m_stamina = glm::clamp(staminaFraction, 0.0f, 1.0f) * m_maxStamina;
        m_staminaExhausted = false;
        m_gameWon = false;
        m_winSequenceStarted = false;
        m_winSequenceRequested = false;
        m_gameOver = false;
        m_compassVisible = false;
        m_poseBlend = 0.0f;
        m_poseTime = 0.0f;
        m_torchRaised = false;
        m_torchBlend = 0.0f;
        m_caughtTimer = 0.0f;
        m_deathSequenceActive = false;
        m_deathTime = 0.0f;
        m_deathFadeTriggered = false;
    }

private:
    bool isBlocked(float x, float z, const std::function<bool(int, int)>& isFloor,
                   const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                   const std::function<float(int, int)>& getChamferSize) const;
    bool tryMove(glm::vec3& pos, glm::vec3 delta,
                 const std::function<bool(int, int)>& isFloor,
                 const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                 const std::function<float(int, int)>& getChamferSize,
                 const std::vector<glm::vec2>& columnCentersXZ,
                 const glm::vec3& exitDoorPos,
                 const std::vector<glm::vec3>& enemyPositions) const;

    // XZ movement: try the full step;
    // if a chamfer or circle blocks it slide along its normal;
    // otherwise try X then Z
    // Enemies always collide as circles regardless of AI state
    void resolveMovement(glm::vec3& pos, glm::vec3 delta,
                         const std::function<bool(int, int)>& isFloor,
                         const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                         const std::function<float(int, int)>& getChamferSize,
                         const std::vector<glm::vec2>& columnCentersXZ,
                         const glm::vec3& exitDoorPos,
                         const std::vector<glm::vec3>& enemyPositions) const;

    // True with the normal if a chamfer diagonal or a circle blocks (x, z);
    // false for axis-aligned walls, where the X/Z fallback already slides
    bool findSlideNormal(float x, float z,
                          const std::function<bool(int, int)>& isFloor,
                          const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                          const std::function<float(int, int)>& getChamferSize,
                          const std::vector<glm::vec2>& columnCentersXZ,
                          const glm::vec3& exitDoorPos,
                          const std::vector<glm::vec3>& enemyPositions,
                          glm::vec3& outNormal) const;

private:
    glm::vec3 m_camPos{ 6.5f, 0.5f, 6.5f };
    float m_yaw = -90.0f;
    float m_pitch = 0.0f;
    bool m_firstMouse = true;
    double m_lastX = 0.0, m_lastY = 0.0;
    float m_mouseSensitivity = 0.1f;

    float m_cameraAnimTime = 0.0f;
    float m_bobPhase = 0.0f;
    float m_bobBlend = 0.0f;

    // Smoothed dt for cosmetic camera motion only; frame-rate independent exponential decay
    float m_animSmoothedDt = 1.0f / 60.0f;
    void advanceSmoothedAnimDt(float rawDeltaTime);

    // Replaces regular input while dying
    void updateDeathSequence(float deltaTime);

    float deathCameraYOffset() const;
    float deathCameraRollDegrees() const;

    float m_poseTime = 0.0f;
    float m_poseBlend = 0.0f;
    bool m_compassVisible = false;
    bool m_vKeyWasDown = false;

    float m_torchBlend = 0.0f;
    bool m_torchRaised = false;

    float m_torchFuel = 1.0f;         // starting torch is already full, nothing to pick up manually
    int m_torchInventoryCount = 0;    // no spares yet, only picked-up wall torches
    int m_nearbyTorchIndex = -1;
    bool m_torchPickupRequested = false;
    bool m_torchEmptyWarningRequested = false;

    int m_stoneCount = kInitialStoneCount;
    bool m_throwStoneRequested = false;
    bool m_gKeyWasDown = false;
    bool m_lmbWasDown = false;

    bool m_isMoving = false;
    bool m_isRunning = false;

    // Caught debuff; phase 1 matches Attack_Lunge (1.25 s)
    static constexpr float kCaughtPhase1Duration = 1.25f;
    static constexpr float kCaughtPhase2Duration = 2.0f;
    // Independent of the enemy's speeds on purpose:
    // it must give a real chance to escape a running enemy after a bite
    static constexpr float kCaughtDebuffSpeed = 1.2f;
    float m_caughtTimer = 0.0f;

    FootstepAudio m_footstepAudio;
    float m_footstepDistance = 0.0f;
    bool m_footstepWasMoving = false;
    bool m_footstepWasRunning = false;

    bool m_noclipEnabled = false;

    bool m_debugMapVisible = false;
    bool m_mKeyWasDown = false;
    bool m_nKeyWasDown = false;

    bool m_invisibleToEnemy = false;
    bool m_iKeyWasDown = false;

    bool m_eKeyWasDown = false;
    bool m_gameWon = false;

    bool m_tabKeyWasDown = false;
    int m_nearbyDiaryIndex = -1;
    bool m_diaryOpenRequested = false;
    bool m_journalToggleRequested = false;
    bool m_winBlockedRequested = false;
    bool m_winSequenceRequested = false;
    bool m_creditsRequested = false;
    // The first E at the exit door happened; distinct from m_gameWon
    bool m_winSequenceStarted = false;
    bool m_gameOver = false; // health hit zero: set by applyDamage(), starts the death sequence (see isDying())

    // Death timeline on m_deathTime:
    // bounce, accelerating fall, roll to 90 degrees, then stamina drains and the menu fade starts
    static constexpr float kDeathBounceDuration = 0.25f;
    static constexpr float kDeathBounceHeight = 0.10f;
    static constexpr float kDeathFallDuration = 1.6f;
    // Below the eye height (0.5), or the camera sinks into the floor
    static constexpr float kDeathFallDistance = 0.35f;
    static constexpr float kDeathRollDuration = 0.85f;
    static constexpr float kDeathRollDegrees = 90.0f;
    static constexpr float kDeathStaminaDrainDuration = 1.2f;

    bool m_deathSequenceActive = false;
    float m_deathTime = 0.0f;
    bool m_deathFadeTriggered = false;

    const float m_maxStamina = 100.0f;
    const float m_staminaDrainPerSec = 28.0f;
    const float m_staminaRegenPerSec = 16.0f;
    const float m_staminaResumeThreshold = 0.25f;
    float m_stamina = m_maxStamina;
    bool m_staminaExhausted = false;

    const float m_maxHealth = 100.0f;
    float m_health = m_maxHealth;
    int m_diariesToWin = 4;
    bool m_oneHitKills = false;

    bool m_hKeyWasDown = false;
    bool m_jKeyWasDown = false;
};
