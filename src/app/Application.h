#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include "render/AsciiEffect.h"
#include "scene/DungeonScene.h"
#include "ui/MainMenu.h"
#include "ui/TextRenderer.h"
#include "WindowManager.h"
#include "audio/AudioMixer.h"

// App-level state machine (menu/pause/settings/fades) and frame composition. main.cpp is only
// bootstrap plus one app.tick(...) call per frame.
class Application {
public:
    void init(GLFWwindow* window, DungeonScene& scene, AsciiEffect& ascii);

    void tick(GLFWwindow* window, DungeonScene& scene, AsciiEffect& ascii,
              WindowManager& windowManager, float deltaTime);

    // True while actual gameplay is running (FADE_TO_GAME/PLAYING); main.cpp uses it to toggle the
    // mouse-look callback and the GLFW cursor mode.
    bool isMouseLookEnabled() const { return m_mouseLookEnabled; }

private:
    TextRenderer m_textRenderer;

    enum class AppState {
        MENU, CONTINUE_SELECT, SETTINGS, SAVE_SELECT, SAVE_CONFIRM, SAVE_NAME_ENTRY,
        // The same slot-picker and overwrite-confirm screens as SAVE_SELECT/SAVE_CONFIRM, for the
        // main menu's NEW GAME (see m_newGameLayout); SAVE_NAME_ENTRY serves both flows (see
        // m_nameEntryForNewGame).
        NEWGAME_SELECT, NEWGAME_CONFIRM,
        FADE_TO_BLACK, FADE_TO_GAME, FADE_TO_MENU, PLAYING, PAUSED,
        // Credits (see PendingAction::SHOW_CREDITS): the thank-you text over a black background
        // (the camera is moved out of the map, see the CREDITS branch in tick()). The only way out
        // is ESC, which closes the game.
        CREDITS
    };

    enum class PendingAction {
        // NEW_GAME/LOAD_GAME run once the screen is fully black (FADE_TO_BLACK), hiding the heavy
        // map/GL regeneration. NEW_GAME_IN_SLOT: slot and name were picked before the fade
        // (NEW_GAME is the fallback that lets DungeonScene::newGame() choose). DIED: player death;
        // unlike RETURN_TO_MENU (which assumes an open pause menu) it has no overlay and generates
        // a fresh map. SHOW_CREDITS: E at the donut, then AppState::CREDITS. SAVE needs no
        // PendingAction: the write is instant.
        NONE, NEW_GAME, NEW_GAME_IN_SLOT, LOAD_GAME, QUIT_APP, RETURN_TO_MENU, DIED, SHOW_CREDITS
    };

    static float sensitivityToSlider01(float sensitivity);
    static float slider01ToSensitivity(float t);

    // Shared confirm logic for the name entry screen (AppState::SAVE_NAME_ENTRY), called from both
    // the ENTER key and the OK click so the default-name and NEW GAME/SAVE branching is not
    // duplicated.
    void confirmNameEntry(DungeonScene& scene);
    // The same "value <-> slider position 0..1" mapping as
    // sensitivityToSlider01/slider01ToSensitivity, for the SHARPNESS slider (ASCII cell size in
    // pixels): a separate pair to keep the units from getting mixed up.
    static float cellSizeToSlider01(int cellSize);
    static int slider01ToCellSize(float t);

    bool m_mouseLookEnabled = false;
    bool m_cursorCaptured = false;

    bool m_fKeyWasDown = false;
    bool m_f1KeyWasDown = false;

    bool m_appliedCinematicBoost = false;
    int m_currentSceneW = 0, m_currentSceneH = 0;
    int m_normalSceneW = 1280, m_normalSceneH = 720;

    AppState m_appState = AppState::MENU;
    PendingAction m_pendingAction = PendingAction::NONE;
    int m_hoveredButton = -1;
    bool m_titleHovered = false;
    bool m_menuLayoutDirty = true;
    int m_menuSeed = 1917;

    int m_menuOpenCount = 0;
    int m_menuVariant = 0;
    MainMenu::Layout m_menuLayout;
    MainMenu::TitleBreakupState m_titleBreakup;

    MainMenu::PauseLayout m_pauseLayout;
    bool m_pauseLayoutDirty = true;
    bool m_escKeyWasDown = false;
    int m_pauseOpenCount = 0;
    int m_pauseVariant = 0;

    MainMenu::SlotMenuLayout m_continueLayout;
    bool m_continueLayoutDirty = true;
    int m_pendingLoadSlot = -1; // slot to load once FADE_TO_BLACK finishes

    MainMenu::SlotMenuLayout m_saveLayout;
    bool m_saveLayoutDirty = true;
    MainMenu::ConfirmLayout m_saveConfirmLayout;
    bool m_saveConfirmLayoutDirty = true;
    int m_pendingSaveSlot = -1; // slot pending overwrite confirmation / name entry

    // NEW GAME has its own layouts and dirty flags instead of reusing the save ones: both screens
    // can appear back to back with different titles and a shared cache would overwrite itself.
    MainMenu::SlotMenuLayout m_newGameLayout;
    bool m_newGameLayoutDirty = true;
    MainMenu::ConfirmLayout m_newGameConfirmLayout;
    bool m_newGameConfirmLayoutDirty = true;
    int m_pendingNewGameSlot = -1; // slot pending overwrite confirmation / new game's name

    // The name entry serves both SAVE (from pause) and NEW GAME (from the main menu); this flag
    // decides whether confirming starts a new game in m_pendingNewGameSlot (NEW_GAME_IN_SLOT) or
    // overwrites m_pendingSaveSlot.
    bool m_nameEntryForNewGame = false;

    MainMenu::NameEntryLayout m_nameEntryLayout;
    bool m_nameEntryLayoutDirty = true;
    std::string m_saveNameBuffer;
    bool m_backHoveredNameEntry = false;
    bool m_confirmHoveredNameEntry = false;
    // Edge-trigger table for the alphanumeric keys plus Enter/Backspace, polled only in
    // AppState::SAVE_NAME_ENTRY. Sized GLFW_KEY_LAST + 1 because GLFW_KEY_ENTER (257) and
    // GLFW_KEY_BACKSPACE (259) lie outside the A-Z/0-9 range (65-90, 48-57).
    bool m_textEntryKeyWasDown[GLFW_KEY_LAST + 1] = {};

    // Autosave of the run's progress (DungeonScene::saveActiveSlot()), so CONTINUE reflects real
    // progress and not only the start/load moment.
    float m_autosaveTimer = 0.0f;

    // Reusable buffer instead of rebuilding the icon vector every frame (like DungeonScene's
    // scratch vectors). Reallocated only when the screen size changes.
    std::vector<unsigned char> m_hudIconsGridScratch;

    // Shared reusable buffer for all six gameplay hints
    // (DIARY_HINT/TORCH_HINT/TORCH_EMPTY_MSG/WIN_BLOCKED_MSG/WIN_ACTIVATE_HINT/WIN_READY_MSG): they
    // are shown one at a time, so one buffer serves them all.
    std::vector<unsigned char> m_hintGridScratch;

    // A separate reusable buffer for the diary-reading screen: a persistent member, not a local
    // vector in tick().
    std::vector<unsigned char> m_diaryReadingGridScratch;

    // Credits (AppState::CREDITS, see PendingAction::SHOW_CREDITS). The text is long and does not
    // fit at once: UP/DOWN scrolling, like the journal (DungeonScene::tickReadingOverlayInput()),
    // but as its own AppState-level screen instead of a gameplay overlay.
    float m_creditsScrollPx = 0.0f;
    bool m_creditsUpKeyWasDown = false;
    bool m_creditsDownKeyWasDown = false;

    // The line-wrapped credits text does not change while the screen is open, only the visible line
    // range (scroll) does. It is built once on entering the screen (see m_creditsLinesBuilt).
    std::vector<std::string> m_creditsLinesCache;
    bool m_creditsLinesBuilt = false;
    float m_creditsLinesCachedWidth = -1.0f; // rebuild if window width changes
    static constexpr float kAutosaveIntervalSeconds = 20.0f;

    AppState m_settingsReturnState = AppState::MENU;
    MainMenu::SettingsLayout m_settingsLayout;
    bool m_settingsLayoutDirty = true;
    bool m_backHovered = false;
    bool m_sliderHovered = false;
    bool m_sliderDragging = false;
    bool m_sharpnessSliderHovered = false;
    bool m_sharpnessSliderDragging = false;

    // MUSIC/MASTER sliders (same hover/drag pattern as SENSITIVITY/SHARPNESS). The values live here
    // because they belong to AudioMixer; they are pushed to it once per frame in tick().
    float m_musicVolume = 1.0f;
    float m_masterVolume = 1.0f;
    bool m_musicSliderHovered = false;
    bool m_musicSliderDragging = false;
    bool m_masterSliderHovered = false;
    bool m_masterSliderDragging = false;

    bool m_colorEnabled = true; // on by default so COLOR and LENS are already enabled at launch
    bool m_lensEnabled = true;
    bool m_lensCheckboxHovered = false;
    bool m_colorCheckboxHovered = false;

    bool m_confirmKeyWasDown = false;

    float m_fadeAlpha = 0.0f;
    static constexpr float kFadeOutSpeed = 1.2f;
    // The death fade uses its own, half-speed value instead of kFadeOutSpeed, which the normal menu
    // exit shares (one fade pipeline, see PendingAction). It applies only while m_pendingAction ==
    // DIED.
    static constexpr float kDeathFadeOutSpeed = 0.6f;
    static constexpr float kFadeInSpeed = 0.5f;
};
