#include "EnemyAI.h"
#include "LineOfSight.h"
#include "GridPathfinding.h"
#include "PlayerController.h"
#include <algorithm>
#include <cmath>

namespace {
// Shortest signed angle from current to target, in (-180, 180]: rotation takes the short way and
// has no jump at ±180.
float ShortestAngleDeltaDeg(float currentDeg, float targetDeg)
{
    float delta = std::fmod(targetDeg - currentDeg, 360.0f);
    if (delta > 180.0f) delta -= 360.0f;
    else if (delta < -180.0f) delta += 360.0f;
    return delta;
}

float NormalizeAngleDeg(float deg)
{
    deg = std::fmod(deg, 360.0f);
    if (deg > 180.0f) deg -= 360.0f;
    else if (deg <= -180.0f) deg += 360.0f;
    return deg;
}

// Flee guessing: how far (in cells) to extrapolate along the player's last movement direction
// before assuming a wall or dead end.
constexpr int kFleeGuessMaxCells = 5;
constexpr float kFleeGuessMeaningfulSpeed = 0.3f; // units/sec: below this it is jitter, not walking
// sec: this much stillness does not yet count as standing still
constexpr float kFleeGuessMovementMemory = 0.4f;
// How deep to look into a side turn (1 cell is the turn itself; more covers a player who kept
// running down that branch).
constexpr int kFleeGuessBranchCells = 2;

// Distance-based volume: full at nearRadius and closer, silent at farRadius and beyond, with an
// ease-out squared falloff in between (closer to how sound fades than linear).
float DistanceVolume(float distance, float nearRadius, float farRadius)
{
    if (distance <= nearRadius) return 1.0f;
    if (distance >= farRadius) return 0.0f;
    const float t = (distance - nearRadius) / (farRadius - nearRadius);
    return 1.0f - t * t;
}

// Audibility radii per sound type. The detection scream carries farther on purpose: it is a warning
// that should be heard even away from the enemy.
constexpr float kFootstepAudibleNear = 2.0f;
constexpr float kFootstepAudibleFar = 14.0f;
constexpr float kMoanAudibleNear = 2.0f;
constexpr float kMoanAudibleFar = 16.0f;
constexpr float kDetectedAudibleNear = 3.0f;
constexpr float kDetectedAudibleFar = 22.0f;
}

void EnemyAI::init(
    int mapW, int mapH,
    const std::vector<int>* map,
    const std::vector<WallShapes::CornerCut>* cornerCuts,
    const std::vector<unsigned char>* diagonalChainMask,
    const std::vector<glm::vec2>* columnCentersXZ,
    float columnRadius,
    const std::vector<int>* pathfindingMap)
{
    m_mapW = mapW;
    m_mapH = mapH;
    m_map = map;
    m_cornerCuts = cornerCuts;
    m_diagonalChainMask = diagonalChainMask;
    m_columnCentersXZ = columnCentersXZ;
    m_columnRadius = columnRadius;

    m_pathfindingMap = pathfindingMap;

    m_audio.init();

    m_moanTimer = 0.0f;
    std::uniform_real_distribution<float> initialMoanDist(9.0f, 18.0f);
    m_nextMoanInterval = initialMoanDist(m_rng);

    m_hasPatrolTarget = false;
    m_patrolPaused = false;
    m_patrolPauseTimer = 0.0f;

    // EnemyAI persists across playthroughs while init() runs for every new map, so the spotted flag
    // must be reset here; otherwise the enemy shows up on the minimap before the first encounter.
    m_hasBeenSpotted = false;
    m_timeSinceLastSpotted = 0.0f;
}

bool EnemyAI::isWalkableCell(int x, int z) const
{
    if (!m_pathfindingMap || m_pathfindingMap->empty()) return false;
    if (x < 0 || z < 0 || x >= m_mapW || z >= m_mapH) return false;
    return (*m_pathfindingMap)[(size_t)z * m_mapW + x] != 1;
}

glm::vec3 EnemyAI::pickRandomPatrolPoint()
{
    if (!m_pathfindingMap || m_pathfindingMap->empty() || m_mapW <= 0 || m_mapH <= 0)
        return m_position;

    std::uniform_int_distribution<int> distX(0, m_mapW - 1);
    std::uniform_int_distribution<int> distZ(0, m_mapH - 1);

    // A maze is mostly walls, so a random pick can miss. After a bounded number of tries stay in
    // place; the next call tries again.
    for (int attempt = 0; attempt < 30; ++attempt)
    {
        const int x = distX(m_rng);
        const int z = distZ(m_rng);
        if (isWalkableCell(x, z))
            return glm::vec3(x + 0.5f, 0.0f, z + 0.5f);
    }
    return m_position;
}

void EnemyAI::appendGuessedFleeWaypoints(const glm::vec3& lastSeenPos, const glm::vec3& headingAtLoss, bool headingWasFresh, std::vector<glm::vec3>& outWaypoints) const
{
    if (!headingWasFresh)
        return;

    // A diagonal heading is almost always single-frame noise and corridors are axis-aligned, so
    // guess along the one dominant axis.
    const glm::vec2 heading2D(headingAtLoss.x, headingAtLoss.z);
    if (glm::length(heading2D) < 0.001f)
        return;

    glm::ivec2 primaryDir;
    if (std::abs(heading2D.x) >= std::abs(heading2D.y))
        primaryDir = glm::ivec2(heading2D.x > 0.0f ? 1 : -1, 0);
    else
        primaryDir = glm::ivec2(0, heading2D.y > 0.0f ? 1 : -1);

    const glm::ivec2 startCell((int)std::floor(lastSeenPos.x), (int)std::floor(lastSeenPos.z));
    glm::ivec2 furthestStraight = startCell;
    bool branchFound = false;
    glm::ivec2 branchCell;

    for (int step = 1; step <= kFleeGuessMaxCells; ++step)
    {
        const glm::ivec2 next = startCell + primaryDir * step;
        if (!isWalkableCell(next.x, next.y))
            break;
        furthestStraight = next;

        // Also check side branches (perpendicular to the running direction): corridors fork and the
        // player may have turned at the nearest corner. The first one found is enough; this is not
        // meant to be a full branch sweep.
        if (!branchFound)
        {
            const glm::ivec2 perp = (primaryDir.x != 0) ? glm::ivec2(0, 1) : glm::ivec2(1, 0);
            for (int side = -1; side <= 1; side += 2)
            {
                const glm::ivec2 branch = next + perp * side;
                if (isWalkableCell(branch.x, branch.y))
                {
                    branchFound = true;
                    branchCell = branch;
                    // Look a couple of cells into the turn, otherwise the guessed point almost
                    // coincides with the straight line.
                    for (int bstep = 2; bstep <= kFleeGuessBranchCells; ++bstep)
                    {
                        const glm::ivec2 deeper = next + perp * side * bstep;
                        if (!isWalkableCell(deeper.x, deeper.y))
                            break;
                        branchCell = deeper;
                    }
                    break;
                }
            }
        }
    }

    if (furthestStraight != startCell)
        outWaypoints.push_back(glm::vec3(furthestStraight.x + 0.5f, 0.0f, furthestStraight.y + 0.5f));

    if (branchFound)
        outWaypoints.push_back(glm::vec3(branchCell.x + 0.5f, 0.0f, branchCell.y + 0.5f));
}

bool EnemyAI::followPath(const glm::vec3& target, float speed, float deltaTime)
{
    const float kPathRecomputeInterval = 0.4f;
    m_pathRecomputeTimer += deltaTime;

    if (m_pathRecomputeTimer >= kPathRecomputeInterval || m_path.empty())
    {
        m_pathRecomputeTimer = 0.0f;

        const glm::ivec2 startCell((int)std::floor(m_position.x), (int)std::floor(m_position.z));
        const glm::ivec2 goalCell((int)std::floor(target.x), (int)std::floor(target.z));

        std::vector<glm::ivec2> cellPath;
        // Use m_pathfindingMap, not m_map: it also marks column cells as blocked, so paths do not
        // route through columns.
        if (m_pathfindingMap && !m_pathfindingMap->empty())
            cellPath = GridPathfinding::FindPath(m_mapW, m_mapH, *m_pathfindingMap, startCell, goalCell);

        // 4-directional BFS picks a "staircase" among many equal-length paths; SmoothPath()
        // straightens it into diagonal segments where the line of sight allows.
        if (m_pathfindingMap && !m_pathfindingMap->empty())
            cellPath = GridPathfinding::SmoothPath(m_mapW, m_mapH, *m_pathfindingMap, cellPath);

        m_path.clear();
        for (const glm::ivec2& c : cellPath)
            m_path.push_back(glm::vec2(c.x + 0.5f, c.y + 0.5f));
        m_pathWaypointIndex = 0;

        // The first point of a fresh path is the current cell's center. If the enemy is not exactly
        // there, that point can be slightly behind it, and reaching it means a small step backward
        // on every recompute. Skip it when it is already close.
        if (m_path.size() > 1)
        {
            const float distToFirst = glm::length(m_path[0] - glm::vec2(m_position.x, m_position.z));
            if (distToFirst < 0.5f)
                m_pathWaypointIndex = 1;
        }
    }

    if (m_pathWaypointIndex >= m_path.size())
        return m_path.empty() ? false : true; // no path found: not "reached", just nowhere to go

    const glm::vec2 targetXZ = m_path[m_pathWaypointIndex];
    glm::vec3 toTarget(targetXZ.x - m_position.x, 0.0f, targetXZ.y - m_position.z);
    float dist = glm::length(toTarget);

    const float kWaypointReachedRadius = 0.25f;
    if (dist < kWaypointReachedRadius)
    {
        if (m_pathWaypointIndex + 1 < m_path.size())
        {
            m_pathWaypointIndex++;
            const glm::vec2 nextXZ = m_path[m_pathWaypointIndex];
            toTarget = glm::vec3(nextXZ.x - m_position.x, 0.0f, nextXZ.y - m_position.z);
            dist = glm::length(toTarget);
        }
        else
        {
            return true;
        }
    }

    if (dist > 0.01f)
    {
        const glm::vec3 dir = toTarget / dist;
        const float moveDist = std::min(dist, speed * deltaTime);
        m_position += dir * moveDist;

        // Yaw is atan2(dir.x, dir.z). This assumes the model's forward axis is local -Z, as is
        // usual for Blender -> glTF exports; the formula turns that axis toward dir. If the model
        // ever faces the wrong way, fix it here with a fixed offset.
        const float desiredYawDegrees = glm::degrees(std::atan2(dir.x, dir.z));

        // Turn toward the desired yaw at a capped rate instead of snapping to it: on a path
        // recompute or a waypoint change the direction can jump by tens of degrees in one frame,
        // which looked like an algorithm rather than a creature turning.
        const float kTurnRateDegPerSec = 225.0f; // deg/sec (~0.8 s for a 180° turn)
        const float maxTurnDelta = kTurnRateDegPerSec * deltaTime;
        float turnDelta = ShortestAngleDeltaDeg(m_yawDegrees, desiredYawDegrees);
        turnDelta = glm::clamp(turnDelta, -maxTurnDelta, maxTurnDelta);
        m_yawDegrees = NormalizeAngleDeg(m_yawDegrees + turnDelta);
    }

    // Stuck safeguard: if the body barely advanced over a check interval, nudge it aside and force
    // a path recompute. It is a general escape from geometric traps (a path can legally hug a cell
    // boundary while the circular collider clips neighboring cells) instead of chasing each case.
    {
        const float kStuckCheckInterval = 0.5f;
        // Over one check interval the body covers ~0.275 units unimpeded; 0.15 leaves nearly 2x
        // margin for normal slowdowns (waypoint pauses, path start) without mistaking them for
        // being stuck.
        const float kStuckMinProgress = 0.15f;

        m_stuckCheckTimer += deltaTime;
        if (!m_stuckCheckRefInit)
        {
            m_stuckCheckRefPos = m_position;
            m_stuckCheckRefInit = true;
        }
        else if (m_stuckCheckTimer >= kStuckCheckInterval)
        {
            const float progressed = glm::length(m_position - m_stuckCheckRefPos);
            if (progressed < kStuckMinProgress)
            {
                // Random direction and a modest distance: just enough to escape a tight gap. The
                // result goes through resolveWallCollision() so the nudge does not land in a wall.
                std::uniform_real_distribution<float> angleDist(0.0f, 6.2831853f);
                const float angle = angleDist(m_rng);
                const float kEscapeDistance = 0.3f;
                const glm::vec3 nudged = m_position +
                    glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * kEscapeDistance;
                m_position = resolveWallCollision(nudged);

                m_path.clear();
                m_pathRecomputeTimer = kPathRecomputeInterval;
            }
            m_stuckCheckTimer = 0.0f;
            m_stuckCheckRefPos = m_position;
        }
    }

    return false;
}

void EnemyAI::update(float deltaTime, const glm::vec3& playerPos, bool playerIsRunning, bool playerIsMoving, bool playerInvisible, bool playerCanSeeThisEnemy, float playerLightLevel, EnemyCharacter& character)
{
    // Walk speed is tuned to the walk clip's stride length. The clip plays in real time, so a
    // higher speed makes the legs skate. The full fix would be distance-based clip playback.
    const float kEnemyWalkSpeed = 0.55f;
    const float kEnemyRunSpeed = 2.0f;
    const float kDashSpeedMultiplier = 1.5f;
    const float kCatchRadius = 0.6f;
    const float kLoseTrackGrace = 5.0f; // sec without contact before the chase turns into a search

    // Estimated player speed for the dash's lead prediction, same values as PlayerController's
    // walk/run speed. Only positions are visible here, so this is an estimate.
    const float kEstimatedPlayerSpeed = playerIsRunning ? 3.0f : 1.5f;

    // Position snapshot before any movement this frame: footsteps are paced by the distance
    // actually moved (like PlayerController::processInput()). Attack/WallSlam/Scream return
    // earlier, so it is unused there.
    const glm::vec3 footstepStartPos = m_position;

    if (m_lastPlayerPosInit)
    {
        glm::vec3 delta = playerPos - m_lastPlayerPos;
        delta.y = 0.0f;
        const float len = glm::length(delta);
        if (len > 0.001f)
            m_playerHeading = delta / len;

        // Real speed is distance / deltaTime (m_playerHeading is always unit length). The stillness
        // timer resets on real movement; kFleeGuessMovementMemory is a grace window so one still
        // frame does not erase recent movement.
        const float speed = (deltaTime > 0.0001f) ? (len / deltaTime) : 0.0f;
        if (speed >= kFleeGuessMeaningfulSpeed)
            m_timeSinceMeaningfulMovement = 0.0f;
        else
            m_timeSinceMeaningfulMovement += deltaTime;
    }
    m_lastPlayerPos = playerPos;
    m_lastPlayerPosInit = true;

    // Minimap visibility (whether the PLAYER has seen this enemy) is independent of AI perception.
    // It is computed every frame from outside (DungeonScene::isEnemyVisibleToPlayer(): FOV + LOS
    // from the player's camera), not on the AI's own perception tick.
    if (playerCanSeeThisEnemy)
    {
        m_hasBeenSpotted = true;
        m_timeSinceLastSpotted = 0.0f;
    }
    else if (m_hasBeenSpotted)
    {
        m_timeSinceLastSpotted += deltaTime;
        if (m_timeSinceLastSpotted >= kSpottedMemoryDuration)
            m_hasBeenSpotted = false;
    }

    if (m_state == AIState::Attack)
    {
        m_stateTimer -= deltaTime;
        if (m_stateTimer <= 0.0f)
        {
            m_state = AIState::Run;
            m_path.clear();
            m_attackCooldownTimer = 1.4f;
        }

        m_position = resolveWallCollision(m_position);
        character.setPosition(m_position);
        character.setYawDegrees(m_yawDegrees);
        character.setState(EnemyCharacter::State::Attack);
        character.update(deltaTime);
        return;
    }

    if (m_state == AIState::WallSlam)
    {
        m_stateTimer -= deltaTime;
        if (m_stateTimer <= 0.0f)
        {
            m_state = AIState::Walk;
            m_path.clear();
        }

        // Resolve collisions here too: Dash stops against a wall on the impact frame, and without
        // this the enemy could stay embedded in geometry for the whole WallSlam duration.
        m_position = resolveWallCollision(m_position);
        character.setPosition(m_position);
        character.setYawDegrees(m_yawDegrees);
        character.setState(EnemyCharacter::State::WallSlam);
        character.update(deltaTime);
        return;
    }

    if (m_state == AIState::Scream)
    {
        m_stateTimer -= deltaTime;
        if (m_stateTimer <= 0.0f)
        {
            m_state = AIState::Run;
            m_path.clear();
        }

        m_position = resolveWallCollision(m_position);
        character.setPosition(m_position);
        character.setYawDegrees(m_yawDegrees);
        character.setState(EnemyCharacter::State::Scream);
        character.update(deltaTime);
        return;
    }

    if (m_state != AIState::Dash)
    {
        const float kPerceptionInterval = 0.2f; // 5 times/sec
        m_perceptionTimer += deltaTime;

        if (m_perceptionTimer >= kPerceptionInterval)
        {
            const float elapsed = m_perceptionTimer;
            m_perceptionTimer = 0.0f;

            const glm::vec2 fromXZ(m_position.x, m_position.z);
            const glm::vec2 toXZ(playerPos.x, playerPos.z);
            const float dist = glm::length(toXZ - fromXZ);

            const float kSightRadiusWalk = 8.0f;
            const float kSightRadiusRun = 14.0f;
            const float baseSightRadius = playerIsRunning ? kSightRadiusRun : kSightRadiusWalk;

            // Light affects sight: kMinSightMultiplier keeps a nonzero radius in full darkness, so
            // darkness hides the player but does not guarantee stealth (only dev-tools
            // playerInvisible does). lightLevel = 1 gives multiplier 1.0.
            const float kMinSightMultiplier = 0.35f;
            const float sightMultiplier = glm::mix(kMinSightMultiplier, 1.0f,
                                                    glm::clamp(playerLightLevel, 0.0f, 1.0f));
            const float sightRadius = baseSightRadius * sightMultiplier;

            bool canSee = dist <= sightRadius;

            // Field of view: a cone of ±kFOVHalfAngleDeg around the current yaw. Within
            // kPeripheralRadius the player is noticed regardless, otherwise standing right beside
            // the enemy would go unnoticed.
            const float kFOVHalfAngleDeg = 55.0f; // ~110° full cone
            const float kPeripheralRadius = 1.2f;
            if (canSee && dist > kPeripheralRadius)
            {
                const float yawRad = glm::radians(m_yawDegrees);
                const glm::vec2 forward(std::sin(yawRad), std::cos(yawRad));
                const glm::vec2 toPlayerDir = (toXZ - fromXZ) / dist;
                const float cosHalfFOV = std::cos(glm::radians(kFOVHalfAngleDeg));
                canSee = glm::dot(forward, toPlayerDir) >= cosHalfFOV;
            }

            if (canSee && m_map)
            {
                canSee = LineOfSight::HasLineOfSight(
                    fromXZ, toXZ,
                    m_mapW, m_mapH,
                    *m_map, *m_cornerCuts, *m_diagonalChainMask,
                    *m_columnCentersXZ, m_columnRadius);
            }

            // Dev tools (I key): applied after the real FOV/LOS/radius check, so every other system
            // (m_hasBeenSpotted, memory window, timers) sees an ordinary loss of sight. Hearing is
            // muted by the same flag below.
            if (playerInvisible)
                canSee = false;

            if (canSee)
            {
                m_lostTrackTimer = 0.0f;
                m_lastKnownPlayerPos = playerPos;

                const bool closeEnough = dist < sightRadius * 0.6f;
                if (closeEnough)
                {
                    if (m_state != AIState::Run)
                    {
                        m_state = AIState::Scream;
                        m_stateTimer = 1.25f;
                        m_audio.playDetected(DistanceVolume(dist, kDetectedAudibleNear, kDetectedAudibleFar));
                    }
                }
                else if (m_state != AIState::Run)
                {
                    m_state = AIState::Walk;
                }
            }
            else
            {
                // Cannot see the player right now: if it was chasing (Walk/Run), keep a memory
                // window instead of resetting instantly.
                if (m_state == AIState::Run || m_state == AIState::Walk)
                {
                    m_lostTrackTimer += elapsed;
                    if (m_lostTrackTimer >= kLoseTrackGrace)
                    {
                        // Goes to Search rather than straight to Idle: it heads to the last seen
                        // position and looks around before returning to patrol, so it does not
                        // abruptly "forget" the chase.
                        m_state = AIState::Search;
                        m_lostTrackTimer = 0.0f;
                        m_searchPaused = false;
                        m_searchPauseTimer = 0.0f;
                        m_path.clear();

                        // Flee guesses: first the last seen position, then, if the player's
                        // direction was known, a guessed continuation of the route (plus a side
                        // turn if one came up).
                        m_searchWaypoints.clear();
                        m_searchWaypoints.push_back(m_lastKnownPlayerPos);
                        const bool headingWasFresh = m_timeSinceMeaningfulMovement <= kFleeGuessMovementMemory;
                        appendGuessedFleeWaypoints(m_lastKnownPlayerPos, m_playerHeading, headingWasFresh, m_searchWaypoints);
                        m_searchWaypointIndex = 0;
                    }
                }
                // Deliberately no else branch: when the player is not seen, Idle is not chasing
                // anyway and Search decides for itself when to end. An unconditional else { Idle }
                // would overwrite Search on every perception tick.

                // Hearing: independent of vision and FOV (sound passes through walls), only while
                // not chasing. It leads to Search, never straight to Run/Scream. playerInvisible
                // mutes it too (full stealth).
                if (!playerInvisible &&
                    m_state == AIState::Idle && (playerIsMoving || playerIsRunning))
                {
                    const float kHearingRadiusWalk = 4.0f;
                    const float kHearingRadiusRun = 7.0f;
                    const float hearingRadius = playerIsRunning ? kHearingRadiusRun : kHearingRadiusWalk;
                    if (dist <= hearingRadius)
                    {
                        m_state = AIState::Search;
                        m_lastKnownPlayerPos = playerPos; // the sound came from here
                        m_searchPaused = false;
                        m_searchPauseTimer = 0.0f;
                        m_path.clear();

                        // Same guess chain as after losing a chase: if the player was moving at the
                        // time of the noise, also guess where they ran further.
                        m_searchWaypoints.clear();
                        m_searchWaypoints.push_back(m_lastKnownPlayerPos);
                        const bool headingWasFresh = m_timeSinceMeaningfulMovement <= kFleeGuessMovementMemory;
                        appendGuessedFleeWaypoints(m_lastKnownPlayerPos, m_playerHeading, headingWasFresh, m_searchWaypoints);
                        m_searchWaypointIndex = 0;
                    }
                }
            }
        }
    }

    if (m_state == AIState::Run)
    {
        m_dashRollTimer += deltaTime;
        if (m_dashRollTimer >= 1.0f)
        {
            m_dashRollTimer = 0.0f;

            // The direction blends the player's heading (lead prediction from estimated speed) and
            // their current position in one vector from predictedPlayerPos, so a player who stands
            // still or turns sharply does not make the dash fly past.
            const float kLeadTime = 0.35f;
            const glm::vec3 predictedPlayerPos = playerPos + m_playerHeading * kEstimatedPlayerSpeed * kLeadTime;
            glm::vec3 toPredicted = predictedPlayerPos - m_position;
            toPredicted.y = 0.0f;
            const float toPredictedLen = glm::length(toPredicted);
            const glm::vec3 candidateDir = toPredictedLen > 0.001f
                ? toPredicted / toPredictedLen
                : m_playerHeading;

            glm::vec3 toPlayerNow = playerPos - m_position;
            toPlayerNow.y = 0.0f;
            const float distToPlayerNow = glm::length(toPlayerNow);

            // Do not launch when there is a wall right along the chosen course: it would turn into
            // Wall_slam on the first frame. The course is not recalculated mid-dash, so hitting a
            // wall later remains a real risk; only an immediate miss is filtered out.
            bool hasRoomAhead = true;
            if (m_map)
            {
                const float kMinClearDistance = 1.5f;
                const glm::vec3 probePos = m_position + candidateDir * kMinClearDistance;
                const glm::ivec2 probeCell((int)std::floor(probePos.x), (int)std::floor(probePos.z));
                hasRoomAhead = isWalkableCell(probeCell.x, probeCell.y);
            }

            if (hasRoomAhead)
            {
                // The chance grows as the player gets closer: a close dash is tactically justified,
                // a distant one is almost always wasted.
                const float kBaseChance = 0.10f;
                const float kMaxChance = 0.25f;
                const float kDashChanceRange = 8.0f; // distance beyond which it's the base chance
                const float t = glm::clamp(1.0f - distToPlayerNow / kDashChanceRange, 0.0f, 1.0f);
                const float chance = kBaseChance + (kMaxChance - kBaseChance) * t;

                std::uniform_real_distribution<float> roll(0.0f, 1.0f);
                if (roll(m_rng) < chance)
                {
                    m_state = AIState::Dash;
                    m_dashDirection = candidateDir;
                    m_dashElapsed = 0.0f;
                }
            }
        }
    }
    else
    {
        m_dashRollTimer = 0.0f;
    }

    if (m_state == AIState::Dash)
    {
        m_dashElapsed += deltaTime;

        const float dashSpeed = kEnemyRunSpeed * kDashSpeedMultiplier;
        const glm::vec3 nextPos = m_position + m_dashDirection * dashSpeed * deltaTime;

        // Hit detection goes through the collider: resolve nextPos and check whether it was pushed
        // back against the dash direction. A "next cell is a wall" test would never fire, because
        // the collider stops the enemy first and the dash would idle until its timeout. Sliding
        // along a wall does not count as a head-on hit.
        const glm::vec3 resolvedNextPos = resolveWallCollision(nextPos);
        glm::vec3 pushback = resolvedNextPos - nextPos;
        pushback.y = 0.0f;
        // slack so sliding along a wall does not trigger a slam
        const float kWallSlamPushbackThreshold = 0.02f;
        bool blocked = -glm::dot(pushback, m_dashDirection) > kWallSlamPushbackThreshold;

        // The collider skips out-of-grid cells. The map is walled around its perimeter, so this
        // should not happen; the explicit check is a safety net.
        if (!blocked && m_map)
        {
            const glm::ivec2 nextCell((int)std::floor(nextPos.x), (int)std::floor(nextPos.z));
            blocked = nextCell.x < 0 || nextCell.y < 0 || nextCell.x >= m_mapW || nextCell.y >= m_mapH;
        }

        glm::vec3 toPlayer = playerPos - m_position;
        toPlayer.y = 0.0f;
        const float distToPlayer = glm::length(toPlayer);

        if (m_attackCooldownTimer > 0.0f)
            m_attackCooldownTimer -= deltaTime;

        if (m_attackCooldownTimer <= 0.0f && distToPlayer < kCatchRadius)
        {
            m_state = AIState::Attack;
            m_stateTimer = 1.25f;
            m_justCaughtPlayer = true;
            m_audio.playAttack(DistanceVolume(distToPlayer, kFootstepAudibleNear, kFootstepAudibleFar));
        }
        else if (blocked)
        {
            m_state = AIState::WallSlam;
            m_stateTimer = 2.67f;
            m_audio.playWallSlam(DistanceVolume(distToPlayer, kFootstepAudibleNear, kFootstepAudibleFar));
        }
        else if (m_dashElapsed > 3.0f)
        {
            m_state = AIState::Run;
            m_path.clear();
        }
        else
        {
            m_position = nextPos;
            // This snap is deliberate, unlike in followPath(): m_dashDirection is locked for the
            // whole Dash, so it is one instant turn at launch, which reads as a sharp, aggressive
            // lunge rather than a repeated pathfinding teleport.
            m_yawDegrees = glm::degrees(std::atan2(m_dashDirection.x, m_dashDirection.z));
        }
    }
    else if (m_state == AIState::Walk || m_state == AIState::Run)
    {
        if (m_attackCooldownTimer > 0.0f)
            m_attackCooldownTimer -= deltaTime;

        // Catching is checked against the player's real position, not the last known one, so memory
        // alone cannot catch anyone. The attack cooldown prevents instant re-hits.
        glm::vec3 toPlayerReal = playerPos - m_position;
        toPlayerReal.y = 0.0f;

        if (m_attackCooldownTimer <= 0.0f && glm::length(toPlayerReal) < kCatchRadius)
        {
            m_state = AIState::Attack;
            m_stateTimer = 1.25f;
            m_justCaughtPlayer = true;
            m_audio.playAttack(DistanceVolume(glm::length(toPlayerReal), kFootstepAudibleNear, kFootstepAudibleFar));
        }
        else
        {
            // Heads for the last known position: it equals the live one while the player is visible
            // (updated on every successful canSee) and stays frozen at the last sighting through
            // the memory window.
            const float speed = (m_state == AIState::Run) ? kEnemyRunSpeed : kEnemyWalkSpeed;
            followPath(m_lastKnownPlayerPos, speed, deltaTime);
        }
    }
    else if (m_state == AIState::Search)
    {
        // Visit m_searchWaypoints in order ([0] is the noise source or last seen position, then 0-2
        // flee guesses). An empty chain (never expected) returns to patrol instead of getting
        // stuck.
        if (m_searchWaypoints.empty())
        {
            m_state = AIState::Idle;
            m_hasPatrolTarget = false;
            m_path.clear();
        }
        else if (m_searchPaused)
        {
            m_searchPauseTimer -= deltaTime;
            if (m_searchPauseTimer <= 0.0f)
            {
                m_searchPaused = false;
                ++m_searchWaypointIndex;
                if (m_searchWaypointIndex >= m_searchWaypoints.size())
                {
                    m_state = AIState::Idle;
                    m_hasPatrolTarget = false;
                    m_path.clear();
                }
                else
                {
                    m_path.clear();
                }
            }
        }
        else
        {
            const glm::vec3& target = m_searchWaypoints[m_searchWaypointIndex];
            const bool reached = followPath(target, kEnemyWalkSpeed, deltaTime);
            if (reached)
            {
                m_searchPaused = true;

                // The last point gets the longer look-around pause (the final check before giving
                // up); intermediate guesses get a short glance, otherwise Search would drag on.
                const bool isLastWaypoint = (m_searchWaypointIndex + 1 >= m_searchWaypoints.size());
                std::uniform_real_distribution<float> pauseDist(
                    isLastWaypoint ? 1.5f : 0.7f,
                    isLastWaypoint ? 3.0f : 1.4f);
                m_searchPauseTimer = pauseDist(m_rng);
            }
        }
    }
    else
    {
        if (m_patrolPaused)
        {
            m_patrolPauseTimer -= deltaTime;
            if (m_patrolPauseTimer <= 0.0f)
            {
                m_patrolPaused = false;
                m_hasPatrolTarget = false;
            }
        }
        else
        {
            if (!m_hasPatrolTarget)
            {
                m_patrolTarget = pickRandomPatrolPoint();
                m_hasPatrolTarget = true;
                m_path.clear();
            }

            const bool reached = followPath(m_patrolTarget, kEnemyWalkSpeed, deltaTime);
            if (reached)
            {
                m_patrolPaused = true;
                std::uniform_real_distribution<float> pauseDist(1.0f, 2.5f);
                m_patrolPauseTimer = pauseDist(m_rng);
            }
        }
    }

    // Collider: the single point every movement of Idle/Search/Walk/Run/Dash passes through before
    // character.setPosition(). Attack/WallSlam/Scream resolve it in their own early returns.
    m_position = resolveWallCollision(m_position);
    character.setPosition(m_position);
    character.setYawDegrees(m_yawDegrees);
    if (m_state == AIState::Idle || m_state == AIState::Search)
    {
        // Moans only while the enemy searches or wanders on its own (Idle patrol or Search), not
        // during a chase: chase states already play Scream at launch. Chase growling is
        // deliberately not implemented.
        m_moanTimer += deltaTime;
        if (m_moanTimer >= m_nextMoanInterval)
        {
            m_moanTimer = 0.0f;
            std::uniform_real_distribution<float> intervalDist(9.0f, 18.0f);
            m_nextMoanInterval = intervalDist(m_rng);
            const float distToPlayer = glm::length(glm::vec2(playerPos.x - m_position.x, playerPos.z - m_position.z));
            m_audio.playMoan(DistanceVolume(distToPlayer, kMoanAudibleNear, kMoanAudibleFar));
        }
    }
    else
    {
        m_moanTimer = 0.0f;
    }

    EnemyCharacter::State animState = EnemyCharacter::State::Idle;
    // Patrol (Idle while walking to a point) reuses the Walk clip: there is no dedicated calm-walk
    // clip. Paused, it uses Idle.
    if (m_state == AIState::Idle)
        animState = m_patrolPaused ? EnemyCharacter::State::Idle : EnemyCharacter::State::Walk;
    else if (m_state == AIState::Search)
        animState = m_searchPaused ? EnemyCharacter::State::Idle : EnemyCharacter::State::Walk;
    else if (m_state == AIState::Walk)
        animState = EnemyCharacter::State::Walk;
    else if (m_state == AIState::Run || m_state == AIState::Dash)
        animState = EnemyCharacter::State::Run;
    else if (m_state == AIState::Attack)
        animState = EnemyCharacter::State::Attack;
    else if (m_state == AIState::WallSlam)
        animState = EnemyCharacter::State::WallSlam;
    else if (m_state == AIState::Scream)
        animState = EnemyCharacter::State::Scream;

    // Footsteps are paced by the distance actually travelled this frame (after path and collider),
    // like the player's, and follow the animation shown (animState) rather than AIState: patrol and
    // Search show the Walk clip and should step too.
    const bool footstepIsRun = (animState == EnemyCharacter::State::Run);
    const bool footstepIsWalking = footstepIsRun || (animState == EnemyCharacter::State::Walk);

    if (footstepIsWalking)
    {
        const glm::vec2 deltaXZ(
            m_position.x - footstepStartPos.x,
            m_position.z - footstepStartPos.z
        );
        const float movedDistance = glm::length(deltaXZ);

        if (movedDistance > 0.00001f)
        {
            const bool modeChanged =
                !m_footstepWasMoving ||
                (footstepIsRun != m_footstepWasRunning);

            // Volume is computed once per frame, not per step: the distance cannot change
            // noticeably within a frame, even if several steps fire in a row (fast dash at low
            // FPS).
            const float distToPlayer = glm::length(glm::vec2(playerPos.x - m_position.x, playerPos.z - m_position.z));
            const float footstepVolume = DistanceVolume(distToPlayer, kFootstepAudibleNear, kFootstepAudibleFar);

            // A fresh burst of movement or a Walk<->Run switch (chase launch, after Wall_slam):
            // step immediately so the rhythm restarts there.
            if (modeChanged)
            {
                m_footstepDistance = 0.0f;
                if (footstepIsRun)
                    m_audio.playFootstepRun(footstepVolume);
                else
                    m_audio.playFootstepWalk(footstepVolume);
            }

            m_footstepWasMoving = true;
            m_footstepWasRunning = footstepIsRun;
            m_footstepDistance += movedDistance;

            // A longer, heavier stride than the player's: THE WRAPPED is bigger, so it steps less
            // often.
            const float kEnemyWalkStepLength = 0.75f;
            const float kEnemyRunStepLength = 1.10f;
            const float stepLength = footstepIsRun ? kEnemyRunStepLength : kEnemyWalkStepLength;
            while (m_footstepDistance >= stepLength)
            {
                m_footstepDistance -= stepLength;
                if (footstepIsRun)
                    m_audio.playFootstepRun(footstepVolume);
                else
                    m_audio.playFootstepWalk(footstepVolume);
            }
        }
        else
        {
            // Standing still while the animation still walks (patrol/Search pause): do not
            // accumulate phantom distance, and do not treat it as a full stop for modeChanged, so
            // the next step after a short pause does not sound like a fresh start.
        }
    }
    else
    {
        m_footstepWasMoving = false;
        m_footstepWasRunning = false;
        m_footstepDistance = 0.0f;
    }

    character.setState(animState);

    character.update(deltaTime);
}

bool EnemyAI::consumeJustCaughtPlayer()
{
    const bool result = m_justCaughtPlayer;
    m_justCaughtPlayer = false;
    return result;
}

void EnemyAI::notifyNoiseEvent(const glm::vec3& impactPos)
{
    // Idle only, like regular hearing in update(): Walk/Run/Search already have better information
    // than a noise source.
    if (m_state != AIState::Idle)
        return;

    m_state = AIState::Search;
    m_lastKnownPlayerPos = impactPos; // the noise came from the impact point, not from the player
    m_searchPaused = false;
    m_searchPauseTimer = 0.0f;
    m_path.clear();

    // Unlike regular hearing, no flee guesses: a stone carries no player movement direction. Just
    // the impact point: the enemy goes there, looks around (same pause as Search) and returns to
    // patrol.
    m_searchWaypoints.clear();
    m_searchWaypoints.push_back(impactPos);
    m_searchWaypointIndex = 0;
}

void EnemyAI::forceAggroFromImpact(const glm::vec3& playerPos)
{
    if (m_state == AIState::Attack || m_state == AIState::WallSlam)
        return;

    m_lostTrackTimer = 0.0f;
    m_lastKnownPlayerPos = playerPos;

    // Same transition as an honest fresh detection in update(), without the FOV/LOS/distance/
    // playerInvisible checks: a stone hit counts as unconditional contact. Already Run or Scream
    // needs no change.
    if (m_state != AIState::Run && m_state != AIState::Scream)
    {
        m_state = AIState::Scream;
        m_stateTimer = 1.25f;
        // A direct hit, so full volume instead of a distance-based one (the distance at throw time
        // says nothing about the impact).
        m_audio.playDetected(1.0f);
    }
}

glm::vec3 EnemyAI::resolveWallCollision(const glm::vec3& pos) const
{
    if (!m_map)
        return pos;

    // Enemy body radius for collision (kCollisionRadius, shared with PlayerController). Walls are
    // whole cells, ignoring chamfers. The radius balances visible wall clipping against leaving the
    // player room to squeeze past in a dead end.

    glm::vec3 result = pos;

    // Several passes: pushing out of one wall can move the position into overlap with a neighboring
    // one (typical at inner corners). Three are enough in practice.
    for (int iter = 0; iter < 3; ++iter)
    {
        bool anyPush = false;

        const int cx = (int)std::floor(result.x);
        const int cz = (int)std::floor(result.z);

        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int x = cx + dx;
                const int z = cz + dz;
                if (x < 0 || z < 0 || x >= m_mapW || z >= m_mapH)
                    continue;
                if ((*m_map)[(size_t)z * m_mapW + x] != 1)
                    continue;

                // Closest point on the wall cell's square to the circle center (same circle-vs-AABB
                // trick as SquareOverlapsCircle in PlayerController.cpp).
                const float closestX = glm::clamp(result.x, (float)x, (float)x + 1.0f);
                const float closestZ = glm::clamp(result.z, (float)z, (float)z + 1.0f);
                const float dxp = result.x - closestX;
                const float dzp = result.z - closestZ;
                const float distSq = dxp * dxp + dzp * dzp;

                if (distSq >= kCollisionRadius * kCollisionRadius)
                    continue;

                float dist = std::sqrt(distSq);
                glm::vec2 pushDir;
                if (dist > 1e-5f)
                {
                    pushDir = glm::vec2(dxp, dzp) / dist;
                }
                else
                {
                    // Circle center exactly on the cell boundary or inside it (dist == 0):
                    // closest-point gives no direction, so push away from the wall cell's center.
                    // Otherwise pushDir would be zero and the enemy would stay inside the wall.
                    const glm::vec2 fromCellCenter(result.x - (x + 0.5f), result.z - (z + 0.5f));
                    const float fromCellCenterLen = glm::length(fromCellCenter);
                    pushDir = fromCellCenterLen > 1e-5f
                        ? fromCellCenter / fromCellCenterLen
                        : glm::vec2(1.0f, 0.0f);
                    dist = 0.0f;
                }

                const float penetration = kCollisionRadius - dist;
                result.x += pushDir.x * penetration;
                result.z += pushDir.y * penetration;
                anyPush = true;
            }
        }

        if (m_columnCentersXZ)
        {
            for (const glm::vec2& col : *m_columnCentersXZ)
            {
                const glm::vec2 d(result.x - col.x, result.z - col.y);
                const float dist = glm::length(d);
                const float minDist = kCollisionRadius + m_columnRadius;
                if (dist >= minDist)
                    continue;

                const glm::vec2 pushDir = dist > 1e-5f ? d / dist : glm::vec2(1.0f, 0.0f);
                const float penetration = minDist - dist;
                result.x += pushDir.x * penetration;
                result.z += pushDir.y * penetration;
                anyPush = true;
            }
        }

        // The player is also a circular obstacle: without pushing the enemy out, a chasing enemy
        // standing on a player pinned against a wall left no free position. m_lastPlayerPos is
        // updated at the start of update(), so it is current here.
        {
            const glm::vec2 playerXZ(m_lastPlayerPos.x, m_lastPlayerPos.z);
            const glm::vec2 d(result.x - playerXZ.x, result.z - playerXZ.y);
            const float dist = glm::length(d);
            const float minDist = kCollisionRadius + PlayerController::kCollisionRadius;
            if (dist < minDist)
            {
                const glm::vec2 pushDir = dist > 1e-5f ? d / dist : glm::vec2(1.0f, 0.0f);
                const float penetration = minDist - dist;
                result.x += pushDir.x * penetration;
                result.z += pushDir.y * penetration;
                anyPush = true;
            }
        }

        if (!anyPush)
            break;
    }

    return result;
}
