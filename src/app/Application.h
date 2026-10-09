#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include "render/AsciiEffect.h"
#include "render/GpuTimer.h"
#include "render/CrtEffect.h"
#include "ui/StoryText.h"
#include "scene/DungeonScene.h"
#include "ui/MainMenu.h"
#include "ui/TextRenderer.h"
#include "WindowManager.h"
#include "audio/AudioMixer.h"

// App state machine (menus, pause, settings, fades) and frame composition
class Application {
public:
    void init(GLFWwindow* window, DungeonScene& scene, AsciiEffect& ascii);
    void shutdown() { m_gpuTimer.shutdown(); m_crt.shutdown(); }

    // While minimized no frames are drawn;
    // restart the [perf] second so it does not report a stall
    void restartPerfCounter() { m_perfLastTime = -1.0; m_perfFrameCount = 0; }

    void tick(GLFWwindow* window, DungeonScene& scene, AsciiEffect& ascii,
              WindowManager& windowManager, float deltaTime);

    // True during FADE_TO_GAME/PLAYING;
    // main.cpp toggles the cursor with it
    bool isMouseLookEnabled() const { return m_mouseLookEnabled; }

private:
    GpuTimer m_gpuTimer;

    StoryText m_storyText;
    bool m_storyLeadsToCredits = false;
    // Holding Space for kStorySkipHoldSeconds skips the text;
    // the hint fills with '#'
    static constexpr float kStorySkipHoldSeconds = 2.0f;
    float m_storySkipHold = 0.0f;
    bool m_storySkipArmed = false;
    std::vector<unsigned char> m_storyGrid;
    void startStory(bool leadsToCredits);

    // Whatever the cursor is on in the current menu (-1 = nothing)
    // for the hover/click sounds
    int currentUiHoverId() const;
    int m_uiHoverId = -1;
    double m_perfLastTime = -1.0;
    int m_perfFrameCount = 0;

    TextRenderer m_textRenderer;

    enum class AppState {
        MENU, CONTINUE_SELECT, SETTINGS, SAVE_SELECT, SAVE_CONFIRM, SAVE_NAME_ENTRY,
        // Slot picker and overwrite confirmation for NEW GAME;
        // SAVE_NAME_ENTRY serves both flows
        NEWGAME_SELECT, NEWGAME_CONFIRM,
        FADE_TO_BLACK, FADE_TO_GAME, FADE_TO_MENU, PLAYING, PAUSED,
        // Thank-you text on black
        CREDITS,
        // Text on black between a fade and the next state
        STORY_TEXT
    };

    enum class PendingAction {
        NONE, NEW_GAME, NEW_GAME_IN_SLOT, LOAD_GAME, QUIT_APP, RETURN_TO_MENU, DIED, SHOW_CREDITS
    };

    static float sensitivityToSlider01(float sensitivity);
    static float slider01ToSensitivity(float t);

    // Shared by ENTER and the OK button
    void confirmNameEntry(DungeonScene& scene);
    // Slider mapping for SHARPNESS (cell size in pixels)
    static float cellSizeToSlider01(int cellSize);
    static int slider01ToCellSize(float t);

    bool m_mouseLookEnabled = false;
    bool m_cursorCaptured = false;

    bool m_fKeyWasDown = false;
    bool m_f1KeyWasDown = false;
    bool m_f3KeyWasDown = false;

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

    // Separate from the save screens caches
    MainMenu::SlotMenuLayout m_newGameLayout;
    Difficulty m_selectedDifficulty = Difficulty::Normal;
    bool m_newGameLayoutDirty = true;
    MainMenu::ConfirmLayout m_newGameConfirmLayout;
    bool m_newGameConfirmLayoutDirty = true;
    int m_pendingNewGameSlot = -1; // slot pending overwrite confirmation / new game's name

    // Whether confirming the name starts a new game or saves
    bool m_nameEntryForNewGame = false;

    MainMenu::NameEntryLayout m_nameEntryLayout;
    bool m_nameEntryLayoutDirty = true;
    std::string m_saveNameBuffer;
    bool m_backHoveredNameEntry = false;
    bool m_confirmHoveredNameEntry = false;
    // Edge detection for typing;
    // sized for Enter/Backspace, which lie outside the letter range
    bool m_textEntryKeyWasDown[GLFW_KEY_LAST + 1] = {};

    float m_autosaveTimer = 0.0f;

    // Reused every frame; reallocated only on a size change
    std::vector<unsigned char> m_hudIconsGridScratch;

    // Shared by the gameplay hints; only one shows at a time
    std::vector<unsigned char> m_hintGridScratch;

    std::vector<unsigned char> m_diaryReadingGridScratch;

    // Credits scroll with UP/DOWN
    float m_creditsScrollPx = 0.0f;
    bool m_creditsUpKeyWasDown = false;
    bool m_creditsDownKeyWasDown = false;

    // Wrapped once on entering the credits
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

    // Pushed to AudioMixer every frame in tick()
    float m_musicVolume = 1.0f;
    float m_masterVolume = 1.0f;
    bool m_musicSliderHovered = false;
    bool m_musicSliderDragging = false;
    bool m_masterSliderHovered = false;
    bool m_masterSliderDragging = false;

    bool m_colorEnabled = true; // on by default so COLOR and LENS are already enabled at launch
    bool m_lensEnabled = true;
    bool m_lensCheckboxHovered = false;

    // SHADERS appears in settings once the title has been knocked down (this session only)
    // Off renders the plain 3D scene instead of ASCII
    bool m_shadersUnlocked = false;
    bool m_shadersEnabled = true;
    bool m_shadersCheckboxHovered = false;

    CrtEffect m_crt;
    bool m_crtEnabled = true;
    bool m_crtCheckboxHovered = false;
    bool m_colorCheckboxHovered = false;

    bool m_confirmKeyWasDown = false;

    float m_fadeAlpha = 0.0f;
    static constexpr float kFadeOutSpeed = 1.2f;
    // Death fades at half the normal speed.
    static constexpr float kDeathFadeOutSpeed = 0.6f;
    static constexpr float kFadeInSpeed = 0.5f;
};
