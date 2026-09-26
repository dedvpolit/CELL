#pragma once
#include <vector>
#include <string>
#include "TitleBreakup.h"

// Assembles complete menu screens (main menu, pause, settings, slot pickers, confirmation, name
// entry) from TextGrid, BigFont, Atmosphere and TitleBreakup into a glyph grid plus button
// rectangles for Application.

namespace MainMenu {

struct ButtonRect { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };

struct Layout {
    std::vector<unsigned char> grid;
    ButtonRect newGameButton;  // opens the slot picker for a new game (AppState::NEWGAME_SELECT)
    ButtonRect continueButton; // opens the load slot picker (see SlotMenuLayout below)
    ButtonRect settingsButton;
    ButtonRect exitButton;
    // Exact bounds of the "CELL" text: the origin for ApplyTitleClick()/DrawBrokenTitle(). Do not
    // change it, or the particle scatter desyncs from the drawn letters.
    ButtonRect titleRect;
    ButtonRect titleBoxRect;
};

// Same as Layout, but for the pause menu, with separate field names so "quit the game"
// (Layout::exitButton, main menu) cannot be confused with "return to the main menu"
// (PauseLayout::menuButton, pause menu).
struct PauseLayout {
    std::vector<unsigned char> grid;
    ButtonRect resumeButton;
    ButtonRect saveButton;     // opens AppState::SAVE_SELECT (see SlotMenuLayout below)
    ButtonRect settingsButton;
    ButtonRect menuButton;
};

// Slot picker used for CONTINUE, SAVE and NEW GAME: the same "title + 3 slots + BACK" layout; the
// caller decides the click behavior (LOAD ignores empty slots; SAVE and NEW GAME accept them and
// ask to confirm overwriting a filled one).
struct SlotMenuLayout {
    std::vector<unsigned char> grid;
    ButtonRect slotButtons[3];
    ButtonRect backButton;

    bool slotFilled[3] = { false, false, false };
};

// A YES/NO confirmation screen (currently only "overwrite this slot?"): a multi-line message
// instead of a giant title, since a whole phrase does not fit the BigFont title style (a title is
// one word like "SAVE").
struct ConfirmLayout {
    std::vector<unsigned char> grid;
    ButtonRect yesButton;
    ButtonRect noButton;
};

// Name entry screen: only displays the letters typed so far (up to SaveSystem::kNameMaxLen); the
// buffer and typing live in Application.cpp. backButton cancels; ENTER or confirmButton confirms.
struct NameEntryLayout {
    std::vector<unsigned char> grid;
    ButtonRect backButton;
    ButtonRect confirmButton;
};

// The settings screen layout, opened by the SETTINGS button from the main menu and from pause
// (Application tracks where to return, m_settingsReturnState).
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
};

// Shared "title + N stacked buttons" layout for the main menu and pause. selection is the hovered
// button (-1 = none); seed is fixed per session so ragged frames do not rebuild on hover;
// skipTitleDraw bakes everything except the title so Build() can cache it; leftAligned presses the
// title and buttons to the left (main menu), otherwise they are centered.
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
                      // Size multiplier for the text of the buttons and, if present, of the title;
                      // frames keep the regular size. 1.0 for the main menu/pause, kCompactScale
                      // for the slot, confirm and name screens.
                      float buttonScale = 1.0f,
                      bool leftAligned = false);

// The main menu: "CELL" plus NEW GAME / CONTINUE / SETTINGS / EXIT. Caches the expensive part of
// the layout (atmosphere, frames, button text) between frames while the inputs do not change.
void Build(Layout& out, int cols, int rows, int selection, int seed,
           const TitleBreakupState* titleState = nullptr,
           bool titleHovered = false,
           int variantIndex = 0);

PauseLayout BuildPauseMenu(int cols, int rows, int selection, int seed,
                            int variantIndex = 0);

// CONTINUE screen: slotLabels[i] is the ready label, slotFilled[i] whether the slot is occupied.
// selection 0..2 are slots, 3 is BACK. The caller must not select an empty slot; the layout does
// not check.
SlotMenuLayout BuildContinueMenu(int cols, int rows, int selection, int seed,
                                  const std::string slotLabels[3],
                                  const bool slotFilled[3],
                                  int variantIndex = 0);

// The SAVE screen (the same layout as BuildContinueMenu(), another title), opened by pause's SAVE.
// Unlike LOAD, all 3 slots are clickable, including empty ones (an empty slot opens name entry at
// once). Application decides from slotFilled[i] whether to show the overwrite confirmation.
SlotMenuLayout BuildSaveMenu(int cols, int rows, int selection, int seed,
                              const std::string slotLabels[3],
                              const bool slotFilled[3],
                              int variantIndex = 0);

SlotMenuLayout BuildNewGameMenu(int cols, int rows, int selection, int seed,
                                 const std::string slotLabels[3],
                                 const bool slotFilled[3],
                                 int variantIndex = 0);

// The overwrite-confirmation screen: messageLines are drawn in a small font, one per line, above
// the YES/NO buttons. selection: 0 = YES, 1 = NO, -1 = nothing highlighted.
ConfirmLayout BuildConfirmMenu(int cols, int rows, int selection, int seed,
                                const std::vector<std::string>& messageLines,
                                int variantIndex = 0);

// Name entry: currentName is what was typed (underscores fill the rest); maxLen is passed by the
// caller so this layer does not depend on the save module. confirmHovered only controls the frame
// highlight.
NameEntryLayout BuildNameEntryMenu(int cols, int rows, int seed,
                                    const std::string& currentName, int maxLen,
                                    bool backHovered, bool confirmHovered,
                                    int variantIndex = 0);

// Settings screen. sharpness01 is the slider position (converted from the cell size in px);
// sharpnessValue is the px value printed on the panel. music01/master01 are plain 0..1 volumes
// shown as 0..100.
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
                                  int variantIndex = 0);

// Torch/stone/diary count icons above the stamina bar: three framed icons centered horizontally,
// growing upward from bottomRow.
void DrawHudIcons(std::vector<unsigned char>& grid, int cols, int rows,
                   int torchCount, int stoneCount, int diaryCount,
                   int bottomRow, int seed);

} // namespace MainMenu
