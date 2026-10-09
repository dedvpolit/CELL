#pragma once
#include <vector>
#include <string>
#include "TitleBreakup.h"

// Complete menu screens as a glyph grid plus button rectangles for Application

namespace MainMenu {

struct ButtonRect { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };

struct Layout {
    std::vector<unsigned char> grid;
    ButtonRect newGameButton;  // opens the slot picker for a new game (AppState::NEWGAME_SELECT)
    ButtonRect continueButton; // opens the load slot picker (see SlotMenuLayout below)
    ButtonRect settingsButton;
    ButtonRect exitButton;
    // Exact bounds of the title text: the origin of the shatter particles
    ButtonRect titleRect;
    ButtonRect titleBoxRect;
};

// Separate from Layout so "menu" (pause) and "exit" (main menu) cannot be confused
struct PauseLayout {
    std::vector<unsigned char> grid;
    ButtonRect resumeButton;
    ButtonRect saveButton;     // opens AppState::SAVE_SELECT (see SlotMenuLayout below)
    ButtonRect settingsButton;
    ButtonRect menuButton;
};

// Title + 3 slots + BACK, for CONTINUE, SAVE and NEW GAME; the caller decides what clicks do
struct SlotMenuLayout {
    std::vector<unsigned char> grid;
    ButtonRect slotButtons[3];
    ButtonRect backButton;

    bool slotFilled[3] = { false, false, false };
    // New game only: the difficulty picker, indexed by Difficulty
    ButtonRect difficultyButtons[3];
};

// YES/NO confirmation with a multi-line message
struct ConfirmLayout {
    std::vector<unsigned char> grid;
    ButtonRect yesButton;
    ButtonRect noButton;
};

// Name entry: display only; typing lives in Application.cpp
struct NameEntryLayout {
    std::vector<unsigned char> grid;
    ButtonRect backButton;
    ButtonRect confirmButton;
};

// Opened from the main menu and from pause
struct SettingsLayout {
    std::vector<unsigned char> grid;
    ButtonRect backButton;
    ButtonRect sliderPanel; // the framed slider strip, used for hover/drag-start detection
    int trackX0 = 0;        // slider cell corresponding to value 0.0
    int trackX1 = 0;        // slider cell corresponding to value 1.0
    int trackRow = 0;       // the slider line's row (for drawing/hit-testing)

    ButtonRect sharpnessSliderPanel;
    int sharpnessTrackX0 = 0;
    int sharpnessTrackX1 = 0;
    int sharpnessTrackRow = 0;

    ButtonRect musicSliderPanel;
    int musicTrackX0 = 0;
    int musicTrackX1 = 0;
    int musicTrackRow = 0;

    ButtonRect masterSliderPanel;
    int masterTrackX0 = 0;
    int masterTrackX1 = 0;
    int masterTrackRow = 0;

    ButtonRect colorCheckbox;

    ButtonRect lensCheckbox;

    ButtonRect crtCheckbox;

    // Only present once unlocked (the title was knocked down); otherwise empty
    bool hasShadersCheckbox = false;
    ButtonRect shadersCheckbox;
};

// Title + N stacked buttons. selection = hovered button (-1 none);
// seed is fixed per session -> ragged frames do not change on hover
void BuildButtonMenu(std::vector<unsigned char>& grid, int cols, int rows,
                      int selection, int seed,
                      const std::string& title,
                      const std::vector<std::string>& buttonTexts,
                      std::vector<ButtonRect>& outButtons,
                      ButtonRect& outTitle,
                      ButtonRect& outTitleBox,
                      const TitleBreakupState* titleState = nullptr,
                      bool pauseMenu = false,
                      bool titleHovered = false,
                      int atmosphereVariant = 0,
                      bool skipTitleDraw = false,
                      // Text scale of buttons and title; frames keep their size
                      float buttonScale = 1.0f,
                      bool leftAligned = false,
                      // Kept free of atmosphere decoration in addition to the title and buttons
                      const ButtonRect* reservedArea = nullptr);

// Main menu
void Build(Layout& out, int cols, int rows, int selection, int seed,
           const TitleBreakupState* titleState = nullptr,
           bool titleHovered = false,
           int variantIndex = 0);

PauseLayout BuildPauseMenu(int cols, int rows, int selection, int seed,
                            int variantIndex = 0);

// slotLabels/slotFilled per slot;
// selection 0..2 = slots
// 3 = BACK
SlotMenuLayout BuildContinueMenu(int cols, int rows, int selection, int seed,
                                  const std::string slotLabels[3],
                                  const bool slotFilled[3],
                                  int variantIndex = 0);

// Same layout titled SAVE
SlotMenuLayout BuildSaveMenu(int cols, int rows, int selection, int seed,
                              const std::string slotLabels[3],
                              const bool slotFilled[3],
                              int variantIndex = 0);

// Slot picker plus a difficulty picker on the right and the shown difficulty's goal and debuffs on the left
// The hovered difficulty (-1 none) is described instead of the selected one
SlotMenuLayout BuildNewGameMenu(int cols, int rows, int selection, int seed,
                                 const std::string slotLabels[3],
                                 const bool slotFilled[3],
                                 int variantIndex,
                                 int selectedDifficulty,
                                 int hoveredDifficulty);

// selection:
// 0 = YES
// 1 = NO
// -1 = none
ConfirmLayout BuildConfirmMenu(int cols, int rows, int selection, int seed,
                                const std::vector<std::string>& messageLines,
                                int variantIndex = 0);

// currentName padded with underscores up to maxLen
NameEntryLayout BuildNameEntryMenu(int cols, int rows, int seed,
                                    const std::string& currentName, int maxLen,
                                    bool backHovered, bool confirmHovered,
                                    int variantIndex = 0);

// sharpness01 is the slider position;
// sharpnessValue the cell size shown on it
// Volumes are 0..1, shown as 0..100
SettingsLayout BuildSettingsMenu(int cols, int rows, int seed,
                                  float sensitivity01,
                                  float sharpness01,
                                  int sharpnessValue,
                                  float music01,
                                  float master01,
                                  bool backHovered,
                                  bool sliderHovered,
                                  bool sharpnessSliderHovered,
                                  bool musicSliderHovered,
                                  bool masterSliderHovered,
                                  bool colorEnabled,
                                  bool colorCheckboxHovered,
                                  bool lensEnabled,
                                  bool lensCheckboxHovered,
                                  bool crtEnabled,
                                  bool crtCheckboxHovered,
                                  bool shadersUnlocked,
                                  bool shadersEnabled,
                                  bool shadersCheckboxHovered,
                                  int variantIndex = 0);

// Item counters above the stamina bar, growing upward from bottomRow
void DrawHudIcons(std::vector<unsigned char>& grid, int cols, int rows,
                   int torchCount, int stoneCount, int diaryCount,
                   int bottomRow, int seed);

} // namespace MainMenu
