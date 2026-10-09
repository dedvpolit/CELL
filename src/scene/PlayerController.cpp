#include "PlayerController.h"
#include "ExitDoor.h"
#include "EnemyAI.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <cstdio>
#include <algorithm>

#if __has_include("dev/DevTools.h")
#include "dev/DevTools.h"
#endif
#ifdef DEV_TOOLS_ACTIVE
#define HAS_DEV_TOOLS 1
#endif

// Exact circle vs axis-aligned square:
// clamp the center into the square and compare with the radius
static bool SquareOverlapsCircle(float squareCenterX, float squareCenterZ, float halfExtent,
                                  const glm::vec2& circleCenter, float radius) {
    const float closestX = std::clamp(circleCenter.x, squareCenterX - halfExtent, squareCenterX + halfExtent);
    const float closestZ = std::clamp(circleCenter.y, squareCenterZ - halfExtent, squareCenterZ + halfExtent);
    const float dx = circleCenter.x - closestX;
    const float dz = circleCenter.y - closestZ;
    return dx * dx + dz * dz < radius * radius;
}

static bool SquareOverlapsExitDoor(float cx, float cz, float halfSize, const glm::vec3& doorPos)
{
    const glm::vec2 p(cx, cz);
    const glm::vec2 closest = ExitDoor::ClosestPoint(glm::vec2(doorPos.x, doorPos.z), p);
    const glm::vec2 d = p - closest;
    return glm::dot(d, d) < halfSize * halfSize;
}

// Must match SceneGeometry::AddChamferedWallCell
static glm::vec3 ChamferDiagonalNormal(WallShapes::CornerCut cut) {
    switch (cut) {
        case WallShapes::CornerCut::SW: return glm::normalize(glm::vec3(-1, 0, -1));
        case WallShapes::CornerCut::SE: return glm::normalize(glm::vec3(1, 0, -1));
        case WallShapes::CornerCut::NE: return glm::normalize(glm::vec3(1, 0, 1));
        case WallShapes::CornerCut::NW: return glm::normalize(glm::vec3(-1, 0, 1));
        default: return glm::vec3(0.0f);
    }
}

bool PlayerController::isBlocked(float x, float z, const std::function<bool(int, int)>& isFloor,
                                 const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                                 const std::function<float(int, int)>& getChamferSize) const {
    int cx = (int)std::floor(x);
    int cz = (int)std::floor(z);

    if (!isFloor(cx, cz)) {
        // A chamfered cell is partly open;
        // the same math and size as the renderer keep collision on the silhouette
        const WallShapes::CornerCut cut = getCornerCut(cx, cz);
        if (cut == WallShapes::CornerCut::None)
            return true;

        const float chamferSize = getChamferSize ? getChamferSize(cx, cz) : WallShapes::kChamferSize;
        const float localX = x - (float)cx;
        const float localZ = z - (float)cz;
        if (WallShapes::IsLocalPointSolid(localX, localZ, cut, chamferSize))
            return true;
        // In the cut wedge: open
    }

    return false;
}

bool PlayerController::tryMove(glm::vec3& pos, glm::vec3 delta,
                               const std::function<bool(int, int)>& isFloor,
                               const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                               const std::function<float(int, int)>& getChamferSize,
                               const std::vector<glm::vec2>& columnCentersXZ,
                               const glm::vec3& exitDoorPos,
                               const std::vector<glm::vec3>& enemyPositions) const {
    glm::vec3 newPos = pos + delta;
    const float r = kCollisionRadius;

    // The four corners of the player square are enough:
    // a wall is thicker than the player's diameter
    if (isBlocked(newPos.x - r, newPos.z - r, isFloor, getCornerCut, getChamferSize)) return false;
    if (isBlocked(newPos.x + r, newPos.z - r, isFloor, getCornerCut, getChamferSize)) return false;
    if (isBlocked(newPos.x - r, newPos.z + r, isFloor, getCornerCut, getChamferSize)) return false;
    if (isBlocked(newPos.x + r, newPos.z + r, isFloor, getCornerCut, getChamferSize)) return false;

    // Round and box obstacles: columns, the exit door, enemies
    for (const glm::vec2& col : columnCentersXZ) {
        if (SquareOverlapsCircle(newPos.x, newPos.z, r, col, Columns::kColumnRadius))
            return false;
    }
    if (SquareOverlapsExitDoor(newPos.x, newPos.z, r, exitDoorPos))
        return false;
    // Moving toward an enemy is blocked, moving away is always allowed, even from inside its circle;
    // otherwise an enemy walking into a pinned player softlocks them
    for (const glm::vec3& enemyPos : enemyPositions) {
        const glm::vec2 enemyXZ(enemyPos.x, enemyPos.z);
        const glm::vec2 curXZ(pos.x, pos.z);
        const glm::vec2 newXZ(newPos.x, newPos.z);
        const float curDistSq = glm::dot(curXZ - enemyXZ, curXZ - enemyXZ);
        const float newDistSq = glm::dot(newXZ - enemyXZ, newXZ - enemyXZ);
        // Tolerance so tangential moves are not blocked by rounding
        const bool approaching = newDistSq < curDistSq - 1e-6f;
        if (approaching &&
            SquareOverlapsCircle(newPos.x, newPos.z, r, enemyXZ, EnemyAI::kCollisionRadius))
            return false;
    }

    pos = newPos;
    return true;
}

bool PlayerController::findSlideNormal(float x, float z,
                                        const std::function<bool(int, int)>& isFloor,
                                        const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                                        const std::function<float(int, int)>& getChamferSize,
                                        const std::vector<glm::vec2>& columnCentersXZ,
                                        const glm::vec3& exitDoorPos,
                                        const std::vector<glm::vec3>& enemyPositions,
                                        glm::vec3& outNormal) const {
    const float r = kCollisionRadius;

    // Circles first: usually none are nearby, so this is cheap to rule out
    for (const glm::vec2& col : columnCentersXZ) {
        if (SquareOverlapsCircle(x, z, r, col, Columns::kColumnRadius)) {
            const glm::vec2 d(x - col.x, z - col.y);
            if (glm::dot(d, d) > 1e-8f) {
                const glm::vec2 n = glm::normalize(d);
                outNormal = glm::vec3(n.x, 0.0f, n.y);
                return true;
            }
        }
    }
    if (SquareOverlapsExitDoor(x, z, r, exitDoorPos)) {
        const glm::vec2 p(x, z);
        const glm::vec2 d = p - ExitDoor::ClosestPoint(glm::vec2(exitDoorPos.x, exitDoorPos.z), p);
        // Inside the footprint the closest point is p itself:
        // push out along the front axis, on the side p is on
        const glm::vec2 n = glm::dot(d, d) > 1e-8f
            ? glm::normalize(d)
            : (glm::dot(p - glm::vec2(exitDoorPos.x, exitDoorPos.z), ExitDoor::Front()) >= 0.0f
                   ? ExitDoor::Front() : -ExitDoor::Front());
        outNormal = glm::vec3(n.x, 0.0f, n.y);
        return true;
    }
    {
        // Without this the player only slides around an enemy through the X/Z fallback
        for (const glm::vec3& enemyPos : enemyPositions) {
            const glm::vec2 enemyXZ(enemyPos.x, enemyPos.z);
            if (SquareOverlapsCircle(x, z, r, enemyXZ, EnemyAI::kCollisionRadius)) {
                const glm::vec2 d(x - enemyXZ.x, z - enemyXZ.y);
                if (glm::dot(d, d) > 1e-8f) {
                    const glm::vec2 n = glm::normalize(d);
                    outNormal = glm::vec3(n.x, 0.0f, n.y);
                    return true;
                }
            }
        }
    }

    // Same corner points as tryMove();
    // here the blocking cell's diagonal is needed
    const float px[4] = { x - r, x + r, x - r, x + r };
    const float pz[4] = { z - r, z - r, z + r, z + r };
    for (int i = 0; i < 4; ++i) {
        const int cx = (int)std::floor(px[i]);
        const int cz = (int)std::floor(pz[i]);
        if (isFloor(cx, cz)) continue;

        const WallShapes::CornerCut cut = getCornerCut(cx, cz);
        if (cut == WallShapes::CornerCut::None) continue;

        const float localX = px[i] - (float)cx;
        const float localZ = pz[i] - (float)cz;
        const float chamferSize = getChamferSize ? getChamferSize(cx, cz) : WallShapes::kChamferSize;
        if (!WallShapes::IsLocalPointSolid(localX, localZ, cut, chamferSize))
            continue;

        outNormal = ChamferDiagonalNormal(cut);
        return true;
    }

    return false;
}

void PlayerController::advanceSmoothedAnimDt(float rawDeltaTime)
{
    // Exponential smoothing with a dt-derived alpha: the same ~0.15 s window at any frame rate
    const float tau = 0.15f;
    const float alpha = 1.0f - std::exp(-rawDeltaTime / tau);
    m_animSmoothedDt = m_animSmoothedDt + (rawDeltaTime - m_animSmoothedDt) * alpha;
}

void PlayerController::updateDeathSequence(float deltaTime)
{
    m_deathTime += deltaTime;

    // The regular blend does not run while dying; drop faster than a normal lower
    const float kDeathTorchDropResponse = 8.0f;
    const float torchAlpha = 1.0f - std::exp(-kDeathTorchDropResponse * deltaTime);
    m_torchBlend += (0.0f - m_torchBlend) * torchAlpha;
    if (m_torchBlend < 0.0005f)
        m_torchBlend = 0.0f;

    const float staminaStart = kDeathBounceDuration + kDeathFallDuration + kDeathRollDuration;

    if (m_deathTime >= staminaStart)
    {
        const float drainT = glm::clamp((m_deathTime - staminaStart) / kDeathStaminaDrainDuration, 0.0f, 1.0f);
        m_stamina = m_maxStamina * (1.0f - drainT);
    }

    if (m_deathTime >= staminaStart && !m_deathFadeTriggered)
    {
        // The fade and the stamina drain run together
        m_deathFadeTriggered = true;
    }
}

float PlayerController::deathCameraYOffset() const
{
    if (!m_deathSequenceActive)
        return 0.0f;

    const float t = m_deathTime;

    if (t < kDeathBounceDuration)
    {
        const float bt = t / kDeathBounceDuration;
        return std::sin(bt * 1.5707963f) * kDeathBounceHeight; // 1.5707963 = pi/2
    }

    const float fallStart = kDeathBounceDuration;
    if (t < fallStart + kDeathFallDuration)
    {
        const float ft = (t - fallStart) / kDeathFallDuration;
        const float eased = ft * ft * ft;
        return glm::mix(kDeathBounceHeight, -kDeathFallDistance, eased);
    }

    return -kDeathFallDistance;
}

float PlayerController::deathCameraRollDegrees() const
{
    if (!m_deathSequenceActive)
        return 0.0f;

    const float rollStart = kDeathBounceDuration + kDeathFallDuration;
    if (m_deathTime < rollStart)
        return 0.0f;

    const float rt = glm::clamp((m_deathTime - rollStart) / kDeathRollDuration, 0.0f, 1.0f);
    const float eased = 1.0f - (1.0f - rt) * (1.0f - rt);
    return eased * kDeathRollDegrees;
}

void PlayerController::resolveMovement(glm::vec3& pos, glm::vec3 delta,
                                       const std::function<bool(int, int)>& isFloor,
                                       const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                                       const std::function<float(int, int)>& getChamferSize,
                                       const std::vector<glm::vec2>& columnCentersXZ,
                                       const glm::vec3& exitDoorPos,
                                       const std::vector<glm::vec3>& enemyPositions) const {
    if (glm::dot(glm::vec2(delta.x, delta.z), glm::vec2(delta.x, delta.z)) < 1e-12f)
        return;

    if (tryMove(pos, delta, isFloor, getCornerCut, getChamferSize, columnCentersXZ, exitDoorPos, enemyPositions))
        return;

    // Chamfer diagonal or circle: slide along the surface normal
    glm::vec3 slideNormal;
    if (findSlideNormal(pos.x + delta.x, pos.z + delta.z, isFloor, getCornerCut, getChamferSize,
                         columnCentersXZ, exitDoorPos, enemyPositions, slideNormal)) {
        const glm::vec3 slideDelta = delta - slideNormal * glm::dot(delta, slideNormal);
        if (tryMove(pos, slideDelta, isFloor, getCornerCut, getChamferSize, columnCentersXZ, exitDoorPos, enemyPositions))
            return;
    }

    // Fallback:
    // X then Z. Exact for axis-aligned walls, and a safety net for concave corners between two obstacles
    tryMove(pos, glm::vec3(delta.x, 0.0f, 0.0f), isFloor, getCornerCut, getChamferSize, columnCentersXZ, exitDoorPos, enemyPositions);
    tryMove(pos, glm::vec3(0.0f, 0.0f, delta.z), isFloor, getCornerCut, getChamferSize, columnCentersXZ, exitDoorPos, enemyPositions);
}

glm::vec3 PlayerController::getFront() const {
    const float headDropDegrees = -58.0f * m_poseBlend;
    const float effectivePitch = m_pitch + headDropDegrees;

    glm::vec3 f;
    f.x = std::cos(glm::radians(m_yaw)) * std::cos(glm::radians(effectivePitch));
    f.y = std::sin(glm::radians(effectivePitch));
    f.z = std::sin(glm::radians(m_yaw)) * std::cos(glm::radians(effectivePitch));
    return glm::normalize(f);
}

void PlayerController::processInput(GLFWwindow* window, float deltaTime,
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
                                     bool winReadyToPressE)
{
    deltaTime = glm::clamp(deltaTime, 0.0f, 0.05f);

    advanceSmoothedAnimDt(deltaTime);
    m_cameraAnimTime += m_animSmoothedDt;

    const bool vKeyDown = glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS;
    if (vKeyDown && !m_vKeyWasDown)
    {
        m_compassVisible = !m_compassVisible;
        m_poseTime = 0.0f;
    }
    m_vKeyWasDown = vKeyDown;

    const bool lmbDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (lmbDown && !m_lmbWasDown)
    {
        if (m_torchRaised)
        {
            m_torchRaised = false;
        }
        else if (m_torchFuel > 0.0f)
        {
            m_torchRaised = true;
        }
        else if (m_torchInventoryCount > 0)
        {
            // The raised torch burned out: light a spare
            --m_torchInventoryCount;
            m_torchFuel = 1.0f;
            m_torchRaised = true;
        }
        else
        {
            m_torchEmptyWarningRequested = true;
        }
    }
    m_lmbWasDown = lmbDown;

    // G, edge-triggered; ignored without stones
    const bool gKeyDown = glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS;
    if (gKeyDown && !m_gKeyWasDown && m_stoneCount > 0)
    {
        --m_stoneCount;
        m_throwStoneRequested = true;
    }
    m_gKeyWasDown = gKeyDown;

    // Dev keys from DevTools.h; compiled out without it
#ifdef HAS_DEV_TOOLS
    DevTools::ToggleDebugMap(window, m_debugMapVisible, m_mKeyWasDown);
    DevTools::ToggleInvisibleToEnemy(window, m_invisibleToEnemy, m_iKeyWasDown);

    const bool noclipWasEnabled = m_noclipEnabled;
    DevTools::ToggleNoclip(window, m_noclipEnabled, m_nKeyWasDown);
    if (noclipWasEnabled && !m_noclipEnabled)
    {
        // Movement never corrects Y, so restore the eye height.
        m_camPos.y = 0.5f;
    }

    DevTools::UpdateViewDistance(window, m_noclipEnabled, deltaTime);

    DevTools::ApplyHealthDebugKeys(window, m_health, m_maxHealth, m_hKeyWasDown, m_jKeyWasDown);
#endif

    {
        const float diaryInteractRadius = 1.2f; // a bit tighter than the exit door: a diary is smaller
        float bestDistSq = diaryInteractRadius * diaryInteractRadius;
        m_nearbyDiaryIndex = -1;
        for (size_t i = 0; i < diaryPositions.size(); ++i)
        {
            const glm::vec2 to(diaryPositions[i].x - m_camPos.x, diaryPositions[i].z - m_camPos.z);
            const float distSq = glm::dot(to, to);
            if (distSq <= bestDistSq)
            {
                bestDistSq = distSq;
                m_nearbyDiaryIndex = (int)i;
            }
        }
    }
    {
        // Tab toggles the journal anywhere
        const bool tabKeyDown = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
        if (tabKeyDown && !m_tabKeyWasDown)
        {
            m_journalToggleRequested = true;
        }
        m_tabKeyWasDown = tabKeyDown;
    }

    // Nearest untaken wall torch; the E block below reads it
    {
        const float torchInteractRadius = 0.7f; // small, so a torch is not taken from a distance
        float bestDistSq = torchInteractRadius * torchInteractRadius;
        m_nearbyTorchIndex = -1;
        for (size_t i = 0; i < torchPositions.size(); ++i)
        {
            if (i < torchTaken.size() && torchTaken[i])
                continue;
            const glm::vec2 to(torchPositions[i].x - m_camPos.x, torchPositions[i].z - m_camPos.z);
            const float distSq = glm::dot(to, to);
            if (distSq <= bestDistSq)
            {
                bestDistSq = distSq;
                m_nearbyTorchIndex = (int)i;
            }
        }
    }

    // E priority: exit door/donut, then a diary, then a wall torch
    if (!m_gameWon)
    {
        const bool eKeyDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
        if (eKeyDown && !m_eKeyWasDown)
        {
            glm::vec2 toButton(exitDoorPos.x - m_camPos.x, exitDoorPos.z - m_camPos.z);

            const float winInteractRadius = 1.4f;
            const bool nearExitDoor = glm::dot(toButton, toButton) <= winInteractRadius * winInteractRadius;

            if (winReadyToPressE && nearExitDoor)
            {
                // Request the credits; the game closes from there on ESC
                m_gameWon = true;
                m_creditsRequested = true;
                std::fprintf(stderr, "DungeonScene: exit (spinning torus) pressed - showing credits.\n");
            }
            else if (nearExitDoor && !m_winSequenceStarted)
            {
                if (diariesReadCount >= m_diariesToWin)
                {
                    m_winSequenceStarted = true;
                    m_winSequenceRequested = true;
                }
                else
                {
                    m_winBlockedRequested = true;
                }
            }
            else if (m_nearbyDiaryIndex != -1)
            {
                m_diaryOpenRequested = true;
            }
            else if (m_nearbyTorchIndex != -1)
            {
                // Third use of E: take the wall torch
                m_torchPickupRequested = true;
            }
        }
        m_eKeyWasDown = eKeyDown;
    }

    // Dying:
    // input and mouse look are frozen;
    // Application starts the menu fade via consumeDeathFadeTrigger()
    if (m_deathSequenceActive)
    {
        updateDeathSequence(deltaTime);
        return;
    }

    const float targetPose = m_compassVisible ? 1.0f : 0.0f;
    const float poseResponse = 5.5f;
    const float poseAlpha = 1.0f - std::exp(-poseResponse * deltaTime);
    m_poseBlend += (targetPose - m_poseBlend) * poseAlpha;
    if (std::abs(targetPose - m_poseBlend) < 0.0005f)
        m_poseBlend = targetPose;

    if (m_poseBlend > 0.0001f || m_compassVisible)
        m_poseTime += deltaTime;

    // Fuel drains while raised; an empty torch lowers itself
    if (m_torchRaised && m_torchFuel > 0.0f)
    {
        m_torchFuel -= kTorchFuelDrainPerSecond * deltaTime;
        if (m_torchFuel <= 0.0f)
        {
            m_torchFuel = 0.0f;
            m_torchRaised = false;
        }
    }

    const float targetTorchBlend = m_torchRaised ? 1.0f : 0.0f;
    const float torchResponse = 2.5f;
    const float torchAlpha = 1.0f - std::exp(-torchResponse * deltaTime);
    m_torchBlend += (targetTorchBlend - m_torchBlend) * torchAlpha;
    if (std::abs(targetTorchBlend - m_torchBlend) < 0.0005f)
        m_torchBlend = targetTorchBlend;

    glm::vec3 front = getFront();

    glm::vec3 flatFront(front.x, 0.0f, front.z);

    if (glm::dot(flatFront, flatFront) < 0.000001f)
    {
        flatFront = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    else
    {
        flatFront = glm::normalize(flatFront);
    }

    glm::vec3 right = glm::normalize(glm::cross(flatFront, glm::vec3(0.0f, 1.0f, 0.0f)));

    glm::vec3 move(0.0f);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        move += flatFront;

    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        move -= flatFront;

    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        move += right;

    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        move -= right;

    m_isMoving = glm::dot(move, move) > 0.000001f;

    // Shift while moving. Running also needs stamina, which may veto it below
    const bool wantsToRun =
        m_isMoving &&
        !m_compassVisible &&
        (
            glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS
        );

    m_isRunning = wantsToRun && (m_noclipEnabled || !m_staminaExhausted);

    // Before the stamina block, which charges stamina by m_isRunning
    if (!m_noclipEnabled && m_caughtTimer > 0.0f)
    {
        m_caughtTimer -= deltaTime;
        if (m_caughtTimer < 0.0f)
            m_caughtTimer = 0.0f;

        m_isRunning = false;
    }

    // At zero health stamina drains at the running rate. Frozen in noclip
    if (!m_noclipEnabled)
    {
        const bool zeroHealthDrain = (m_health <= 0.0f);

        if (m_isRunning || zeroHealthDrain)
        {
            m_stamina -= m_staminaDrainPerSec * deltaTime;
            if (m_stamina <= 0.0f)
            {
                m_stamina = 0.0f;
                m_staminaExhausted = true;
                m_isRunning = false;
            }
        }
        else
        {
            m_stamina += m_staminaRegenPerSec * deltaTime;
            if (m_stamina > m_maxStamina)
                m_stamina = m_maxStamina;

            if (m_staminaExhausted && m_stamina >= m_maxStamina * m_staminaResumeThreshold)
                m_staminaExhausted = false;
        }
    }

    const float walkSpeed = 1.5f;
    const float runSpeed  = 3.0f;

    const float noclipWalkSpeed = 4.5f;
    const float noclipRunSpeed  = 10.0f;

    float speed =
        m_noclipEnabled
        ? (m_isRunning ? noclipRunSpeed : noclipWalkSpeed)
        : (m_isRunning ? runSpeed : walkSpeed);

    if (!m_noclipEnabled && m_caughtTimer > 0.0f)
    {
        // Phase 2: slowed escape
        // Phase 1 is a plain walk, already selected by m_isRunning = false
        if (m_caughtTimer <= kCaughtPhase2Duration)
            speed = kCaughtDebuffSpeed;
    }

    const glm::vec3 footstepStartPos = m_camPos;

    if (m_isMoving)
    {
        move = glm::normalize(move) * speed * deltaTime;

        if (m_noclipEnabled)
        {
            m_camPos.x += move.x;
            m_camPos.z += move.z;
        }
        else
        {
            glm::vec3 pos = m_camPos;

            resolveMovement(
                pos,
                glm::vec3(move.x, 0.0f, move.z),
                isFloor,
                getCornerCut,
                getChamferSize,
                columnCentersXZ,
                exitDoorPos,
                enemyPositions
            );

            m_camPos = pos;
        }
    }

    if (!m_noclipEnabled && m_isMoving)
    {
        const glm::vec2 deltaXZ(m_camPos.x - footstepStartPos.x, m_camPos.z - footstepStartPos.z);
        const float movedDistance = glm::length(deltaXZ);

        if (movedDistance > 0.00001f)
        {
            const bool runNow = m_isRunning;
            const bool modeChanged = !m_footstepWasMoving || (runNow != m_footstepWasRunning);

            if (modeChanged)
            {
                m_footstepDistance = 0.0f;
                if (runNow)
                    m_footstepAudio.playRun();
                else
                    m_footstepAudio.playWalk();
            }

            m_footstepWasMoving = true;
            m_footstepWasRunning = runNow;
            m_footstepDistance += movedDistance;

            const float stepLength = runNow ? 0.95f : 0.68f;
            while (m_footstepDistance >= stepLength)
            {
                m_footstepDistance -= stepLength;
                if (runNow)
                    m_footstepAudio.playRun();
                else
                    m_footstepAudio.playWalk();
            }
        }
    }
    else
    {
        m_footstepWasMoving = false;
        m_footstepWasRunning = false;
        m_footstepDistance = 0.0f;
    }

    if (m_noclipEnabled)
    {
        const float flySpeed = m_isRunning ? noclipRunSpeed : noclipWalkSpeed;

        float vertical = 0.0f;

        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            vertical += 1.0f;

        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
            vertical -= 1.0f;

        if (vertical != 0.0f)
            m_camPos.y += vertical * flySpeed * deltaTime;
    }

    if (m_isMoving)
    {
        const float bobFrequency = 7.0f;

        // Smoothed dt so the bob looks the same at any fps
        m_bobPhase +=
            m_animSmoothedDt * bobFrequency;

        if (m_bobPhase > glm::two_pi<float>() * 100.0f)
        {
            m_bobPhase = std::fmod(m_bobPhase, glm::two_pi<float>());
        }
    }

    const float targetBlend = m_isMoving ? 1.0f : 0.0f;

    const float blendSpeed = m_isRunning ? 12.0f : 10.0f;

    if (m_bobBlend < targetBlend)
    {
        m_bobBlend = glm::min(m_bobBlend + deltaTime * blendSpeed, 1.0f);
    }
    else
    {
        m_bobBlend = glm::max(m_bobBlend - deltaTime * blendSpeed, 0.0f);
    }
}

void PlayerController::processMouse(double xpos, double ypos) {
    if (m_firstMouse) {
        m_lastX = xpos; m_lastY = ypos;
        m_firstMouse = false;
    }

    const float sensitivity = m_mouseSensitivity;
    const float dx = (float)(xpos - m_lastX) * sensitivity;
    const float dy = (float)(m_lastY - ypos) * sensitivity;
    m_lastX = xpos;
    m_lastY = ypos;

    m_yaw += dx;
    m_pitch += dy;

    // Look limits apply only while the minimap is shown, not during the lowering animation
    if (m_compassVisible)
    {
        m_pitch = glm::clamp(m_pitch, -8.0f, 20.0f);
    }
    else
    {
        m_pitch = glm::clamp(m_pitch, -89.0f, 89.0f);
    }
}

void PlayerController::tickPauseCameraIdle(float deltaTime)
{
    deltaTime = glm::clamp(deltaTime, 0.0f, 0.05f);

    advanceSmoothedAnimDt(deltaTime);
    m_cameraAnimTime += m_animSmoothedDt;
}

glm::vec3 PlayerController::getCameraRenderPosition() const
{
    glm::vec3 front = getFront();

    glm::vec3 flatFront(front.x, 0.0f, front.z);

    if (glm::dot(flatFront, flatFront) < 0.000001f)
    {
        flatFront = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    else
    {
        flatFront = glm::normalize(flatFront);
    }

    glm::vec3 right = glm::normalize(glm::cross(flatFront, glm::vec3(0.0f, 1.0f, 0.0f)));

    const float t = m_cameraAnimTime;

    const float phase = m_bobPhase;

    const float moving = m_bobBlend;

    const float running = m_isRunning ? 1.0f : 0.0f;

    const float idleVertical = std::sin(t * 3.0f) * 0.007f + std::sin(t * 4.0f) * 0.003f;

    const float walkVerticalAmp = 0.014f;

    const float runVerticalAmp = 0.030f;

    const float verticalAmp = glm::mix(walkVerticalAmp, runVerticalAmp, running);

    const float walkSideAmp = 0.007f;

    const float runSideAmp = 0.030f;

    const float sideAmp = glm::mix(walkSideAmp, runSideAmp, running);

    const float walkForwardAmp = 0.003f;

    const float runForwardAmp = 0.010f;

    const float forwardAmp = glm::mix(walkForwardAmp, runForwardAmp, running);

    const float verticalBob = idleVertical + moving * std::sin(phase * 2.0f) * verticalAmp;

    const float sideSway = std::sin(t * 0.90f) * 0.004f + moving * std::sin(phase) * sideAmp;

    const float forwardBob = moving * std::sin(phase * 2.0f + 0.5f) * forwardAmp;

    return
        m_camPos
        +
        right * sideSway
        +
        flatFront * forwardBob
        +
        glm::vec3(0.0f, verticalBob + deathCameraYOffset(), 0.0f);
}

glm::vec3 PlayerController::getCameraRenderUp() const
{
    glm::vec3 front = getFront();

    const float t = m_cameraAnimTime;

    const float phase = m_bobPhase;

    const float moving = m_bobBlend;

    const float running = m_isRunning ? 1.0f : 0.0f;

    const float idleRoll = std::sin(t * 0.82f) * 0.28f + std::sin(t * 1.37f) * 0.10f;

    const float walkRollAmp = 0.65f;

    const float runRollAmp = 2.20f;

    const float activeRoll = glm::mix(walkRollAmp, runRollAmp, running);

    const float rollDegrees =
        idleRoll
        +
        moving
        * std::sin(phase)
        * activeRoll
        +
        deathCameraRollDegrees();

    glm::mat4 rollMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(rollDegrees), front);

    return
        glm::normalize(glm::vec3(rollMatrix * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
}

