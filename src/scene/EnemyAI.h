#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <random>
#include "EnemyCharacter.h"
#include "WallShapes.h"
#include "audio/EnemyAudio.h"

// Enemy AI:
// perception (sight with line of sight, hearing)
// and the state machine Idle patrol -> Walk/Run chase -> Search, plus Scream, Dash, Attack and Wall_slam
// Paths are grid BFS, smoothed where line of sight allows
class EnemyAI {
public:
    // Shared with PlayerController;
    // both radii together must leave room to squeeze past in a 1-unit corridor
    static constexpr float kCollisionRadius = 0.30f;

    // Stores pointers: the map is immutable after generation
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

    // Minimap marker alpha:
    // the player's memory of having seen this enemy
    // Fades out over the end
    // of kSpottedMemoryDuration
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

    // True once, on the frame the enemy catches the player
    bool consumeJustCaughtPlayer();

    // playerInvisible (dev) blinds and deafens the enemy playerCanSeeThisEnemy only feeds the minimap playerLightLevel scales the sight radius
    void update(float deltaTime, const glm::vec3& playerPos, bool playerIsRunning, bool playerIsMoving, bool playerInvisible, bool playerCanSeeThisEnemy, float playerLightLevel, EnemyCharacter& character);

    // A stone landing nearby: an Idle enemy goes to check the impact point
    void notifyNoiseEvent(const glm::vec3& impactPos);

    // A direct stone hit: unconditional chase
    // Attack and Wall_slam are not interrupted
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

    // m_playerHeading is unit length and cannot show standing still, so real speed is tracked separately
    float m_timeSinceMeaningfulMovement = 0.0f;

    glm::vec3 m_lastKnownPlayerPos{ 0.0f };
    float m_lostTrackTimer = 0.0f;

    glm::vec3 m_patrolTarget{ 0.0f };
    bool m_hasPatrolTarget = false;
    bool m_patrolPaused = false;
    float m_patrolPauseTimer = 0.0f;

    // Search state, separate from patrol:
    // it has a specific target and ends by returning to patrol

    bool m_searchPaused = false;
    float m_searchPauseTimer = 0.0f;

    // Points to check in order: the last known position, then up to two guessed continuations
    std::vector<glm::vec3> m_searchWaypoints;
    size_t m_searchWaypointIndex = 0;

    std::mt19937 m_rng{ std::random_device{}() };
    float m_dashRollTimer = 0.0f;
    glm::vec3 m_dashDirection{ 0.0f };
    float m_dashElapsed = 0.0f;

    // Attack_Lunge / Wall_slam / Scream timers copy the clip durations;
    // update them with the animations
    float m_stateTimer = 0.0f;

    // Grace after an attack so the stunned player is not caught again next frame
    float m_attackCooldownTimer = 0.0f;

    float m_perceptionTimer = 0.0f;

    std::vector<glm::vec2> m_path;
    size_t m_pathWaypointIndex = 0;
    float m_pathRecomputeTimer = 0.0f;

    // Moves along m_path toward target, recomputing the path periodically;
    // true on arrival. Used by patrol and chase
    bool followPath(const glm::vec3& target, float speed, float deltaTime);
    glm::ivec2 nearestWalkableCell(const glm::vec3& pos) const;

    bool isWalkableCell(int x, int z) const;
    glm::vec3 pickRandomPatrolPoint();

    // Appends up to two guesses of where the player fled;
    // none if the player was standing still
    void appendGuessedFleeWaypoints(const glm::vec3& lastSeenPos, const glm::vec3& headingAtLoss, bool headingWasFresh, std::vector<glm::vec3>& outWaypoints) const;

    // Pushes the position out of walls every frame
    // Uses full cell squares, ignoring chamfers
    glm::vec3 resolveWallCollision(const glm::vec3& pos) const;

    int m_mapW = 0, m_mapH = 0;
    const std::vector<int>* m_map = nullptr;
    const std::vector<WallShapes::CornerCut>* m_cornerCuts = nullptr;
    const std::vector<unsigned char>* m_diagonalChainMask = nullptr;
    const std::vector<glm::vec2>* m_columnCentersXZ = nullptr;    float m_columnRadius = 0.0f;

    // m_map with columns marked as walls; columns are floor cells with a circular collider
    const std::vector<int>* m_pathfindingMap = nullptr;

    // Anti-stuck: real progress over kStuckCheckInterval
    float m_stuckCheckTimer = 0.0f;
    glm::vec3 m_stuckCheckRefPos{ 0.0f };
    bool m_stuckCheckRefInit = false;

    EnemyAudio m_audio;

    // Occasional moans while patrolling or searching, at randomized intervals
    float m_moanTimer = 0.0f;
    float m_nextMoanInterval = 10.0f; // overridden by a random value in init()/the first tick

    // Footsteps follow the distance moved and the clip shown
    float m_footstepDistance = 0.0f;
    bool m_footstepWasMoving = false;
    bool m_footstepWasRunning = false;
};
