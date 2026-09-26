#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <random>
#include "EnemyCharacter.h"
#include "WallShapes.h"
#include "audio/EnemyAudio.h"

// EnemyAI: perception (distance + line of sight via LineOfSight::HasLineOfSight, plus hearing)
// and the state machine Idle patrol -> Walk/Run chase -> Search, with Scream, Dash, Attack and
// Wall_slam. Paths are a BFS over the map grid (GridPathfinding.h).
//
// - Sight: FOV cone (kFOVHalfAngleDeg), radius scaled by PlayerController::lightLevel() but
//   never to zero (kMinSightMultiplier). Point-blank contact is always noticed.
// - Hearing: player noise is heard through walls in Idle only; it sends the enemy to Search,
//   never straight into a chase.
// - Losing the player: keeps heading to the last known position for kLoseTrackGrace, then
//   Searches (a guessed continuation of the route from m_playerHeading) and returns to patrol.
// - Dash: Run sub-state at 1.5x speed, direction locked at launch, no pathfinding; ends in
//   Attack_Lunge or Wall_slam.
// - A cylindrical collider pushes the position out of walls in every state, every frame.
class EnemyAI {
public:
    // Shared with PlayerController so the player cannot walk through an enemy. The sum of both
    // radii must leave room to squeeze past in a 1-unit corridor.
    static constexpr float kCollisionRadius = 0.30f;

    // Stores pointers, not copies: the map is immutable after generation. pathfindingMap is built
    // once in DungeonScene and shared by all enemies.
    void init(
        int mapW, int mapH,
        const std::vector<int>* map,
        const std::vector<WallShapes::CornerCut>* cornerCuts,
        const std::vector<unsigned char>* diagonalChainMask,
        const std::vector<glm::vec2>* columnCentersXZ,
        float columnRadius,
        const std::vector<int>* pathfindingMap);

    void setPosition(const glm::vec3& pos) { m_position = pos; }
    glm::vec3 position() const { return m_position; }

    // Minimap marker alpha (0..1): the player's memory of having seen this enemy (FOV + LOS from
    // the player's camera), independent of the AI's own perception. Full until kSpottedFadeDuration
    // before kSpottedMemoryDuration expires, then a linear fade. Not saved: enemies restart fresh
    // after CONTINUE.
    float spottedMarkerAlpha() const
    {
        if (!m_hasBeenSpotted)
            return 0.0f;
        const float remaining = kSpottedMemoryDuration - m_timeSinceLastSpotted;
        if (remaining >= kSpottedFadeDuration)
            return 1.0f;
        if (remaining <= 0.0f)
            return 0.0f;
        return remaining / kSpottedFadeDuration;
    }

    // true exactly once, on the frame the enemy caught the player (the transition into Attack). The
    // caller reads it and applies the debuff immediately; it does not repeat (a single-shot pulse,
    // reset right after being read).
    bool consumeJustCaughtPlayer();

    // playerInvisible (dev tools) forces canSee = false and mutes hearing. playerCanSeeThisEnemy
    // only drives spottedMarkerAlpha() (the minimap), not the AI. playerLightLevel narrows the
    // sight radius, not the FOV or hearing.
    void update(float deltaTime, const glm::vec3& playerPos, bool playerIsRunning, bool playerIsMoving, bool playerInvisible, bool playerCanSeeThisEnemy, float playerLightLevel, EnemyCharacter& character);

    // A stone landing nearby: only an Idle enemy reacts, and it Searches toward the impact point
    // (the distraction).
    void notifyNoiseEvent(const glm::vec3& impactPos);

    // A direct stone hit: unconditional chase (Scream -> Run), ignoring FOV/LOS/invisibility. An
    // enemy already in Attack/Wall_slam is not interrupted.
    void forceAggroFromImpact(const glm::vec3& playerPos);

private:
    enum class AIState { Idle, Walk, Run, Dash, Attack, WallSlam, Scream, Search };
    AIState m_state = AIState::Idle;
    bool m_justCaughtPlayer = false;

    bool m_hasBeenSpotted = false;
    float m_timeSinceLastSpotted = 0.0f; // accumulates while the enemy is NOT visible
    static constexpr float kSpottedMemoryDuration = 20.0f;
    static constexpr float kSpottedFadeDuration = 3.0f;

    glm::vec3 m_position{ 0.0f };
    float m_yawDegrees = 0.0f;

    glm::vec3 m_lastPlayerPos{ 0.0f };
    bool m_lastPlayerPosInit = false;
    glm::vec3 m_playerHeading{ 1.0f, 0.0f, 0.0f };

    // The player's real speed is tracked separately from m_playerHeading, which is always unit
    // length and so cannot tell "stood still". A short grace window keeps one still frame from
    // erasing recent movement.
    float m_timeSinceMeaningfulMovement = 0.0f;

    glm::vec3 m_lastKnownPlayerPos{ 0.0f };
    float m_lostTrackTimer = 0.0f;

    glm::vec3 m_patrolTarget{ 0.0f };
    bool m_hasPatrolTarget = false;
    bool m_patrolPaused = false;
    float m_patrolPauseTimer = 0.0f;

    // Search: checking the last known position/noise source. These are separate fields from the
    // patrol ones: Search is not a patrol (it has a specific target, a noise/loss point, not a
    // random one), although the pause-at-destination mechanic is the same.
    bool m_searchPaused = false;
    float m_searchPauseTimer = 0.0f;

    // The chain of points to check in order (see "Flee guesses" in the class comment and
    // appendGuessedFleeWaypoints() in the .cpp): [0] is always m_lastKnownPlayerPos at the moment
    // of entering Search, then 0-2 guessed route-continuation/side-turn points.
    std::vector<glm::vec3> m_searchWaypoints;
    size_t m_searchWaypointIndex = 0;

    std::mt19937 m_rng{ std::random_device{}() };
    float m_dashRollTimer = 0.0f;
    glm::vec3 m_dashDirection{ 0.0f };
    float m_dashElapsed = 0.0f;

    // Attack_Lunge / Wall_slam / Scream: the timers are hardcoded copies of the real clip
    // durations; update them if the animations change.
    float m_stateTimer = 0.0f;

    // Grace after an attack: a re-catch inside it does not count, otherwise the stunned player is
    // caught again on the next frame and the animation stutters.
    float m_attackCooldownTimer = 0.0f;

    float m_perceptionTimer = 0.0f;

    std::vector<glm::vec2> m_path;
    size_t m_pathWaypointIndex = 0;
    float m_pathRecomputeTimer = 0.0f;

    // Moves m_position along m_path toward target, recomputing the path periodically, not every
    // frame. Returns true once the target is reached. Shared by patrol and the Walk/Run chase to
    // avoid duplicated logic.
    bool followPath(const glm::vec3& target, float speed, float deltaTime);

    bool isWalkableCell(int x, int z) const;
    glm::vec3 pickRandomPatrolPoint();

    // Guesses where the player ran after being lost (from headingAtLoss): appends 0-2 waypoints
    // after lastSeenPos, which the caller adds first. headingWasFresh = false (the player stood
    // still) adds none.
    void appendGuessedFleeWaypoints(const glm::vec3& lastSeenPos, const glm::vec3& headingAtLoss, bool headingWasFresh, std::vector<glm::vec3>& outWaypoints) const;

    // Cylindrical collider: pushes the position out of walls and columns every frame in every
    // state, so the enemy can never end up inside geometry. It tests the full cell square and
    // ignores chamfered corners (a deliberate simplification; chamfers are exact only for the
    // player).
    glm::vec3 resolveWallCollision(const glm::vec3& pos) const;

    int m_mapW = 0, m_mapH = 0;
    const std::vector<int>* m_map = nullptr;
    const std::vector<WallShapes::CornerCut>* m_cornerCuts = nullptr;
    const std::vector<unsigned char>* m_diagonalChainMask = nullptr;
    const std::vector<glm::vec2>* m_columnCentersXZ = nullptr;    float m_columnRadius = 0.0f;

    // Path map: like m_map but with column cells marked as walls. Columns are floor in m_map (a
    // separate circular collider), so pathfinding would route into their invisible shell and get
    // stuck.
    const std::vector<int>* m_pathfindingMap = nullptr;

    // Anti-stuck last resort: tracks real progress over kStuckCheckInterval regardless of the
    // cause.
    float m_stuckCheckTimer = 0.0f;
    glm::vec3 m_stuckCheckRefPos{ 0.0f };
    bool m_stuckCheckRefInit = false;

    EnemyAudio m_audio;

    // Moans: rare, only while the enemy wanders/searches (Idle patrol or Search), not during a
    // chase (Scream/chase sounds already play there). The interval is randomized fresh each time:
    // occasional, not a regular tick.
    float m_moanTimer = 0.0f;
    float m_nextMoanInterval = 10.0f; // overridden by a random value in init()/the first tick

    // Footsteps are paced by the distance actually travelled and follow the animation shown
    // (Walk/Run; patrol and Search reuse Walk_Nervous), not the AIState.
    float m_footstepDistance = 0.0f;
    bool m_footstepWasMoving = false;
    bool m_footstepWasRunning = false;
};
