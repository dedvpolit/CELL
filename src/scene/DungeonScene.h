#pragma once
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
#include "Lighting.h"
#include "PlayerController.h"
#include "audio/EnemyAudio.h"

// Optional dev tools, see DevTools.h. Included via __has_include, so a missing file does not break
// the build: HAS_DEV_TOOLS is then undefined and the dev-key blocks are compiled out.
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

    // True while the diary reading screen or the journal is open. It is an overlay on top of
    // PLAYING, so Application uses this to route input to tickReadingOverlayInput().
    bool isReadingOverlayOpen() const { return m_openDiaryIndex != -1 || m_journalOpen; }

    // Number of diaries read: used by PlayerController (win button gate, kMinDiariesToWin) and by
    // Application for the "need N more" message.
    int diariesReadCount() const {
        int n = 0;
        for (bool read : m_diariesRead) if (read) ++n;
        return n;
    }

    // True briefly after a failed press of the win button without enough diaries read (see
    // PlayerController::consumeWinBlockedRequest()); Application shows a message meanwhile.
    bool showWinBlockedMessage() const { return m_winBlockedMessageTimer > 0.0f; }

    // Index of an unpicked wall torch within E range (see m_nearbyWallTorchIndex); used for the
    // "[E] TAKE TORCH" hint.
    int nearbyWallTorchIndex() const { return m_nearbyWallTorchIndex; }

    bool showTorchEmptyMessage() const { return m_torchEmptyMessageTimer > 0.0f; }

    int playerTorchInventoryCount() const { return m_player.torchInventoryCount(); }
    int playerStoneCount() const { return m_player.stoneCount(); }

    // "Unreliable vision": anomalies that intensify with time spent in the run
    // (m_glitchElapsedTime, advanced only during unpaused gameplay). 90 s: flickering wall glyph;
    // 180 s: the nearest torch trembles; 300 s: deceptive sound; 420 s: flickering enemy silhouette
    // and more frequent effects.
    int glitchStage() const
    {
        const float t = m_glitchElapsedTime;
        if (t >= 420.0f) return 4;
        if (t >= 300.0f) return 3;
        if (t >= 180.0f) return 2;
        if (t >= 90.0f)  return 1;
        return 0;
    }

    // Effect 1 (wall glyph) is the only one drawn outside DungeonScene::render(): it lives in the
    // ASCII post-process owned by Application, so it is the only one with public state getters.
    bool glyphGlitchActive() const { return m_glyphGlitchActiveTimer > 0.0f; }
    glm::vec2 glyphGlitchUV() const { return m_glyphGlitchUV; }
    float glyphGlitchRadiusCells() const { return m_glyphGlitchRadiusCells; }

    // Index of the diary in E range (-1 = none), updated in processInput(); used for the "[E] READ
    // DIARY" hint before the reading screen opens.
    int nearbyDiaryIndex() const { return m_nearbyDiaryIndex; }

    // Closes whatever is open. The diary takes priority over the journal (both open at once should
    // not happen: opening an entry collapses the journal). Application calls it on Escape.
    void closeDiaryOrJournal() {
        if (m_openDiaryIndex != -1) m_openDiaryIndex = -1;
        else m_journalOpen = false;
    }

    // Polls E (close the diary / open the highlighted journal entry), Tab (close the journal) and
    // the up/down arrows (list navigation). Called instead of processInput() while
    // isReadingOverlayOpen().
    void tickReadingOverlayInput(GLFWwindow* window);

    // Builds the grid for ascii.setUIOverlay(): the diary frame or the journal list, depending on
    // what is open. Returns false and leaves the grid empty if nothing is open.
    bool buildReadingOverlayGrid(std::vector<unsigned char>& grid, int cols, int rows) const;

    // Same frame bounds as buildReadingOverlayGrid() (in menu-grid cells). Application converts
    // them to pixels (AsciiEffect::menuCellSizeForWindow()) to place TextRenderer text inside the
    // frame.
    void getReadingBoxBounds(int cols, int rows, int& x0, int& y0, int& x1, int& y1) const;

    const Diaries::PlacedDiary* openDiary() const {
        return (m_openDiaryIndex != -1 && m_openDiaryIndex < (int)m_diaries.size())
            ? &m_diaries[(size_t)m_openDiaryIndex] : nullptr;
    }

    bool isJournalListMode() const { return m_journalOpen && m_openDiaryIndex == -1; }

    int diariesTotalCount() const { return (int)m_diaries.size(); }
    bool diaryReadAt(int i) const { return i >= 0 && i < (int)m_diariesRead.size() && m_diariesRead[(size_t)i]; }
    int journalSelectedIndex() const { return m_journalSelectedIndex; }

    void processMouse(double xpos, double ypos);

    // Resets only the mouse-look baseline; yaw, pitch and position are unchanged. Needed when
    // re-capturing GLFW_CURSOR_DISABLED after a pause, so the first cursor event is not read as a
    // huge dx/dy.
    void resetMouseLook() { m_player.resetMouseLook(); }

    void tickPauseCameraIdle(float deltaTime);

    // Gameplay ambience (audio/GameplayMusic.h): the track plus the diary-read volume duck. Called
    // every frame by Application::tick() and not folded into processInput(), which is skipped while
    // the reading overlay is open, while the duck must keep tracking diariesReadCount().
    void tickAmbientMusic(float deltaTime, bool active)
    {
        m_ambientMusic.update(deltaTime, active, diariesReadCount());
    }

    // gameplayActive is false during pause/menu, when the AI must not update: a catch would start
    // the death sequence, but the transition is gated on PLAYING and would never fire.
    void render(int viewportWidth, int viewportHeight, bool gameplayActive);

    // Draws the compass into the currently bound framebuffer. Call it after AsciiEffect::end(), not
    // inside begin()/end(), so the ASCII post-process does not reprocess it.
    void renderCompassOverlay(int viewportWidth, int viewportHeight);

    // Debug full-map overlay (M key): the whole maze, unfogged, as a panel with the player's
    // position and orientation. Call after AsciiEffect::end(), like renderCompassOverlay(). Does
    // nothing while hidden.
    void renderDebugMap(int viewportWidth, int viewportHeight);

    // Color ASCII mode (Settings -> COLOR). The compass has its own shader outside the ASCII
    // post-process, so its tint is enabled separately with the same flag.
    void setColorEnabled(bool enabled) { m_colorEnabled = enabled; }

    // Perf diagnostics (Application::tick()'s [perf] log): what render() submitted last frame, to
    // tell whether an fps dip matches more geometry, more particles or more active torches (the
    // per-fragment shadow raymarch scales with the latter).
    int getLastVisibleTriangles() const { return m_lastVisibleTriangles; }
    int getLastVisibleParticles() const { return m_lastVisibleParticles; }

    int getLastActiveTorchCount() const { return m_lastActiveTorchCount; }
    int getLastVisibleChunks() const { return m_lastVisibleChunkCount; }

    static const int MAX_TORCHES = 1024; // must stay consistent with PARTICLE_ID_SCALE in the shader
    // Every active torch costs a full DDA shadow raymarch per fragment in scene.frag: the first
    // lever to pull for FPS drops in torch-dense rooms.
    static const int MAX_ACTIVE_TORCHES = 20;

    float getStaminaFraction() const { return m_player.staminaFraction(); }

    float getHealthFraction() const { return m_player.healthFraction(); }

    // True while debug noclip is active (N key, see DevTools.h). main.cpp uses it to hide the
    // stamina bar and switch AsciiEffect to cinematic mode. Always false without DevTools.h or with
    // DEV_TOOLS_ENABLED=0.
    bool isNoclipEnabled() const { return m_player.noclipEnabled(); }

    // View distance multiplier (1.0 for a regular player, see DevTools.h). Used in render() for the
    // shader's RENDER_DISTANCE, the far projection plane and the active-torch selection radius.
    float getViewDistanceMultiplier() const {
#ifdef HAS_DEV_TOOLS
        return DevTools::GetViewDistanceMultiplier();
#else
        return 1.0f;
#endif
    }

    // Whether the cinematic resolution/detail boost (C key, DevTools.h) applies while noclip is on.
    // Always false without DevTools.h.
    bool isCinematicResolutionEnabled() const {
#ifdef HAS_DEV_TOOLS
        return DevTools::IsCinematicResolutionEnabled();
#else
        return false;
#endif
    }

    // Cinematic mode parameters; the constants live in DevTools.h. Without it these return regular
    // values, which keeps the getters meaningful on their own.
    int getCinematicSceneWidth() const {
#ifdef HAS_DEV_TOOLS
        return DevTools::kCinematicSceneW;
#else
        return 1280;
#endif
    }
    int getCinematicSceneHeight() const {
#ifdef HAS_DEV_TOOLS
        return DevTools::kCinematicSceneH;
#else
        return 720;
#endif
    }
    int getCinematicCellSize() const {
#ifdef HAS_DEV_TOOLS
        return DevTools::kCinematicCellSize;
#else
        return 11;
#endif
    }

    void setCompassUiFont(
        GLuint texture,
        int glyphCount
    );

    bool hasWon() const { return m_player.hasWon(); }
    bool isDying() const { return m_player.isDying(); }
    bool consumeDeathFadeTrigger() { return m_player.consumeDeathFadeTrigger(); }
    void tickDeathFade(float deltaTime) { m_player.tickDeathFade(deltaTime); }

    // Camera sensitivity range for the SETTINGS screen, picked by hand: the minimum is noticeably
    // slower than default but controllable, the maximum is fast but still controllable. The slider
    // stores only a 0..1 position and converts it itself.
    static constexpr float kMinMouseSensitivity = PlayerController::kMinMouseSensitivity;
    static constexpr float kMaxMouseSensitivity = PlayerController::kMaxMouseSensitivity;

    float getMouseSensitivity() const { return m_player.mouseSensitivity(); }
    void setMouseSensitivity(float sensitivity) { m_player.setMouseSensitivity(sensitivity); }

    void generateMenuBackgroundMaze();

    // A fresh random maze and a full player reset; the active slot is saved immediately so CONTINUE
    // sees it. explicitSlot >= 0 uses that slot and name as given; otherwise
    // SaveSystem::PickSlotForNewGame() chooses and the name is empty (death path).
    void newGame(int explicitSlot = -1, const std::string& explicitName = std::string());

    // Loads a saved slot (0..SaveSystem::kSlotCount-1): the saved seed rebuilds the exact maze and
    // torch layout, then the saved player state and fog of war are applied on top. Returns false if
    // the slot is empty or corrupted.
    bool loadSlot(int slotIndex);

    // Overwrites the active slot (m_activeSlot) with the current state: autosave, and before
    // exiting to the menu. No-op without an active slot.
    void saveActiveSlot();

    // Saves the current state to the given slot under the given name (typed in
    // AppState::SAVE_NAME_ENTRY, up to SaveSystem::kNameMaxLen chars) and makes it the active slot,
    // so autosaves continue there.
    void saveToSlot(int slotIndex, const std::string& name) {
        m_activeSlot = slotIndex;
        m_saveName = name;
        saveActiveSlot();
    }

private:

    int m_mapW = 0, m_mapH = 0;
    std::vector<int> m_map;

    // Copy of m_map with column cells also marked as wall, used only for enemy pathfinding.
    // Computed once in loadMapAndGeometry() and shared by all enemies by pointer (EnemyAI::init()).
    std::vector<int> m_pathfindingMapWithColumns;

    // Chamfered wall corners (WallShapes.h), built once in generateMap() from the maze seed; size
    // m_mapW*m_mapH, indexed z*m_mapW+x like m_map. Geometry and collision (wallCornerCut()) read
    // this one table, so silhouette and collision cannot drift apart.
    std::vector<WallShapes::CornerCut> m_wallCornerCuts;

    std::vector<float> m_chamferSizes;

    // Free-standing columns (Columns.h): world XZ centers. Currently always empty: column
    // generation is disabled in generateMap().
    std::vector<glm::vec2> m_columnCentersXZ;

    // Zoning (Zoning.h). The sector grid is not read again after generateMap(); it is a member only
    // so the debug map (M key) can draw sector boundaries and profiles.
    Zoning::ZoneGrid m_zoneGrid;

    // Corridor width (CorridorWidth.h): mapW*mapH mask, 1 if the cell became floor through corridor
    // widening rather than from MapGenerator. Only the debug map needs it.
    std::vector<unsigned char> m_corridorWidened;

    // Final wall/floor color per cell (mapW*mapH), not a palette index: already blended between the
    // two nearest zones near a border (BlendedZoneColor() in the .cpp). Built with the other
    // per-cell arrays in generateMap(). See Zoning.h and GetZonePalette in SceneGeometry.cpp.
    std::vector<glm::vec3> m_paletteWallColors;
    std::vector<glm::vec3> m_paletteFloorColors;

    // Diagonal chains (DiagonalCorridors.h): mapW*mapH mask, 1 for cells in a long enough chain of
    // same-type eligible chamfers (DiagonalCorridors::DetectChains); their chamfer is forced in
    // generateMap(). Kept afterward only for the debug map.
    std::vector<unsigned char> m_diagonalChainMask;

    // Finish safe zone (opposite corner): bounds set in generateMap(); placeTorches() places its
    // own torches there.
    int m_endSafeX0 = 0, m_endSafeZ0 = 0, m_endSafeX1 = 0, m_endSafeZ1 = 0;

    // Small safe zones (pockets): centers set in generateMap(); placeTorches() guarantees torches
    // in each pocket.
    std::vector<glm::ivec2> m_smallSafeZoneCenters;
    int m_smallSafeZoneRadius = 0;

    // Diaries (Diaries.h): one per pocket, m_diaries[i] sits at m_smallSafeZoneCenters[i].
    // m_diariesRead[i] says whether it has been read (affects UI and saves only; the diary stays in
    // the world).
    std::vector<Diaries::PlacedDiary> m_diaries;
    std::vector<bool> m_diariesRead;

    // Gameplay ambience (GameplayMusic.h). Not reset on newGame()/loadSlot(): a stale replay timer
    // is harmless and the duck target is recomputed every frame.
    GameplayMusic m_ambientMusic;
    int m_nearbyDiaryIndex = -1;
    // -1 = reading screen closed; otherwise the index into m_diaries of the diary being read
    // (opened in place with E, or from the journal, see m_journalOpen).
    int m_openDiaryIndex = -1;
    bool m_journalOpen = false;
    int m_journalSelectedIndex = 0;
    // Edge tracking for tickReadingOverlayInput(). Separate from PlayerController's E/Tab trackers
    // because that function is called instead of processInput().
    bool m_overlayEKeyWasDown = false;
    bool m_overlayEnterKeyWasDown = false;
    bool m_overlayTabKeyWasDown = false;
    bool m_overlayUpKeyWasDown = false;

    // Throwable stones (a distraction mechanic): 1-2 per safe zone (m_smallSafeZoneCenters),
    // auto-picked up on approach with no key and no HUD hint (see tryAutoPickupStones()).
    std::vector<glm::vec3> m_stoneWorldPositions;
    std::vector<unsigned char> m_stonePickedUp;     // same indexing as above — 0/1

    // Dynamic mesh of pickable (lying) stones, rebuilt rarely (on pickup, see
    // rebuildStonePickupMesh()), unlike m_thrownStone*, which is rebuilt every frame while any
    // stone is in flight.
    GLuint m_stonePickupVao = 0, m_stonePickupVbo = 0, m_stonePickupEbo = 0;
    GLsizei m_stonePickupIndexCount = 0;

    // Active thrown stones (updateThrownStones()): simple parabolic flight (gravity, no spin or
    // bounce), tested every frame against walls/floor (EnemyAI::notifyNoiseEvent()) and against
    // enemies (EnemyAI::forceAggroFromImpact()).
    struct ThrownStone {
        glm::vec3 pos;
        glm::vec3 vel;
        float life = 0.0f; // seconds in flight (safety cap)
    };
    std::vector<ThrownStone> m_thrownStones;

    GLuint m_thrownStoneVao = 0, m_thrownStoneVbo = 0, m_thrownStoneEbo = 0;
    GLsizei m_thrownStoneIndexCount = 0;

    // Places stones (1-2 per pocket, deterministic from the seed) and builds the initial pickup
    // mesh. Called from loadMapAndGeometry() right after placeTorches().
    void placeStones(unsigned int seed);

    void tryAutoPickupStones();

    void rebuildStonePickupMesh();

    // Ticks physics for m_thrownStones (wall collision -> EnemyAI::notifyNoiseEvent(), enemy
    // collision -> EnemyAI::forceAggroFromImpact()) and rebuilds the thrown-stone mesh. Called from
    // render(), which has this frame's enemy positions (see m_enemyAIs about the one-frame lag).
    void updateThrownStones(float deltaTime);

    void updateGlitchEffects(float deltaTime);

    // Elapsed real (unpaused) gameplay time of the current playthrough; the only input of
    // glitchStage(). Reset in loadMapAndGeometry().
    float m_glitchElapsedTime = 0.0f;

    float m_glyphGlitchCooldown = 6.0f;   // short pause after level load
    float m_glyphGlitchActiveTimer = 0.0f;
    glm::vec2 m_glyphGlitchUV{ 0.5f, 0.5f };
    float m_glyphGlitchRadiusCells = 4.0f;

    // Effect 2: nearest torch trembles; read directly in render() (scene.vert), no public getter.
    float m_torchGlitchCooldown = 12.0f;
    double m_torchGlitchUntilTime = -1.0; // absolute glfwGetTime(), compared with uTime in the shader
    int m_torchGlitchIndex = -1;

    // Effect 4 (stage 4, 420 s+): flickering silhouette, read directly in render() (drawn as
    // another EnemyCharacter).
    float m_silhouetteCooldown = 45.0f;
    float m_silhouetteActiveTimer = 0.0f;
    EnemyCharacter m_glitchGhost; // shares m_enemySharedModel with the real enemies

    // Stage 3 (300 s+): a fake enemy moan, or mismatched fake footsteps for 2-4 s (walk sound while
    // running or vice versa). The variant that would overlap the player's real steps is excluded.
    float m_soundGlitchCooldown = 30.0f;
    EnemyAudio m_glitchAudio;

    // Fake-footsteps sub-effect (two of the three variants share this timer). While > 0,
    // updateGlitchEffects() calls m_player.playFakeWalkFootstep() or playFakeRunFootstep() (per
    // m_fakeFootstepsIsRun) every step interval, counted down by m_fakeFootstepsNextStepIn.
    float m_fakeFootstepsTimer = 0.0f;
    float m_fakeFootstepsNextStepIn = 0.0f;
    bool m_fakeFootstepsIsRun = false; // which sound/cadence this active window uses

    bool m_overlayDownKeyWasDown = false;
    // Timer for the "need N more diaries" message; counts down to 0 in processInput() (see
    // showWinBlockedMessage()).
    float m_winBlockedMessageTimer = 0.0f;

    // Win button position at the center of the finish safe zone (see generateMap() and
    // addWinButtonMesh()); the player presses E next to it.
    glm::vec3 m_winButtonPos{ 0.0f, 0.0f, 0.0f };

    // Debug full-map overlay (M key), testing only: the whole m_map without fog of war, drawn after
    // AsciiEffect::end() like renderCompassOverlay(). Drawing lives in DebugMapOverlay; visibility
    // is PlayerController state.
    DebugMapOverlay m_debugMapOverlay;

    // Fog of war and minimap (MinimapFog.h). m_map stays here; MinimapFog owns the fog logic and
    // the related GL textures (full map and minimap) and receives the data it needs as parameters.
    MinimapFog m_minimapFog;

    std::vector<glm::vec3> m_torchWallBase;
    std::vector<glm::vec3> m_torchNormal;
    std::vector<glm::vec3> m_torchFlamePos;
    std::vector<glm::vec3> m_torchColor;
    std::vector<float>     m_torchIntensity;

    // O(1) "is there a torch on this cell" lookup for MinimapFog::updateMinimap(), built once in
    // placeTorches() (see MapGenerator::PlaceTorches()/buildTorchCellLookup()) instead of
    // rescanning the torch list for every minimap cell each frame.
    std::vector<unsigned char> m_torchCellLookup; // size m_mapW*m_mapH, 0/1

    // Chunked geometry (owned by SceneGeometry): chunk AABBs are tested against the frustum and
    // render distance, so only visible chunks are drawn.
    SceneGeometry m_geometry;

    // Wall texture (WallTexture.h), loaded in init() via m_wallTex.load(WallTexture::kDefaultName).
    // To use another texture, drop the file into assets/textures/walls/ and change
    // WallTexture::kDefaultName; the shader and pipeline stay untouched.
    WallTexture m_wallTex;

    GLuint m_program = 0;

    // Cached uniform locations for m_program. glGetUniformLocation() is a driver name lookup, so
    // they are resolved once in init() (cacheUniformLocations()) instead of every frame.
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
    GLint m_uniMapTex = -1;
    GLint m_uniWallTex = -1;
    GLint m_uniWallTexEnabled = -1;
    GLint m_uniWallTexContrast = -1;

    // Torch pickup state, parallel to m_torchWallBase (same index). The light is killed by
    // m_torchIntensity[i] = 0; the visible flame is baked into the static chunk mesh, so it is
    // hidden through a small lookup texture (uTorchLitMask in scene.frag) updated with one
    // glTexSubImage2D.
    std::vector<unsigned char> m_torchTaken; // size == m_torchWallBase.size(), 0/1

    static constexpr int kTorchLitMaskDim = 32; // 32*32 = 1024 >= MAX_TORCHES
    GLuint m_torchLitMaskTex = 0;
    GLint m_uniTorchLitMask = -1;

    // Wall torch (index into m_torchWallBase) the player stands next to and has not taken, -1 if
    // none. Computed every frame in processInput(); used for the "[E] TAKE TORCH" hint.
    int m_nearbyWallTorchIndex = -1;

    float m_torchEmptyMessageTimer = 0.0f;

    // Extinguishes wall torch i (light and flame) and adds +1 to the inventory. Rebuilds
    // m_torchCellLookup in full: it runs at most once per E press.
    void pickupWallTorch(int torchIndex);

    // Landmark torches (indices >= this, added by placeLandmarks()) have a blue flame and cannot be
    // picked up: m_torchTaken is set for them only to suppress the pickup prompt. -1 = none.
    // saveActiveSlot() must exclude them from torchTakenIndices or they would be extinguished on
    // load.
    int m_firstGuideTorchIndex = -1;

    // Visit order of the pockets (a nearest-neighbor tour over m_smallSafeZoneCenters built in
    // placeLandmarks()); each link gets 2-3 landmark torches along the first cells of its path.
    std::vector<int> m_safeZoneChainOrder;

    // Walls drawn blue on the compass minimap (pocket perimeters, the path toward the next pocket,
    // landmark torch walls); mapW*mapH, indexed like m_torchCellLookup. Separate from it because
    // that one excludes taken torches and landmarks always count as taken.
    std::vector<unsigned char> m_guideTorchCellLookup;

    // Perimeter walls of pockets whose diary was read are drawn purple on the minimap as a trail.
    // Dynamic, unlike m_guideTorchCellLookup: see rebuildDiaryReadWallLookup() (also called after a
    // load).
    std::vector<unsigned char> m_diaryReadWallLookup;

    void rebuildDiaryReadWallLookup();

    // Places landmark torches and m_guideTorchCellLookup, deterministic from the seed. Must run
    // before m_geometry.build(), which bakes torches into the static mesh.
    void placeLandmarks(unsigned int seed);

    // Decorative props (cairn/obelisk/tripod/cross, see AddCairnProp() etc. in the .cpp), built
    // once in placeLandmarks() into a separate static VAO; never rebuilt, unlike m_stonePickupVao.
    GLuint m_landmarkPropVao = 0, m_landmarkPropVbo = 0, m_landmarkPropEbo = 0;
    GLsizei m_landmarkPropIndexCount = 0;
    std::vector<glm::vec2> m_landmarkPropPositionsXZ; // for the debug map (M), see DebugMapOverlay

    // Final win sequence (monument dissolves -> spinning torus -> prompt). It has its own dynamic
    // VAO because this geometry changes during play and cannot live in a baked chunk.
    enum class WinSequenceState { None, Dissolving, Spinning };
    WinSequenceState m_winSequenceState = WinSequenceState::None;
    // seconds since the current phase (Dissolving or Spinning) started; resets on transition
    float m_winSequenceTimer = 0.0f;
    float m_torusAngleA = 0.0f; // two independent rotation angles: the donut tumbles on both at once
    float m_torusAngleB = 0.0f;
    GLuint m_winMonumentVao = 0, m_winMonumentVbo = 0, m_winMonumentEbo = 0;
    GLsizei m_winMonumentIndexCount = 0;
    bool m_winMonumentBuiltOnce = false;
    // see uMonumentFadeAlpha in scene.frag, computed in updateWinSequence()
    float m_winMonumentFadeAlpha = 1.0f;
    GLint m_uniMonumentFadeAlpha = -1;

    // Reusable scratch buffers for updateWinSequence(): the torus phase can last indefinitely if
    // the player just watches, so a fresh std::vector every frame would allocate continuously.
    std::vector<Vertex> m_winMonumentVertsScratch;
    std::vector<GLuint> m_winMonumentIndicesScratch;

    static constexpr float kWinDissolveDuration = 0.8f; // pedestal dissolve time, seconds
    // seconds of spinning before the donut can be pressed
    static constexpr float kWinSpinBeforePressable = 5.0f;

    // Whether the win sequence has been requested (E pressed at the pedestal with enough diaries);
    // a consumable flag, like m_diaryOpenRequested and similar ones in PlayerController.h.
    bool m_winSequenceRequested = false;

public:
    // What Application shows: in the Spinning phase, after kWinSpinBeforePressable seconds, "[E]
    // PRESS TO WIN".
    bool winReadyToPressE() const
    {
        return m_winSequenceState == WinSequenceState::Spinning &&
               m_winSequenceTimer >= kWinSpinBeforePressable;
    }
    bool winSequenceActive() const { return m_winSequenceState != WinSequenceState::None; }

    // "End of game" (see PlayerController::m_creditsRequested). Application calls this every frame
    // during PLAYING (same consume pattern as the other requests).
    bool consumeCreditsRequest() { return m_player.consumeCreditsRequest(); }

    void teleportCameraForCredits() { m_player.teleportCameraOutOfBounds(); }

    // Shows the [E] ACTIVATE hint on approach (with enough diaries), not only after a press; same
    // radius as the press check in PlayerController::processInput().
    bool nearWinMonumentReadyToActivate() const
    {
        if (winSequenceActive() || diariesReadCount() < PlayerController::kMinDiariesToWin)
            return false;
        const glm::vec3 camPos = m_player.camPos();
        const float dx = m_winButtonPos.x - camPos.x;
        const float dz = m_winButtonPos.z - camPos.z;
        const float kWinInteractRadius = 1.4f; // matches PlayerController.cpp
        return (dx * dx + dz * dz) <= kWinInteractRadius * kWinInteractRadius;
    }

private:
    void updateWinSequence(float deltaTime);

    // Restoring from a save: extinguishes the listed torches without crediting the inventory (the
    // spare count is restored by PlayerController::restoreTorchState()). Rebuilds m_torchCellLookup
    // once for the whole list.
    void restoreTorchPickups(const std::vector<int>& takenIndices);

    void extinguishTorchVisualAndLight(int torchIndex);
    void rebuildTorchCellLookupFromTaken();

    void resetTorchLitMask();
    GLint m_uniTorchPos = -1;
    GLint m_uniTorchColor = -1;
    GLint m_uniTorchIntensity = -1;
    GLint m_uniTorchCount = -1;
    // Columns: the shader needs them for the ray-vs-circle shadow test (shadowedByWall() in
    // assets/shaders/scene.frag), since a column cell is floor in mapTex and would not cast a
    // shadow by itself.
    GLint m_uniColumnPos = -1;
    GLint m_uniColumnCount = -1;
    GLint m_uniColumnRadius = -1;

    // Enemy shadows in shadowedByWall(): an exact ray-vs-cylinder test with a height check, like
    // the columns. No shadow map or extra GPU resources needed.
    GLint m_uniEnemyOccluderPosXZ = -1;
    GLint m_uniEnemyOccluderCount = -1;
    GLint m_uniEnemyOccluderRadius = -1;
    GLint m_uniEnemyOccluderHeight = -1;
    void cacheUniformLocations();

    Lighting m_lighting;

    // Persistent per-frame scratch buffers: sized once by reserveRenderScratchBuffers() (called
    // from init() after m_geometry.build()) and reused with clear() + push_back(), which does not
    // reallocate while the reserved capacity suffices. This avoids a malloc/free every render().
    std::vector<char>          m_chunkVisible;
    std::vector<GLsizei>       m_mainCounts;
    std::vector<const GLvoid*> m_mainOffsets;
    std::vector<GLint>         m_particleFirsts;
    std::vector<GLsizei>       m_particleCounts;
    void reserveRenderScratchBuffers();

    // Reusable buffer of this frame's enemy XZ positions for enemyOccluderPosXZ (see
    // scene.frag::shadowedByWall()); rebuilt every frame without reallocation.
    std::vector<glm::vec2> m_enemyOccluderScratch;

    int m_lastVisibleTriangles = 0;
    int m_lastVisibleParticles = 0;
    int m_lastActiveTorchCount = 0;
    int m_lastVisibleChunkCount = 0;

    PlayerController m_player;

    bool m_colorEnabled = false;

    // Current draw/fog distance (usually 16.0), recomputed every frame in render() and used for the
    // shader's renderDistance uniform, the far projection plane and the active-torch selection
    // radius.
    float m_currentRenderDistance = 16.0f;

    Compass m_compass;
    PlayerTorchViewmodel m_playerTorch;

    // The single shared model ("THE WRAPPED"): this member owns its resources, m_testEnemy and
    // m_enemies[] only reference it. Loaded without the diffuse texture (see init()).
    SkinnedModel m_enemySharedModel;
    GLuint m_enemyProgram = 0;
    GLint m_uEnemyView = -1, m_uEnemyProjection = -1, m_uEnemyModel = -1, m_uEnemyBoneMatrices = -1;
    GLint m_uEnemyCamPos = -1, m_uEnemyTime = -1;
    GLint m_uEnemyPlayerLightPos = -1, m_uEnemyPlayerLightDir = -1, m_uEnemyPlayerLightColor = -1, m_uEnemyPlayerLightIntensity = -1;
    GLint m_uEnemyDevLightPos = -1, m_uEnemyDevLightDir = -1, m_uEnemyDevLightCount = -1;
    GLint m_uEnemyDevPaletteOverride = -1;
    GLint m_uEnemyTorchPos = -1, m_uEnemyTorchColor = -1, m_uEnemyTorchIntensity = -1, m_uEnemyTorchCount = -1;
    GLint m_uEnemyMapTex = -1, m_uEnemyColumnPos = -1, m_uEnemyColumnCount = -1, m_uEnemyColumnRadius = -1;
    GLint m_uEnemyRenderDistance = -1;
    GLint m_uEnemyDiffuseTex = -1, m_uEnemyHasDiffuseTex = -1;
    void cacheEnemyUniformLocations();
    EnemyCharacter m_testEnemy; // dev-tools dummy (K key), see spawnDevDummyEnemyAtPlayerView()
    EnemyAI m_enemyAI;          // the dummy's AI state (only position, rotation and Idle are used)

    // Real enemies: independent AI instances (m_enemyAIs[i]), one per map sector, all referencing
    // the shared model. m_testEnemy above is a separate dev dummy that only stands there.
    static constexpr int kEnemyCount = 7;
    std::array<EnemyCharacter, kEnemyCount> m_enemies;
    std::array<EnemyAI, kEnemyCount> m_enemyAIs;

    // Whether the PLAYER can see an enemy (for the minimap marker): FOV cone, direct LOS with the
    // camera as the observer, sane range. It raycasts, so it runs on the AI perception timer
    // (kPerceptionInterval) instead of every frame.
    bool isEnemyVisibleToPlayer(const glm::vec3& enemyPos) const;

    // Independent timer and cached result per enemy (see isEnemyVisibleToPlayer()); the cache has
    // to be per enemy anyway, so the timer lives next to it.
    std::array<float, kEnemyCount> m_playerVisibilityCheckTimers{};
    std::array<bool, kEnemyCount> m_cachedPlayerCanSeeEnemy{};

    // Spreads kEnemyCount enemies over separate, non-overlapping grid sectors. The sector grid is
    // derived from kEnemyCount. Called once per new map/new game, after geometry generation.
    void spawnEnemiesAcrossMap(uint32_t seed);

    // Dev tools (src/dev/DevTools.h): L spawns a static spotlight at the player, K spawns or
    // repositions a single enemy dummy (m_testEnemy, no AI), U undoes the last L/K
    // (m_devActionHistory), P cycles the environment palette (m_devPaletteOverride).

    struct DevSpotlight
    {
        glm::vec3 position;
        glm::vec3 direction;
    };
    // The limit of 8 matches devLightPos[8]/devLightDir[8] in the shaders;
    // spawnDevLightAtPlayerView() silently ignores calls past it.
    std::vector<DevSpotlight> m_devSpotlights;

    bool m_devDummyEnemyActive = false;

    enum class DevActionType { SpawnLight, SpawnDummyEnemy };
    struct DevAction
    {
        DevActionType type;
        // Only for SpawnDummyEnemy: whether the dummy was active before. Undo just restores that
        // state, not its exact previous position: this is a dev tool, not a full undo system.
        bool dummyWasActiveBefore = false;
    };
    std::vector<DevAction> m_devActionHistory;

    bool m_devLightKeyWasDown = false;
    bool m_devEnemyKeyWasDown = false;
    bool m_devUndoKeyWasDown = false;

    void spawnDevLightAtPlayerView();
    void spawnDevDummyEnemyAtPlayerView();
    void undoLastDevAction();

    // Environment palette cycling (P key), see the devPaletteOverride uniform in scene.frag. -1 =
    // off (the regular baked zone palette); 0..Zoning::kPaletteCount-1 forces one palette onto all
    // visible geometry, without zone transitions: a "view the whole scene in this palette" tool.
    int m_devPaletteOverride = -1;
    bool m_devPaletteKeyWasDown = false;
    void cycleDevPalette();

    double m_lastEnemyUpdateTime = 0.0;
    bool m_enemyUpdateInit = false;

    // Hand torch sway (uViewmodelSway in scene.vert): yaw smoothing so the torch lags behind a
    // sharp turn and catches up, in plain scalar arithmetic (see render()). Initialized to the
    // current yaw on the first frame (m_playerTorchLagInit) so it does not jump.
    float m_playerTorchLagYaw = 0.0f;
    bool m_playerTorchLagInit = false;
    double m_playerTorchLastSwayTime = 0.0;
    // Second smoothing stage: clamping (kMaxSway) bounds the sway but not its rate, so this
    // continuous offset chases the raw target to avoid jumps on sharp mouse moves.
    float m_playerTorchSwayVisual = 0.0f;

    // Walk/run bob (see render()): a phase accumulator separate from the camera's
    // PlayerController::m_bobPhase, so the torch's frequency can be tuned independently.
    float m_playerTorchBobPhase = 0.0f;
    float m_playerTorchMoveBlend = 0.0f;

    glm::vec3 getFront() const;

    bool isWall(int x, int z) const;
    bool isFloor(int x, int z) const;
    // Whether this wall cell's corner is chamfered (CornerCut::None for a regular cell, a non-wall
    // or out of grid); see m_wallCornerCuts.
    WallShapes::CornerCut wallCornerCut(int x, int z) const;
    // Chamfer size for this cell (see m_chamferSizes); meaningless for cells with no data, callers
    // check wallCornerCut() != None first.
    float wallChamferSize(int x, int z) const;

    // The seed deterministically produces the same maze and torch layout (the same seed goes to
    // placeTorches()).
    void generateMap(unsigned int seed);
    void placeTorches(unsigned int seed);

    // Shared by newGame()/loadSlot()/init(): generates the map for a seed, places torches and
    // rebuilds GL geometry and PVS. It does not touch the player, the slot or the fog of war:
    // callers decide that.
    void loadMapAndGeometry(unsigned int seed);

    // Shared by newGame()/generateMenuBackgroundMaze(): map and GL geometry regeneration plus a
    // full player reset, with no interaction with the save system.
    void generateFreshMapAndPlayer();

    // The slot the session is bound to (-1 until newGame() or loadSlot() has run; the menu
    // background does not bind one) and the seed of the current map, kept for saveActiveSlot();
    // neither changes until newGame()/loadSlot() runs.
    int m_activeSlot = -1;
    unsigned int m_currentSeed = 0;

    // Current save name (see saveToSlot()): typed on an explicit SAVE, or empty if the session
    // started via NEW GAME and was not saved manually (the slot picker then shows a generic label).
    // loadSlot() copies it from the file so later autosaves keep it.
    std::string m_saveName;

    GLuint compileShader(GLenum type, const char* src);
    GLuint linkProgram(GLuint vs, GLuint fs);

};
