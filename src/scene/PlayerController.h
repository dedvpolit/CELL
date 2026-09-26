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

// The player: camera, movement/collision, health/stamina, footsteps and the input toggles
// processInput() flips. It owns its state; it does not own the map (DungeonScene), the win button
// position or the render distance.
class PlayerController {
public:
    // Collision radius, public because EnemyAI uses it too. Deliberately small (no visible model);
    // together with EnemyAI::kCollisionRadius it must stay below half the corridor width (0.5) or
    // squeezing past an enemy in a dead end is impossible.
    static constexpr float kCollisionRadius = 0.10f;

    void init() { m_footstepAudio.init(); }
    void shutdown() { m_footstepAudio.shutdown(); }

    // Plays one footstep out of band, not tied to real movement: DungeonScene's glitch effects use
    // it to fake the player's footsteps (see updateGlitchEffects()).
    void playFakeRunFootstep() { m_footstepAudio.playRun(); }
    void playFakeWalkFootstep() { m_footstepAudio.playWalk(); }

    // Minimum diaries that must be read before the win button works: below it, E near the pedestal
    // does nothing except request a "blocked" message.
    static constexpr int kMinDiariesToWin = 4;

    // Per-frame input and movement. getCornerCut/getChamferSize must match the renderer
    // (diagonal-chain cells are chamfered much more). columnCentersXZ, winButtonPos and
    // enemyPositions (by value for this frame, radius EnemyAI::kCollisionRadius) are circular
    // obstacles on top of the grid check. torchTaken entries are ignored when searching for the
    // nearest torch. winReadyToPressE: the spinning donut can be pressed.
    void processInput(GLFWwindow* window, float deltaTime,
                       const std::function<bool(int, int)>& isFloor,
                       const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                       const std::function<float(int, int)>& getChamferSize,
                       const std::vector<glm::vec2>& columnCentersXZ,
                       const glm::vec3& winButtonPos,
                       const std::vector<glm::vec3>& enemyPositions,
                       const std::vector<glm::vec3>& diaryPositions,
                       int diariesReadCount,
                       const std::vector<glm::vec3>& torchPositions,
                       const std::vector<unsigned char>& torchTaken,
                       bool winReadyToPressE);

    // Index of the diary (same order as diaryPositions) whose radius the player stands in, -1 if
    // none; recomputed on every processInput(). DungeonScene uses it for the "[E] READ DIARY" hint.
    int nearbyDiaryIndex() const { return m_nearbyDiaryIndex; }

    // Edge-triggered "want to open a diary": true for one frame after E when nearbyDiaryIndex() !=
    // -1. Consume semantics, like the other requests below: the reader resets the flag, so one
    // press is not handled twice.
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

    // One-shot request for "monument dissolves -> spinning donut": true once, after the first E
    // press at the pedestal with enough diaries. DungeonScene starts the animation; the game is won
    // only later, with a separate E press at the spinning donut.
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

    // Resets only the mouse-look baseline; yaw, pitch and position are unchanged. Needed when
    // re-capturing GLFW_CURSOR_DISABLED after a pause, so the first cursor event is not read as a
    // huge dx/dy.
    void resetMouseLook() { m_firstMouse = true; }

    void tickPauseCameraIdle(float deltaTime);

    glm::vec3 getFront() const;
    glm::vec3 getCameraRenderPosition() const;
    glm::vec3 getCameraRenderUp() const;

    void setSpawnPosition(const glm::vec3& pos) { m_camPos = pos; }

    glm::vec3 camPos() const { return m_camPos; }
    float yaw() const { return m_yaw; }
    float pitch() const { return m_pitch; }

    // One-off pitch set: DungeonScene::generateMenuBackgroundMaze() uses a slight downward tilt so
    // both the floor and the wall torches of the starting safe zone fit in frame. Set once at
    // generation, never per frame.
    void setPitchDegrees(float pitch) { m_pitch = pitch; }
    float poseBlend() const { return m_poseBlend; }

    float torchBlend() const { return m_torchBlend; }

    // Torch fuel and spares: m_torchFuel (0..1) drains only while the torch is raised. At zero the
    // torch lowers itself and is not replaced automatically: the next LMB lights one from the
    // inventory; with none left only consumeTorchEmptyWarningRequest() fires.
    static constexpr float kTorchFuelDrainPerSecond = 1.0f / 75.0f; // ~75s of burn time per torch

    float torchFuel() const { return m_torchFuel; }
    int torchInventoryCount() const { return m_torchInventoryCount; }

    // Light level for EnemyAI (0..1): follows the raise animation (m_torchBlend) and dims with fuel
    // (last 30%), so a dying torch hides the player a bit better. Unlike invisibleToEnemy() (dev
    // tools, forces canSee = false) it only narrows the detection radius.
    float lightLevel() const
    {
        const float kFuelShrinkStartsBelow = 0.3f; // must match scene.frag/scene.vert
        const float kMinVisibleFlameFuel = 0.22f;   // must match scene.frag/scene.vert
        const float fuelBrightness =
            (m_torchFuel >= kFuelShrinkStartsBelow)
                ? 1.0f
                : glm::mix(kMinVisibleFlameFuel, 1.0f,
                           glm::clamp(m_torchFuel / kFuelShrinkStartsBelow, 0.0f, 1.0f));
        return m_torchBlend * fuelBrightness;
    }

    void addTorchToInventory() { ++m_torchInventoryCount; }

    // Throwable stones (a distraction mechanic): the player starts with kInitialStoneCount, topped
    // up by stones picked up in safe zones (1-2 per pocket). Same pickup as torches but with no key
    // press (auto-picked on approach) and deliberately no HUD hint.
    static constexpr int kInitialStoneCount = 2;
    int stoneCount() const { return m_stoneCount; }
    void addStoneToInventory() { ++m_stoneCount; }

    void restoreStoneCount(int count) { m_stoneCount = std::max(0, count); }

    // Edge-triggered "throw a stone" (G key): true for one frame after the press, only if the
    // inventory has a stone (otherwise the press is silently swallowed, no message). The physical
    // throw is DungeonScene's job; PlayerController only tracks the inventory and catches the key.
    bool consumeThrowStoneRequest()
    {
        bool r = m_throwStoneRequested;
        m_throwStoneRequested = false;
        return r;
    }

    // Restore from a save: absolute values, unlike addTorchToInventory(). Called once on slot load,
    // when the fuel and spare count at save time are known. The taken-torch restore path
    // deliberately does not call addTorchToInventory(), or the inventory would be counted twice.
    void restoreTorchState(float fuel, int inventoryCount)
    {
        m_torchFuel = glm::clamp(fuel, 0.0f, 1.0f);
        m_torchInventoryCount = std::max(0, inventoryCount);
    }

    int nearbyTorchIndex() const { return m_nearbyTorchIndex; }

    // Edge-triggered "take the torch off the wall": true for one frame after E when
    // nearbyTorchIndex() != -1 and no higher-priority E action (win button, diary) took the press.
    // Consume semantics.
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

    // Called on the frame an enemy catches the player: running is disabled for
    // kCaughtPhase1Duration (Attack_Lunge's length), then the player moves at kCaughtDebuffSpeed
    // for kCaughtPhase2Duration. m_caughtTimer counts down in processInput().
    void applyCaughtDebuff()
    {
        m_caughtTimer = kCaughtPhase1Duration + kCaughtPhase2Duration;
    }
    bool noclipEnabled() const { return m_noclipEnabled; }
    bool invisibleToEnemy() const { return m_invisibleToEnemy; }
    bool hasWon() const { return m_gameWon; }

    // Called from DungeonScene::render() when an enemy catches the player (together with
    // applyCaughtDebuff()). One Attack_Lunge hit is a fixed amount of damage; zero health sets
    // m_gameOver and starts the death sequence.
    void applyDamage(float amount)
    {
        if (m_gameOver || m_noclipEnabled) // noclip is a debug tool and must not kill the player
            return;

        m_health -= amount;
        if (m_health <= 0.0f)
        {
            m_health = 0.0f;
            m_gameOver = true;
            // Instead of closing the window instantly, a death sequence starts: the camera falls,
            // the screen fades to black, and only then does the transition to the menu happen
            // (through the same fade as a normal menu exit).
            m_deathSequenceActive = true;
            m_deathTime = 0.0f;
            // m_torchRaised = false only expresses intent: the regular m_torchBlend interpolation
            // lives in processInput() after the early return for m_deathSequenceActive, so the
            // actual lowering is done in updateDeathSequence().
            m_torchRaised = false;
            std::fprintf(stderr, "PlayerController: health reached 0 - starting death sequence.\n");
        }
    }

    bool isDying() const { return m_deathSequenceActive; }

    // True exactly once: the frame the death sequence (fall + tilt) finishes and the fade to the
    // menu should start (same RETURN_TO_MENU/FADE_TO_BLACK as a normal menu exit). A single-shot
    // pulse, like EnemyAI's consumeJustCaughtPlayer().
    bool consumeDeathFadeTrigger()
    {
        const bool result = m_deathFadeTriggered;
        m_deathFadeTriggered = false;
        return result;
    }

    // processInput() runs only during real gameplay, but the post-death menu fade interrupts that,
    // so Application calls this ungated tick every frame while isDying(); it keeps counting the
    // same m_deathTime so stamina drains in step with the fade.
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

    // Full reset to a fresh-start state: position is the given spawn (the new map's starting safe
    // zone center), look/health/stamina as at first launch. Leaves the mouse sensitivity (a session
    // setting, not progress) and the dev-mode flags untouched.
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
        // Also reset the torch fuel and inventory: PlayerController is reused across sessions, so
        // old values would otherwise survive New Game and death as a free torch stash.
        m_torchFuel = 1.0f;
        m_torchInventoryCount = 0;
        m_stoneCount = kInitialStoneCount;
        m_caughtTimer = 0.0f;
        // Also reset the death state: if m_deathSequenceActive stayed true the camera would stay
        // fallen and processInput() would re-trigger the death fade, sending the new game straight
        // back to the menu.
        m_deathSequenceActive = false;
        m_deathTime = 0.0f;
        m_deathFadeTriggered = false;
    }

    // "End of game": moves the camera far outside the map (see AppState::CREDITS in Application),
    // where there is nothing to render, so the background is plain black via glClearColor. Purely
    // cosmetic: health, stamina and other state stay untouched.
    void teleportCameraOutOfBounds()
    {
        m_camPos = glm::vec3(-500.0f, 0.0f, -500.0f);
        m_pitch = 0.0f;
    }

    // Restores saved player state: camera position and rotation, health/stamina fractions (0..1;
    // the maxima are constant).
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
                 const glm::vec3& winButtonPos,
                 const std::vector<glm::vec3>& enemyPositions) const;

    // Full XZ movement resolution: try the full step; if blocked by a chamfer diagonal or a circle
    // (no axis-aligned normal), slide along that normal; else fall back to X then Z. Enemies are
    // circular obstacles with kCollisionRadius, independent of their AI state: state-dependent
    // softening froze the player when an enemy switched to Walk/Run while touching.
    void resolveMovement(glm::vec3& pos, glm::vec3 delta,
                         const std::function<bool(int, int)>& isFloor,
                         const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                         const std::function<float(int, int)>& getChamferSize,
                         const std::vector<glm::vec2>& columnCentersXZ,
                         const glm::vec3& winButtonPos,
                         const std::vector<glm::vec3>& enemyPositions) const;

    // True with the surface normal when (x,z) is blocked by a chamfer diagonal or a circle; false
    // for a regular axis-aligned wall, where separate X/Z already slides exactly.
    bool findSlideNormal(float x, float z,
                          const std::function<bool(int, int)>& isFloor,
                          const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                          const std::function<float(int, int)>& getChamferSize,
                          const std::vector<glm::vec2>& columnCentersXZ,
                          const glm::vec3& winButtonPos,
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

    // Smoothed deltaTime for cosmetic camera timers only (idle sway, bob phase); movement uses the
    // raw deltaTime. advanceSmoothedAnimDt() uses a dt-dependent exponential decay, not a fixed
    // blend factor, which would shake differently at 30 and 60 fps.
    float m_animSmoothedDt = 1.0f / 60.0f;
    void advanceSmoothedAnimDt(float rawDeltaTime);

    // Advances m_deathTime and computes the camera Y offset, roll and stamina drain from it. Called
    // from processInput() instead of the regular input handling while m_deathSequenceActive.
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

    // Speed debuff after being caught by an enemy. m_caughtTimer counts down from
    // kCaughtPhase1Duration + kCaughtPhase2Duration to zero; processInput() restricts the speed by
    // phase. Phase 1 matches Attack_Lunge's length (1.25 s).
    static constexpr float kCaughtPhase1Duration = 1.25f;
    static constexpr float kCaughtPhase2Duration = 2.0f;
    // Independent of the enemy's speeds on purpose: it must give the player a real (not guaranteed)
    // chance to escape after a bite, when the enemy switches to Run. Tying it to the enemy's walk
    // speed made it drop whenever that was retuned and left an unwinnable race. Tuned empirically.
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
    // While true, E at the pedestal/donut requests nothing more: the sequence is already running in
    // DungeonScene and waits for the final E press to become valid. Separate from m_gameWon: that
    // means the game is won, this only means the first E already happened.
    bool m_winSequenceStarted = false;
    bool m_gameOver = false; // health hit zero: set by applyDamage(), starts the death sequence (see isDying())

    // Death sequence, all derived from one timer (m_deathTime): kDeathBounceDuration upward jolt,
    // kDeathFallDuration accelerating fall, kDeathRollDuration roll to 90 degrees, then
    // kDeathStaminaDrainDuration drains stamina to 0 and triggers the menu fade
    // (consumeDeathFadeTrigger()).
    static constexpr float kDeathBounceDuration = 0.25f;
    static constexpr float kDeathBounceHeight = 0.10f;
    static constexpr float kDeathFallDuration = 1.6f;
    // must stay below the eye height (0.5), or the camera sinks under the floor
    static constexpr float kDeathFallDistance = 0.35f;
    static constexpr float kDeathRollDuration = 0.85f;
    static constexpr float kDeathRollDegrees = 90.0f;
    static constexpr float kDeathStaminaDrainDuration = 1.2f;

    bool m_deathSequenceActive = false;
    float m_deathTime = 0.0f;
    // set once when the fade to the menu should start (see consumeDeathFadeTrigger())
    bool m_deathFadeTriggered = false;

    const float m_maxStamina = 100.0f;
    const float m_staminaDrainPerSec = 28.0f;
    const float m_staminaRegenPerSec = 16.0f;
    const float m_staminaResumeThreshold = 0.25f;
    float m_stamina = m_maxStamina;
    bool m_staminaExhausted = false;

    const float m_maxHealth = 100.0f;
    float m_health = m_maxHealth;

    bool m_hKeyWasDown = false;
    bool m_jKeyWasDown = false;
};
