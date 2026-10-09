#pragma once
#include <algorithm>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <array>
#include <cstdint>
#include "audio/FootstepAudio.h"
#include "audio/GameplayMusic.h"
#include "WallTexture.h"
#include "MinimapFog.h"
#include "DebugMapOverlay.h"
#include "Compass.h"
#include "Culling.h"
#include "SceneGeometry.h"
#include "PlayerTorchViewmodel.h"
#include "EnemyCharacter.h"
#include "EnemyAI.h"
#include "WallShapes.h"
#include "Columns.h"
#include "Zoning.h"
#include "Diaries.h"
#include "CorridorWidth.h"
#include "DiagonalCorridors.h"
#include "Difficulty.h"
#include "Lighting.h"
#include "TorchShadowMap.h"
#include "PlayerController.h"
#include "audio/EnemyAudio.h"

// Optional dev tools: without DevTools.h the dev-key blocks compile out
#if __has_include("dev/DevTools.h")
#include "dev/DevTools.h"
#endif
#ifdef DEV_TOOLS_ACTIVE
#define HAS_DEV_TOOLS 1
#endif

class DungeonScene {
public:
    bool init();
    void shutdown();

    void processInput(GLFWwindow* window, float deltaTime);

    // Diary reading screen or journal open (an overlay on top of PLAYING)
    bool isReadingOverlayOpen() const { return m_openDiaryIndex != -1 || m_journalOpen; }

    // Gates the exit door and drives the "need N more" message
    int diariesReadCount() const {
        int n = 0;
        for (bool read : m_diariesRead) if (read) ++n;
        return n;
    }

    // Briefly true after pressing E at the exit door without enough diaries read
    bool showWinBlockedMessage() const { return m_winBlockedMessageTimer > 0.0f; }

    // Untaken wall torch within E range, -1 if none
    int nearbyWallTorchIndex() const { return m_nearbyWallTorchIndex; }

    bool showTorchEmptyMessage() const { return m_torchEmptyMessageTimer > 0.0f; }

    int playerTorchInventoryCount() const { return m_player.torchInventoryCount(); }
    int playerStoneCount() const { return m_player.stoneCount(); }

    // "Unreliable vision": anomalies that grow with unpaused play time
    // 90 s: flickering wall glyph;
    // 180 s: a torch trembles;
    // 300 s: deceptive sounds;
    // 420 s: flickering enemy silhouette
    // and more frequent effects
    int glitchStage() const
    {
        const float t = m_glitchElapsedTime;
        if (t >= 420.0f) return 4;
        if (t >= 300.0f) return 3;
        if (t >= 180.0f) return 2;
        if (t >= 90.0f)  return 1;
        return 0;
    }

    // Effect 1 lives in the ASCII post-process owned by Application, hence the public getters
    bool glyphGlitchActive() const { return m_glyphGlitchActiveTimer > 0.0f; }
    glm::vec2 glyphGlitchUV() const { return m_glyphGlitchUV; }
    float glyphGlitchRadiusCells() const { return m_glyphGlitchRadiusCells; }

    // Diary within E range, -1 if none
    int nearbyDiaryIndex() const { return m_nearbyDiaryIndex; }

    // Escape: closes the diary first, then the journal
    void closeDiaryOrJournal() {
        if (m_openDiaryIndex != -1) m_openDiaryIndex = -1;
        else m_journalOpen = false;
    }

    // Called instead of processInput() while isReadingOverlayOpen():
    // E, Tab and list navigation
    void tickReadingOverlayInput(GLFWwindow* window);

    // Frame of the diary or journal for ascii.setUIOverlay();
    // false if nothing is open
    bool buildReadingOverlayGrid(std::vector<unsigned char>& grid, int cols, int rows) const;

    // Frame bounds in menu-grid cells, for placing TextRenderer text inside it
    void getReadingBoxBounds(int cols, int rows, int& x0, int& y0, int& x1, int& y1) const;

    const Diaries::PlacedDiary* openDiary() const {
        return (m_openDiaryIndex != -1 && m_openDiaryIndex < (int)m_diaries.size())
            ? &m_diaries[(size_t)m_openDiaryIndex] : nullptr;
    }

    bool isJournalListMode() const { return m_journalOpen && m_openDiaryIndex == -1; }

    int diariesTotalCount() const { return (int)m_diaries.size(); }
    // Journal rows follow read order: row i is the i-th diary read
    bool journalEntryRead(int row) const { return row >= 0 && row < (int)m_diaryReadOrder.size(); }
    int journalSelectedIndex() const { return m_journalSelectedIndex; }

    void processMouse(double xpos, double ypos);

    // Resets only the mouse-look baseline -< re-capturing the cursor after a pause does not produce a huge jump
    void resetMouseLook() { m_player.resetMouseLook(); }

    void tickPauseCameraIdle(float deltaTime);

    // Called every frame, separately from processInput():
    // the diary-read volume duck must keep updating while the reading overlay is open
    void tickAmbientMusic(float deltaTime, bool active)
    {
        m_ambientMusic.update(deltaTime, active, diariesReadCount());
    }

    // gameplayActive = false freezes the AI (pause, menu)
    void render(int viewportWidth, int viewportHeight, bool gameplayActive);

    // Call after AsciiEffect::end() so the post-process does not touch the compass
    void renderCompassOverlay(int viewportWidth, int viewportHeight);

    // Debug map (M): the whole unfogged maze. Call after AsciiEffect::end()
    void renderDebugMap(int viewportWidth, int viewportHeight);

    // The compass has its own shader, so it gets the color flag separately
    void setColorEnabled(bool enabled) { m_colorEnabled = enabled; }

    // What render() submitted last frame, for the [perf] log
    int getLastVisibleTriangles() const { return m_lastVisibleTriangles; }
    int getLastVisibleParticles() const { return m_lastVisibleParticles; }

    int getLastActiveTorchCount() const { return m_lastActiveTorchCount; }
    int getLastVisibleChunks() const { return m_lastVisibleChunkCount; }

    static const int MAX_TORCHES = 1024; // flame/particle ids in scene.vert are sized for this
    // Nearest torches lit per frame; each costs one baked shadow lookup per lit pixel
    static const int MAX_ACTIVE_TORCHES = 20;

    float getStaminaFraction() const { return m_player.staminaFraction(); }

    float getHealthFraction() const { return m_player.healthFraction(); }

    // Debug noclip (N)
    bool isNoclipEnabled() const { return m_player.noclipEnabled(); }

    // 1.0 for a regular player; +/- change it in noclip
    float getViewDistanceMultiplier() const {
#ifdef HAS_DEV_TOOLS
        return DevTools::GetViewDistanceMultiplier();
#else
        return 1.0f;
#endif
    }

    // Projection planes; AsciiEffect needs them to linearize depth
    float nearPlane() const { return 0.05f; }
    float farPlane() const { return std::max(50.0f, m_currentRenderDistance + 10.0f); }

    void setCompassUiFont(
        GLuint texture,
        int glyphCount
    );

    bool hasWon() const { return m_player.hasWon(); }
    bool isDying() const { return m_player.isDying(); }
    bool consumeDeathFadeTrigger() { return m_player.consumeDeathFadeTrigger(); }
    void tickDeathFade(float deltaTime) { m_player.tickDeathFade(deltaTime); }

    // Mouse sensitivity range for the settings slider
    static constexpr float kMinMouseSensitivity = PlayerController::kMinMouseSensitivity;
    static constexpr float kMaxMouseSensitivity = PlayerController::kMaxMouseSensitivity;

    float getMouseSensitivity() const { return m_player.mouseSensitivity(); }
    void setMouseSensitivity(float sensitivity) { m_player.setMouseSensitivity(sensitivity); }

    void generateMenuBackgroundMaze();

    // Fresh random maze and full player reset, saved immediately so CONTINUE sees it
    // explicitSlot < 0 picks a slot automatically with an empty name (death path)
    void newGame(int explicitSlot = -1, const std::string& explicitName = std::string());

    // Rebuilds the maze from the saved seed, then applies the saved player state and fog of war
    // False if the slot is empty or corrupted
    bool loadSlot(int slotIndex);

    // Applies to the next newGame(); loadSlot() takes it from the save
    void setDifficulty(Difficulty difficulty) { m_difficulty = difficulty; }
    Difficulty difficulty() const { return m_difficulty; }
    int diariesToWin() const { return m_player.diariesToWin(); }

    // Autosave into the active slot; no-op without one
    void saveActiveSlot();

    // Saves under a name and makes the slot active for later autosaves
    void saveToSlot(int slotIndex, const std::string& name) {
        m_activeSlot = slotIndex;
        m_saveName = name;
        saveActiveSlot();
    }

private:

    int m_mapW = 0, m_mapH = 0;
    std::vector<int> m_map;

    // m_map with column cells marked as walls, for enemy pathfinding; shared by pointer
    std::vector<int> m_pathfindingMapWithColumns;

    // Chamfered corners per cell (z*m_mapW+x). Geometry, collision and torch shadows all read this table
    std::vector<WallShapes::CornerCut> m_wallCornerCuts;

    std::vector<float> m_chamferSizes;

    // Column centers in world XZ
    std::vector<glm::vec2> m_columnCentersXZ;

    // Kept only for the debug map
    Zoning::ZoneGrid m_zoneGrid;

    // 1 where corridor widening opened the cell; debug map only
    std::vector<unsigned char> m_corridorWidened;

    // Final wall/floor color per cell, already blended across zone borders
    std::vector<glm::vec3> m_paletteWallColors;
    std::vector<glm::vec3> m_paletteFloorColors;

    // 1 for cells of a diagonal chain; their chamfer is forced. Debug map only afterwards
    std::vector<unsigned char> m_diagonalChainMask;

    // Finish safe zone bounds
    int m_endSafeX0 = 0, m_endSafeZ0 = 0, m_endSafeX1 = 0, m_endSafeZ1 = 0;

    // Pocket centers; every pocket gets torches
    std::vector<glm::ivec2> m_smallSafeZoneCenters;
    int m_smallSafeZoneRadius = 0;

    // One diary per pocket (m_diaries[i] at m_smallSafeZoneCenters[i])
    // Read flags affect only UI and saves
    std::vector<Diaries::PlacedDiary> m_diaries;
    std::vector<bool> m_diariesRead;
    std::vector<int> m_diaryReadOrder;  // diary indices, first read first
    std::array<int, Diaries::kStepCount> m_storyVariants{};
    void markDiaryRead(int diaryIndex);

    // Not reset between runs:
    // its state is harmless and the duck target is recomputed every frame
    GameplayMusic m_ambientMusic;
    int m_nearbyDiaryIndex = -1;
    // Diary being read, -1 if the reading screen is closed
    int m_openDiaryIndex = -1;
    bool m_journalOpen = false;
    int m_journalSelectedIndex = 0;
    // Edge tracking for tickReadingOverlayInput(), separate from PlayerController's
    bool m_overlayEKeyWasDown = false;
    bool m_overlayEnterKeyWasDown = false;
    bool m_overlayTabKeyWasDown = false;
    bool m_overlayUpKeyWasDown = false;

    // Throwable stones, 1-2 per pocket, picked up automatically on approach
    std::vector<glm::vec3> m_stoneWorldPositions;
    std::vector<unsigned char> m_stonePickedUp;     // same indexing as above: 0/1

    // Mesh of stones lying on the floor, rebuilt on pickup
    GLuint m_stonePickupVao = 0, m_stonePickupVbo = 0, m_stonePickupEbo = 0;
    GLsizei m_stonePickupIndexCount = 0;

    // Parabolic flight without bounce; walls and floor make noise, enemies get aggroed
    struct ThrownStone {
        glm::vec3 pos;
        glm::vec3 vel;
        float life = 0.0f; // seconds in flight (safety cap)
    };
    std::vector<ThrownStone> m_thrownStones;

    GLuint m_thrownStoneVao = 0, m_thrownStoneVbo = 0, m_thrownStoneEbo = 0;
    GLsizei m_thrownStoneIndexCount = 0;

    // Deterministic from the seed; runs right after placeTorches()
    void placeStones(unsigned int seed);

    void tryAutoPickupStones();

    void rebuildStonePickupMesh();

    // Called from render(), which has this frame's enemy positions
    void updateThrownStones(float deltaTime);

    void updateGlitchEffects(float deltaTime);

    // Unpaused play time of the current run, the only input of glitchStage()
    float m_glitchElapsedTime = 0.0f;

    float m_glyphGlitchCooldown = 6.0f;   // short pause after level load
    float m_glyphGlitchActiveTimer = 0.0f;
    glm::vec2 m_glyphGlitchUV{ 0.5f, 0.5f };
    float m_glyphGlitchRadiusCells = 4.0f;

    // Effect 2: a torch trembles (scene.vert)
    float m_torchGlitchCooldown = 12.0f;
    double m_torchGlitchUntilTime = -1.0; // absolute glfwGetTime(), compared with uTime in the shader
    int m_torchGlitchIndex = -1;

    // Effect 4: flickering silhouette, drawn as another EnemyCharacter
    float m_silhouetteCooldown = 45.0f;
    float m_silhouetteActiveTimer = 0.0f;
    EnemyCharacter m_glitchGhost; // shares m_enemySharedModel with the real enemies

    // Effect 3: a fake moan, or fake footsteps that do not match the player's gait
    float m_soundGlitchCooldown = 30.0f;
    EnemyAudio m_glitchAudio;

    // Fake footsteps: plays a step every m_fakeFootstepsNextStepIn while the timer runs
    float m_fakeFootstepsTimer = 0.0f;
    float m_fakeFootstepsNextStepIn = 0.0f;
    bool m_fakeFootstepsIsRun = false; // which sound/cadence this active window uses

    bool m_overlayDownKeyWasDown = false;
    float m_winBlockedMessageTimer = 0.0f;

    // Center of the finish safe zone
    glm::vec3 m_exitDoorPos{ 0.0f, 0.0f, 0.0f };

    // Debug map overlay (M); visibility is PlayerController state
    DebugMapOverlay m_debugMapOverlay;

    // Fog of war and the minimap textures
    MinimapFog m_minimapFog;

    std::vector<glm::vec3> m_torchWallBase;
    std::vector<glm::vec3> m_torchNormal;
    std::vector<glm::vec3> m_torchFlamePos;
    std::vector<glm::vec3> m_torchColor;
    std::vector<float>     m_torchIntensity;

    // Torch presence per cell for the minimap, so it does not rescan the torch list per cells
    std::vector<unsigned char> m_torchCellLookup; // size m_mapW*m_mapH, 0/1

    SceneGeometry m_geometry;

    // Loaded in init(); change WallTexture::kDefaultName to use another file
    WallTexture m_wallTex;

    GLuint m_program = 0;

    // Uniform locations of m_program, resolved once in init()
    GLint m_uniView = -1;
    GLint m_uniProjection = -1;
    GLint m_uniCamPos = -1;

    GLint m_uniPlayerLightPos = -1;
    GLint m_uniPlayerLightDir = -1;
    GLint m_uniPlayerLightColor = -1;
    GLint m_uniPlayerLightIntensity = -1;
    GLint m_uniIsViewmodelDraw = -1;
    GLint m_uniViewmodelTorchFuel = -1;
    GLint m_uniGlitchTorchIndex = -1, m_uniGlitchTorchUntilTime = -1;

    GLint m_uniDevLightPos = -1;
    GLint m_uniDevLightDir = -1;
    GLint m_uniDevLightCount = -1;

    GLint m_uniDevPaletteOverride = -1;
    GLint m_uniInvView = -1;
    GLint m_uniViewmodelSway = -1;
    GLint m_uniTime = -1;
    GLint m_uniRenderDistance = -1;
    GLint m_uniDepthOnly = -1;
    GLint m_uniAlpha = -1;
    GLint m_uniWallTex = -1;
    GLint m_uniWallTexEnabled = -1;
    GLint m_uniWallTexContrast = -1;

    // Parallel to m_torchWallBase
    // A taken torch has zero intensity; its flame is baked into the chunk mesh and hidden through uTorchLitMask
    std::vector<unsigned char> m_torchTaken; // size == m_torchWallBase.size(), 0/1

    static constexpr int kTorchLitMaskDim = 32; // 32*32 = 1024 >= MAX_TORCHES
    GLuint m_torchLitMaskTex = 0;
    GLint m_uniTorchLitMask = -1;

    // Untaken wall torch next to the player, -1 if none; updated in processInput()
    int m_nearbyWallTorchIndex = -1;

    float m_torchEmptyMessageTimer = 0.0f;

    // Extinguishes the torch and adds it to the inventory
    void pickupWallTorch(int torchIndex);

    // Guide torches (indices >= this) burn blue and cannot be taken;
    // m_torchTaken is set for them only to suppress the prompt
    // saveActiveSlot() must skip them
    // -1 = none
    int m_firstGuideTorchIndex = -1;

    // Visit order of the pockets (nearest-neighbor tour)
    std::vector<int> m_safeZoneChainOrder;

    // Walls drawn blue on the minimap:
    // pocket perimeters, the path toward the next pocket, guide torch walls
    std::vector<unsigned char> m_guideTorchCellLookup;

    // Perimeters of pockets whose diary was read, drawn purple on the minimap
    std::vector<unsigned char> m_diaryReadWallLookup;

    void rebuildDiaryReadWallLookup();

    // Deterministic from the seed
    // Must run before m_geometry.build(), which bakes torches into the mesh
    void placeLandmarks(unsigned int seed);

    // Static mesh of the decorative props placed by placeLandmarks()
    GLuint m_landmarkPropVao = 0, m_landmarkPropVbo = 0, m_landmarkPropEbo = 0;
    GLsizei m_landmarkPropIndexCount = 0;
    std::vector<glm::vec2> m_landmarkPropPositionsXZ; // for the debug map (M), see DebugMapOverlay

    // Win sequence:
    // the exit door swings open, the donut spins in the doorway
    // Its geometry changes during play, so it has its own dynamic VAO
    enum class WinSequenceState { None, Opening, Spinning };
    WinSequenceState m_winSequenceState = WinSequenceState::None;
    // Seconds since the current phase started
    float m_winSequenceTimer = 0.0f;
    float m_torusAngleA = 0.0f; // two independent rotation angles: the donut tumbles on both at once
    float m_torusAngleB = 0.0f;
    GLuint m_exitDoorVao = 0, m_exitDoorVbo = 0, m_exitDoorEbo = 0;
    GLsizei m_exitDoorIndexCount = 0;
    bool m_exitDoorBuiltOnce = false;

    // The spinning phase can last indefinitely, so the mesh buffers are reused
    std::vector<Vertex> m_exitDoorVertsScratch;
    std::vector<GLuint> m_exitDoorIndicesScratch;

    // The donut has its own mesh so it can be drawn translucent while it fades in
    GLuint m_donutVao = 0, m_donutVbo = 0, m_donutEbo = 0;
    GLsizei m_donutIndexCount = 0;
    float m_donutAlpha = 0.0f;
    std::vector<Vertex> m_donutVertsScratch;
    std::vector<GLuint> m_donutIndicesScratch;

    static constexpr float kExitDoorOpenDuration = 1.2f; // seconds
    static constexpr float kDonutAppearDuration = 1.5f;  // seconds, fades in from transparent
    // Seconds of spinning before the donut can be pressed
    static constexpr float kWinSpinBeforePressable = 5.0f;

    // Consumable request: E at the exit door with enough diaries
    bool m_winSequenceRequested = false;

public:
    // Spinning long enough to show "[E] PRESS TO WIN"
    bool winReadyToPressE() const
    {
        return m_winSequenceState == WinSequenceState::Spinning &&
               m_winSequenceTimer >= kWinSpinBeforePressable;
    }
    bool winSequenceActive() const { return m_winSequenceState != WinSequenceState::None; }

    // End of game; Application polls it every frame during PLAYING
    bool consumeCreditsRequest() { return m_player.consumeCreditsRequest(); }

    void teleportCameraForCredits() { m_player.teleportCameraOutOfBounds(); }

    // Shows the activation hint on approach, with the same radius as the press chec
    bool nearExitDoorReadyToActivate() const
    {
        if (winSequenceActive() || diariesReadCount() < diariesToWin())
            return false;
        const glm::vec3 camPos = m_player.camPos();
        const float dx = m_exitDoorPos.x - camPos.x;
        const float dz = m_exitDoorPos.z - camPos.z;
        const float kWinInteractRadius = 1.4f; // matches PlayerController.cpp
        return (dx * dx + dz * dz) <= kWinInteractRadius * kWinInteractRadius;
    }

private:
    void updateWinSequence(float deltaTime);

    // Save restore: extinguishes torches without crediting the inventory
    void restoreTorchPickups(const std::vector<int>& takenIndices);

    void extinguishTorchVisualAndLight(int torchIndex);
    void rebuildTorchCellLookupFromTaken();

    void resetTorchLitMask();
    GLint m_uniTorchPos = -1;
    GLint m_uniTorchColor = -1;
    GLint m_uniTorchIntensity = -1;
    GLint m_uniTorchCount = -1;
    GLint m_uniTorchShadowRow = -1;
    GLint m_uniTorchShadowMap = -1;

    // Enemies are dynamic occluders, tested analytically in scene.frag
    GLint m_uniEnemyOccluderPosXZ = -1;
    GLint m_uniEnemyOccluderCount = -1;
    GLint m_uniEnemyOccluderRadius = -1;
    GLint m_uniEnemyOccluderHeight = -1;
    void cacheUniformLocations();
    void uploadDevLights(GLint posLoc, GLint dirLoc, GLint countLoc) const;
    void updateViewmodelSway();

    Difficulty m_difficulty = Difficulty::Normal;
    void applyDifficulty();

    Lighting m_lighting;
    TorchShadowMap m_torchShadowMap;

    // Per-frame scratch buffers sized by reserveRenderScratchBuffers(); clear() keeps the capacity
    std::vector<char>          m_chunkVisible;
    std::vector<GLsizei>       m_mainCounts;
    std::vector<const GLvoid*> m_mainOffsets;
    std::vector<GLint>         m_particleFirsts;
    std::vector<GLsizei>       m_particleCounts;
    void reserveRenderScratchBuffers();

    // This frame's enemy XZ positions for enemyOccluderPosXZ in scene.frag
    std::vector<glm::vec2> m_enemyOccluderScratch;

    int m_lastVisibleTriangles = 0;
    int m_lastVisibleParticles = 0;
    int m_lastActiveTorchCount = 0;
    int m_lastVisibleChunkCount = 0;

    PlayerController m_player;

    bool m_colorEnabled = false;

    // Draw and fog distance of the current frame
    float m_currentRenderDistance = 16.0f;

    Compass m_compass;
    PlayerTorchViewmodel m_playerTorch;

    // Owns the shared enemy model;
    // m_testEnemy and m_enemies[] only reference it
    SkinnedModel m_enemySharedModel;
    GLuint m_enemyProgram = 0;
    GLint m_uEnemyView = -1, m_uEnemyProjection = -1, m_uEnemyModel = -1, m_uEnemyBoneMatrices = -1;
    GLint m_uEnemyCamPos = -1, m_uEnemyTime = -1;
    GLint m_uEnemyPlayerLightPos = -1, m_uEnemyPlayerLightDir = -1, m_uEnemyPlayerLightColor = -1, m_uEnemyPlayerLightIntensity = -1;
    GLint m_uEnemyDevLightPos = -1, m_uEnemyDevLightDir = -1, m_uEnemyDevLightCount = -1;
    GLint m_uEnemyDevPaletteOverride = -1;
    GLint m_uEnemyTorchPos = -1, m_uEnemyTorchColor = -1, m_uEnemyTorchIntensity = -1, m_uEnemyTorchCount = -1;
    GLint m_uEnemyTorchShadowMap = -1, m_uEnemyTorchShadowRow = -1;
    GLint m_uEnemyRenderDistance = -1;
    GLint m_uEnemyDiffuseTex = -1, m_uEnemyHasDiffuseTex = -1;
    void cacheEnemyUniformLocations();
    EnemyCharacter m_testEnemy; // dev-tools dummy (K key), see spawnDevDummyEnemyAtPlayerView()
    EnemyAI m_enemyAI;          // the dummy's position for the debug map; its AI never runs

    // One enemy per map sector.
    static constexpr int kEnemyCount = 7;
    std::array<EnemyCharacter, kEnemyCount> m_enemies;
    std::array<EnemyAI, kEnemyCount> m_enemyAIs;

    // Whether the player can see an enemy (minimap marker):
    // FOV cone, range and line of sight
    // It raycasts it runs on the perception interval
    bool isEnemyVisibleToPlayer(const glm::vec3& enemyPos) const;

    std::array<float, kEnemyCount> m_playerVisibilityCheckTimers{};
    std::array<bool, kEnemyCount> m_cachedPlayerCanSeeEnemy{};

    // One enemy per non-overlapping sector; called once per map
    void spawnEnemiesAcrossMap(uint32_t seed);

    // Dev tools:
    // L spotlight
    // K enemy dummy
    // U undo
    // P palette cycle

    struct DevSpotlight
    {
        glm::vec3 position;
        glm::vec3 direction;
    };
    // At most 8 (devLightPos[8] in the shaders)
    std::vector<DevSpotlight> m_devSpotlights;

    bool m_devDummyEnemyActive = false;

    enum class DevActionType { SpawnLight, SpawnDummyEnemy };
    struct DevAction
    {
        DevActionType type;
        // Undo restores whether the dummy existed, not its previous position
        bool dummyWasActiveBefore = false;
    };
    std::vector<DevAction> m_devActionHistory;

    bool m_devLightKeyWasDown = false;
    bool m_devEnemyKeyWasDown = false;
    bool m_devUndoKeyWasDown = false;

    void spawnDevLightAtPlayerView();
    void spawnDevDummyEnemyAtPlayerView();
    void undoLastDevAction();

    // P key: -1 = zone palettes, otherwise one palette forced on everything
    int m_devPaletteOverride = -1;
    bool m_devPaletteKeyWasDown = false;
    void cycleDevPalette();

    double m_lastEnemyUpdateTime = 0.0;
    bool m_enemyUpdateInit = false;

    // Hand torch sway: the torch lags behind sharp turns and catches up (updateViewmodelSway())
    float m_playerTorchLagYaw = 0.0f;
    bool m_playerTorchLagInit = false;
    double m_playerTorchLastSwayTime = 0.0;
    // Second smoothing stage: the clamp bounds the sway, this bounds its rate
    float m_playerTorchSwayVisual = 0.0f;

    // Bob phase separate from the camera's, so the torch can be tuned on its own
    float m_playerTorchBobPhase = 0.0f;
    float m_playerTorchMoveBlend = 0.0f;

    glm::vec3 getFront() const;

    bool isWall(int x, int z) const;
    bool isFloor(int x, int z) const;
    // CornerCut::None for regular cells, floors and cells outside the grid
    WallShapes::CornerCut wallCornerCut(int x, int z) const;
    // Only meaningful where wallCornerCut() != None
    float wallChamferSize(int x, int z) const;

    // Deterministic from the seed
    void generateMap(unsigned int seed);
    void placeTorches(unsigned int seed);

    // Map, torches, geometry and PVS for a seed
    // Leaves the player, slot and fog of war to the caller
    void loadMapAndGeometry(unsigned int seed);

    // New map plus full player reset, without touching saves
    void generateFreshMapAndPlayer();

    // Slot the session saves to (-1 for the menu background) and the current map seed
    int m_activeSlot = -1;
    unsigned int m_currentSeed = 0;

    // Empty until the player names the save; loadSlot() restores it
    std::string m_saveName;

    GLuint compileShader(GLenum type, const char* src);
    GLuint linkProgram(GLuint vs, GLuint fs);

};

// aboba said: "HELP ME"
