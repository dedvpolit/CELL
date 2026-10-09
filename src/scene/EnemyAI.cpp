#include "EnemyAI.h"
#include "LineOfSight.h"
#include "GridPathfinding.h"
#include "PlayerController.h"
#include <algorithm>
#include <cmath>

// He is a dummy. but really cool dummy

namespace {
// Shortest signed angle in (-180, 180]
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

// How far, in cells, to extrapolate the player's last direction when guessing where they fled
constexpr int kFleeGuessMaxCells = 5;
constexpr float kFleeGuessMeaningfulSpeed = 0.3f; // units/sec: below this it is jitter, not walking. Seconds of stillness that still count as moving.
constexpr float kFleeGuessMovementMemory = 0.4f;
// Depth of a side-turn guess; 1 cell is the turn itself
constexpr int kFleeGuessBranchCells = 2;

// Full volume up to nearRadius, silent past farRadius, squared ease-out in between
float DistanceVolume(float distance, float nearRadius, float farRadius)
{
    if (distance <= nearRadius) return 1.0f;
    if (distance >= farRadius) return 0.0f;
    const float t = (distance - nearRadius) / (farRadius - nearRadius);
    return 1.0f - t * t;
}

// The detection scream carries farther: it is a warning
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

    // init() runs for every map; clear the flag or the enemy appears on the minimap before the first encounter
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

    // Mostly walls, so a random pick can miss; give up after a few tries and retry next call
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

    // Corridors are axis-aligned and a diagonal heading is usually noise: guess along the dominant  axis
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

        // The player may have turned at the nearest fork: also try side branches. The first hit is enough
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
                    // A guess one cell into the turn would almost coincide with the straight line
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

glm::ivec2 EnemyAI::nearestWalkableCell(const glm::vec3& pos) const
{
    const glm::ivec2 cell((int)std::floor(pos.x), (int)std::floor(pos.z));
    if (isWalkableCell(cell.x, cell.y))
        return cell;

    // A column cell is blocked for pathfinding but only its center is solid for the collider, so
    // a body (or the player) can stand in its corner. Path from the closest open neighbor instead
    glm::ivec2 best = cell;
    float bestDistSq = 1e9f;
    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            const glm::ivec2 n = cell + glm::ivec2(dx, dz);
            if ((dx == 0 && dz == 0) || !isWalkableCell(n.x, n.y))
                continue;
            const float ox = n.x + 0.5f - pos.x;
            const float oz = n.y + 0.5f - pos.z;
            const float distSq = ox * ox + oz * oz;
            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                best = n;
            }
        }
    }
    return best;
}

bool EnemyAI::followPath(const glm::vec3& target, float speed, float deltaTime)
{
    const float kPathRecomputeInterval = 0.4f;
    m_pathRecomputeTimer += deltaTime;

    if (m_pathRecomputeTimer >= kPathRecomputeInterval || m_path.empty())
    {
        m_pathRecomputeTimer = 0.0f;

        const glm::ivec2 startCell = nearestWalkableCell(m_position);
        const glm::ivec2 goalCell = nearestWalkableCell(target);

        std::vector<glm::ivec2> cellPath;
        // m_pathfindingMap also blocks column cells
        if (m_pathfindingMap && !m_pathfindingMap->empty())
            cellPath = GridPathfinding::FindPath(m_mapW, m_mapH, *m_pathfindingMap, startCell, goalCell);

        // BFS picks a staircase among equal paths; SmoothPath() straightens it where line of sight allows
        if (m_pathfindingMap && !m_pathfindingMap->empty())
            cellPath = GridPathfinding::SmoothPath(m_mapW, m_mapH, *m_pathfindingMap, cellPath);

        m_path.clear();
        for (const glm::ivec2& c : cellPath)
            m_path.push_back(glm::vec2(c.x + 0.5f, c.y + 0.5f));
        m_pathWaypointIndex = 0;

        // The first point is the current cell's center and may lie slightly behind the enemy;
        // skip it when close, or every recompute starts with a step back
        if (m_path.size() > 1)
        {
            const float distToFirst = glm::length(m_path[0] - glm::vec2(m_position.x, m_position.z));
            if (distToFirst < 0.5f)
                m_pathWaypointIndex = 1;
        }
    }

    bool reached = false;
    if (m_pathWaypointIndex >= m_path.size())
    {
        // An empty path is "nowhere to go", not "reached"; the stuck check below still runs
        reached = !m_path.empty();
    }
    else
    {
        glm::vec2 targetXZ = m_path[m_pathWaypointIndex];
        glm::vec3 toTarget(targetXZ.x - m_position.x, 0.0f, targetXZ.y - m_position.z);
        float dist = glm::length(toTarget);

        const float kWaypointReachedRadius = 0.25f;
        if (dist < kWaypointReachedRadius)
        {
            if (m_pathWaypointIndex + 1 < m_path.size())
            {
                m_pathWaypointIndex++;
                targetXZ = m_path[m_pathWaypointIndex];
                toTarget = glm::vec3(targetXZ.x - m_position.x, 0.0f, targetXZ.y - m_position.z);
                dist = glm::length(toTarget);
            }
            else
            {
                reached = true;
            }
        }

        if (!reached && dist > 0.01f)
        {
            const glm::vec3 dir = toTarget / dist;
            const float moveDist = std::min(dist, speed * deltaTime);
            m_position += dir * moveDist;

            // Assumes the model faces local -Z (Blender -> glTF)
            // Add a fixed offset here if it faces the wrong way
            const float desiredYawDegrees = glm::degrees(std::atan2(dir.x, dir.z));

            // Capped turn rate: path recomputes can change the direction by tens of degrees in one frame
            const float kTurnRateDegPerSec = 225.0f; // deg/sec (~0.8 s for a 180 deg turn)
            const float maxTurnDelta = kTurnRateDegPerSec * deltaTime;
            float turnDelta = ShortestAngleDeltaDeg(m_yawDegrees, desiredYawDegrees);
            turnDelta = glm::clamp(turnDelta, -maxTurnDelta, maxTurnDelta);
            m_yawDegrees = NormalizeAngleDeg(m_yawDegrees + turnDelta);
        }
    }

    // Stuck safeguard: if the body barely moved over a check interval, nudge it aside and recompute the path
    // A generic escape from geometric traps where the circular collider catches a neighboring cell
    if (reached)
    {
        m_stuckCheckRefInit = false;
        m_stuckCheckTimer = 0.0f;
        return true;
    }
    {
        const float kStuckCheckInterval = 0.5f;
        // Unimpeded, the body covers ~0.275 per interval; 0.15 leaves room for normal slowdowns
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
                // Random direction, short distance, collision-resolved
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
    // Matched to the walk clip's stride; faster makes the legs skate
    const float kEnemyWalkSpeed = 0.55f;
    const float kEnemyRunSpeed = 2.0f;
    const float kDashSpeedMultiplier = 1.5f;
    const float kCatchRadius = 0.6f;
    const float kLoseTrackGrace = 5.0f; // sec without contact before the chase turns into a search

    // Player speed estimate for the dash lead, same as PlayerController's walk/run speeds
    const float kEstimatedPlayerSpeed = playerIsRunning ? 3.0f : 1.5f;

    // Footsteps are paced by the distance actually moved this frame
    // Attack/WallSlam/Scream return before it is used
    const glm::vec3 footstepStartPos = m_position;

    if (m_lastPlayerPosInit)
    {
        glm::vec3 delta = playerPos - m_lastPlayerPos;
        delta.y = 0.0f;
        const float len = glm::length(delta);
        if (len > 0.001f)
            m_playerHeading = delta / len;

        // The stillness timer resets on real movement;
        // the grace window keeps one still frame from erasing it
        const float speed = (deltaTime > 0.0001f) ? (len / deltaTime) : 0.0f;
        if (speed >= kFleeGuessMeaningfulSpeed)
            m_timeSinceMeaningfulMovement = 0.0f;
        else
            m_timeSinceMeaningfulMovement += deltaTime;
    }
    m_lastPlayerPos = playerPos;
    m_lastPlayerPosInit = true;

    // Minimap visibility is decided from the player's side (DungeonScene::isEnemyVisibleToPlayer()), independently of AI perception
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

        // The dash stops at a wall on the impact frame;
        // resolve so the enemy does not stay embedded during WallSlam.
        m_position = resolveWallCollision(m_position);
        character.setPosition(m_position);
        character.setYawDegrees(m_yawDegrees);
        character.setState(EnemyCharacter::State::WallSlam);
        character.update(deltaTime);
        return;
    }

    // mewo-meow-meow
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

            // Darkness shrinks the sight radius but never to zero:
            // it hides the player without guaranteeing stealth
            const float kMinSightMultiplier = 0.35f;
            const float sightMultiplier = glm::mix(kMinSightMultiplier, 1.0f,
                                                    glm::clamp(playerLightLevel, 0.0f, 1.0f));
            const float sightRadius = baseSightRadius * sightMultiplier;

            bool canSee = dist <= sightRadius;

            // FOV cone around the current yaw; within kPeripheralRadius the player is noticed regardless
            const float kFOVHalfAngleDeg = 55.0f; // ~110 deg full cone
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

            // Dev invisibility (I) is applied after the real checks, so everything else sees a normal loss of sight. It mutes hearing too
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
                // Lost sight during a chase: keep a memory window instead of dropping it instantly
                if (m_state == AIState::Run || m_state == AIState::Walk)
                {
                    m_lostTrackTimer += elapsed;
                    if (m_lostTrackTimer >= kLoseTrackGrace)
                    {
                        // Search the last seen position before returning to patrol
                        m_state = AIState::Search;
                        m_lostTrackTimer = 0.0f;
                        m_searchPaused = false;
                        m_searchPauseTimer = 0.0f;
                        m_path.clear();

                        // Waypoints: last seen position, then a guessed continuation of the player's route (and a side turn if found)
                        m_searchWaypoints.clear();
                        m_searchWaypoints.push_back(m_lastKnownPlayerPos);
                        const bool headingWasFresh = m_timeSinceMeaningfulMovement <= kFleeGuessMovementMemory;
                        appendGuessedFleeWaypoints(m_lastKnownPlayerPos, m_playerHeading, headingWasFresh, m_searchWaypoints);
                        m_searchWaypointIndex = 0;
                    }
                }
                // No else: Idle is not chasing and Search ends itself
                // An else { Idle } would cancel Search on every tick

                // Hearing ignores walls and FOV and works only while not chasing
                // It leads to Search, never straight to a chase
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

                        // Same guesses as after a lost chase if the player was moving
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

            // Aim at a point between the player's lead-predicted and current positions
            // so a sudden stop or turn does not make the dash overshoot
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

            // Do not launch straight into a wall
            // The course is fixed for the whole dash, so later wall hits stay possible
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
                // More likely up close, where a dash pays off
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

    // Why its a dummy:
    if (m_state == AIState::Dash)
    {
        m_dashElapsed += deltaTime;

        const float dashSpeed = kEnemyRunSpeed * kDashSpeedMultiplier;
        const glm::vec3 nextPos = m_position + m_dashDirection * dashSpeed * deltaTime;

        // A head-on hit shows up as the collider pushing nextPos back against the dash direction
        // A "next cell is a wall" test would never fire because the collider stops the enemy first
        // Sliding along a wall is not a hit
        const glm::vec3 resolvedNextPos = resolveWallCollision(nextPos);
        glm::vec3 pushback = resolvedNextPos - nextPos;
        pushback.y = 0.0f;
        // Slack so sliding along a wall is not a slam
        const float kWallSlamPushbackThreshold = 0.02f;
        bool blocked = -glm::dot(pushback, m_dashDirection) > kWallSlamPushbackThreshold;

        // Safety net: the map has a wall border, so this should not happen
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
            // Snap once at launch: the direction is locked for the dash, and the instant turn reads as a lunge
            m_yawDegrees = glm::degrees(std::atan2(m_dashDirection.x, m_dashDirection.z));
        }
    }
    else if (m_state == AIState::Walk || m_state == AIState::Run)
    {
        if (m_attackCooldownTimer > 0.0f)
            m_attackCooldownTimer -= deltaTime;

        // Catching uses the real position, so memory alone cannot catch the player
        // The attack cooldown prevents instant re-hits
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
            // The last known position tracks the player while visible and freezes at the last sighting during the memory window
            const float speed = (m_state == AIState::Run) ? kEnemyRunSpeed : kEnemyWalkSpeed;
            followPath(m_lastKnownPlayerPos, speed, deltaTime);
        }
    }
    else if (m_state == AIState::Search)
    {
        // Waypoints in order:
        // noise source or last sighting, then up to two guesses. An empty list returns to patrol
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

                // The last point gets the long look-around; intermediate guesses only a glance
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

    // Every movement state passes through the collider here
    // Attack/WallSlam/Scream resolve it in their own early returns
    m_position = resolveWallCollision(m_position);
    character.setPosition(m_position);
    character.setYawDegrees(m_yawDegrees);
    if (m_state == AIState::Idle || m_state == AIState::Search)
    {
        // Moans only while patrolling or searching; a chase already starts with a scream
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
    // No calm-walk clip: patrol uses Walk, and Idle while paused
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

    // Steps follow the distance actually moved and the clip shown, not the AI state: patrol and search show Walk too
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

            // Once per frame: the distance does not change noticeably between steps
            const float distToPlayer = glm::length(glm::vec2(playerPos.x - m_position.x, playerPos.z - m_position.z));
            const float footstepVolume = DistanceVolume(distToPlayer, kFootstepAudibleNear, kFootstepAudibleFar);

            // Movement just started or switched gait: step now so the rhythm restarts
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

            // Longer stride than the player's: THE WRAPPED is bigger
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
            // Standing while the clip still walks:
            // accumulate nothing, but do not treat it as a full stop, or the next step sounds like a fresh start
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
    // Idle only; other states already have better information than a noise
    if (m_state != AIState::Idle)
        return;

    m_state = AIState::Search;
    m_lastKnownPlayerPos = impactPos; // the noise came from the impact point, not from the player
    m_searchPaused = false;
    m_searchPauseTimer = 0.0f;
    m_path.clear();

    // A stone says nothing about where the player went: just check the impact point
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

    // A hit counts as contact regardless of vision; same transition as a fresh detection
    if (m_state != AIState::Run && m_state != AIState::Scream)
    {
        m_state = AIState::Scream;
        m_stateTimer = 1.25f;
        // Full volume: the throw distance says nothing about where the hit happened
        m_audio.playDetected(1.0f);
    }
}

glm::vec3 EnemyAI::resolveWallCollision(const glm::vec3& pos) const
{
    if (!m_map)
        return pos;

    // Walls are whole cells, chamfers ignored
    // The radius trades visible wall clipping against leaving the player room to slip past

    glm::vec3 result = pos;

    // Pushing out of one wall can push into another at inner corners; three passes suffice
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

                // Closest point of the cell square to the circle center
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
                    // Center exactly on or inside the cell: no direction from the closest point, so push away from the cell center
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

        // The player is an obstacle too;
        // otherwise a player pinned against a wall leaves the enemy no free position. m_lastPlayerPos is current here.
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
