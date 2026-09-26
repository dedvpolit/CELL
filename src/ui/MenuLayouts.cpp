#include "MenuLayouts.h"
#include "TextGrid.h"
#include "BigFont.h"
#include "Atmosphere.h"
#include "UiGlyphs.h"
#include <algorithm>
#include <cmath>

namespace MainMenu {

// Menu text scale: rows / 60, capped at 1.0 (float division on purpose). The grid always has >=
// kMenuMinGridRows rows, so on regular windows it sits at the cap; the 0.15 floor is only a safety
// net.
static float ComputeMenuBaseScale(int rows) {
    return std::clamp((float)rows / 60.0f, 0.15f, 1.0f);
}

// Shrinks the font by one whole finalRes step (used for buttonScale in BuildButtonMenu and for the
// confirm message). If the font is already at its floor (finalRes == 1) it returns the original
// scale: faking the difference with a thinner frame or margin elsewhere was tried and looked worse.
static float ShrinkTextScale(float baseTextScale, float scaleMultiplier) {
    if (scaleMultiplier >= 1.0f) return baseTextScale;
    const int normalRes = ComputeFinalRes(baseTextScale);
    int reducedRes = std::max(1, (int)std::lround((double)normalRes * scaleMultiplier));
    if (reducedRes >= normalRes && normalRes > 1) {
        reducedRes = normalRes - 1;
    }
    return (float)reducedRes / (float)kMaskUpsample;
}

// Shared multiplier for the compact screens (save/load slots, overwrite confirmation, name entry):
// applied to the title and the buttons through ShrinkTextScale(), about 20% smaller than the main
// menu and pause.
static constexpr float kCompactScale = 0.8f;

// Left-aligned menu (the main menu): the distance of the left edge from the screen edge is
// cols / kLeftMarginDiv cells, but at least kLeftMarginMin.
static constexpr int kLeftMarginDiv = 16;
static constexpr int kLeftMarginMin = 6;


// Shared "title + N stacked buttons" layout for the main menu, pause and slot screens (all buttons
// share one width). selection is the hovered button (others = none); seed is fixed per session so
// ragged frames do not rebuild on hover. skipTitleDraw bakes everything except the title glyphs so
// the caller can cache it (see Build()). leftAligned presses the title frame and buttons to the
// left (main menu); otherwise they are centered.
void BuildButtonMenu(std::vector<unsigned char>& grid, int cols, int rows,
                             int selection, int seed,
                             const std::string& title,
                             const std::vector<std::string>& buttonTexts,
                             std::vector<ButtonRect>& outButtons,
                             ButtonRect& outTitle,
                             ButtonRect& outTitleBox,
                             const TitleBreakupState* titleState,
                             bool pauseMenu,
                             bool titleHovered,
                             int atmosphereVariant,
                             bool skipTitleDraw,
                             float buttonScale,
                             bool leftAligned) {
    grid.assign((size_t)std::max(0, cols) * std::max(0, rows), 0);
    outButtons.assign(buttonTexts.size(), ButtonRect{});
    outTitle = ButtonRect{};
    outTitleBox = ButtonRect{};

    if (cols <= 0 || rows <= 0 || buttonTexts.empty()) return;

    // Title. It can be empty (pause has none): the title block then takes no space and only the
    // buttons are centered. The atmosphere (frame, cracks, rune, torches) is drawn further below,
    // once the buttons and outTitle are known, because it needs the real content rectangle.
    const bool hasTitle = !title.empty();

    // baseScale is an adaptive step for the screen size. The title scale (larger) and the button
    // scale (much smaller) derive from it separately, so buttons do not look almost as big as the
    // title.
    const float baseScale = ComputeMenuBaseScale(rows);

    // buttonScale shrinks both the title ("SAVE"/"LOAD") and the buttons; the slot, confirm and
    // name screens pass kCompactScale. The main menu and pause keep the default 1.0.
    const float titleScaleBase = baseScale * 1.5f;
    const float titleScale = ShrinkTextScale(titleScaleBase, buttonScale);
    const int letterH = hasTitle ? BigGlyphHeight(titleScale) : 0;
    const int titleWidth = hasTitle ? BigTextWidth(title, titleScale) : 0;

    // buttonScale shrinks only the text; frame thickness and padding stay regular so screens match.
    // At 1280x720 the button font is already at its floor (finalRes = 1).
    const int buttonBorderThickness = 2;
    const int buttonInnerPadding = 1; // padding between text and frame inside a button

    // The title frame extends titleInnerPadding + border beyond the letters, so its height is
    // letterH + 2 * titleOverhang; that must be counted in contentHeight/contentTop or the frame
    // top clips at row < 0.
    const int titleInnerPadding = 2;
    const int titleOverhang = hasTitle ? (titleInnerPadding + buttonBorderThickness) : 0;

    // buttonGap/titleToButtonsGap are not const: if the block still does not fit after the button
    // font reached its floor, a second pass tightens the gaps.
    int buttonGap = pauseMenu ? 4 : 3;
    int titleToButtonsGap = hasTitle ? std::max(4, rows / 10) : 0;
    const int buttonCount = (int)buttonTexts.size();

    // Auto-fit: shrink the button font one finalRes step at a time until the block fits with a
    // margin; the title height is untouched.
    float buttonTextScale = ShrinkTextScale(baseScale * 0.5f, buttonScale);
    int buttonTextAreaW = 0, buttonTextAreaH = 0, buttonW = 0, buttonH = 0, contentHeight = 0;

    auto recomputeButtonMetrics = [&]() {
        buttonTextAreaW = 0;
        for (const std::string& text : buttonTexts) {
            buttonTextAreaW = std::max(buttonTextAreaW, BigTextWidth(text, buttonTextScale));
        }
        buttonTextAreaH = BigGlyphHeight(buttonTextScale);
        buttonW = buttonTextAreaW + buttonInnerPadding * 2 + buttonBorderThickness * 2;
        buttonH = buttonTextAreaH + buttonInnerPadding * 2 + buttonBorderThickness * 2;
        contentHeight = (letterH + 2 * titleOverhang) + titleToButtonsGap +
            buttonCount * buttonH + std::max(0, buttonCount - 1) * buttonGap;
    };
    recomputeButtonMetrics();

    const int targetHeight = std::max(1, rows - std::max(2, rows / 30));
    for (int guard = 0; guard < 6 && contentHeight > targetHeight; ++guard) {
        const float smaller = ShrinkTextScale(buttonTextScale, 0.99f);
        if (smaller >= buttonTextScale) break;
        buttonTextScale = smaller;
        recomputeButtonMetrics();
    }

    // Second pass when the font is at its floor: squeeze the gaps (buttons floor 1, under the title
    // floor 3); frame and padding stay to match the other screens.
    for (int guard = 0; guard < 12 && contentHeight > targetHeight; ++guard) {
        const int minButtonGap = 1;
        const int minTitleGap = hasTitle ? 3 : 0;
        bool shrunk = false;
        if (buttonGap > minButtonGap) { --buttonGap; shrunk = true; }
        else if (titleToButtonsGap > minTitleGap) { --titleToButtonsGap; shrunk = true; }
        if (!shrunk) break;
        recomputeButtonMetrics();
    }
    const int contentShiftY = pauseMenu ? -1 : 0;
    const int contentTop = std::max(1, (rows - contentHeight) / 2 + contentShiftY);

    // Horizontal placement: centered, or (main menu) pressed to the left. The title frame is the
    // widest element, so the shared left edge is clamped to keep the whole frame inside the grid
    // on narrow windows. The title letters sit titleOverhang inside the frame's left edge.
    const int widestW = hasTitle ? (titleWidth + 2 * titleOverhang) : buttonW;
    const int leftEdge = std::min(std::max(kLeftMarginMin, cols / kLeftMarginDiv),
                                  std::max(0, cols - widestW));
    const int titleCol = leftAligned ? leftEdge + titleOverhang
                                     : std::max(0, (cols - titleWidth) / 2);
    // titleRow is offset by titleOverhang so the frame above the letters does not start above
    // contentTop and get clipped.
    const int titleRow = contentTop + titleOverhang;

    if (hasTitle) {
        outTitle = ButtonRect{
            titleCol,
            titleRow,
            titleCol + std::max(0, titleWidth - 1),
            titleRow + std::max(0, letterH - 1)
        };

        // Title frame: more padding around the letters than buttons get (titleInnerPadding 2 vs 1),
        // since the text is much bigger. Same frame thickness as the buttons, for one visual style.
        outTitleBox = ButtonRect{
            outTitle.x0 - titleInnerPadding - buttonBorderThickness,
            outTitle.y0 - titleInnerPadding - buttonBorderThickness,
            outTitle.x1 + titleInnerPadding + buttonBorderThickness,
            outTitle.y1 + titleInnerPadding + buttonBorderThickness
        };
    }

    const int buttonCol = leftAligned ? leftEdge : std::max(0, (cols - buttonW) / 2);

    std::vector<ButtonRect> buttons(buttonCount);
    int nextY0 = titleRow + letterH + titleOverhang + titleToButtonsGap;
    for (int i = 0; i < buttonCount; ++i) {
        buttons[i] = ButtonRect{ buttonCol, nextY0, buttonCol + buttonW - 1, nextY0 + buttonH - 1 };
        nextY0 = buttons[i].y1 + 1 + buttonGap;
    }
    outButtons = buttons;

    // Draw the atmosphere around the content bbox (title frame unioned with the buttons, including
    // their padding and frame). It is drawn before the buttons so they overwrite any decoration
    // that still reaches them on tiny windows.
    AtmosphereBounds contentBounds;
    contentBounds.x0 = hasTitle ? std::min(outTitleBox.x0, buttons.front().x0) : buttons.front().x0;
    contentBounds.y0 = hasTitle ? outTitleBox.y0 : buttons.front().y0;
    contentBounds.x1 = hasTitle ? std::max(outTitleBox.x1, buttons.front().x1) : buttons.front().x1;
    contentBounds.y1 = buttons.back().y1;
    DrawDarkFantasyAtmosphere(grid, cols, rows, seed, pauseMenu, contentBounds, atmosphereVariant);

    if (hasTitle) {
        DrawBox(grid, cols, rows, outTitleBox.x0, outTitleBox.y0, outTitleBox.x1, outTitleBox.y1,
                buttonBorderThickness, titleHovered, seed + 2000);
    }

    auto centerBigText = [&](const ButtonRect& b, const std::string& text) {
        const int textW = BigTextWidth(text, buttonTextScale);
        const int textCol = b.x0 + (buttonW - textW) / 2;
        const int textRow = b.y0 + (buttonH - buttonTextAreaH) / 2;
        DrawBigText(grid, cols, rows, text, textCol, textRow, buttonTextScale, seed + 1000);
    };

    for (int i = 0; i < buttonCount; ++i) {
        DrawBox(grid, cols, rows, buttons[i].x0, buttons[i].y0, buttons[i].x1, buttons[i].y1,
                buttonBorderThickness, selection == i, seed + i);
        centerBigText(buttons[i], buttonTexts[(size_t)i]);
    }

    // The title is drawn after the buttons: on the third click the sagging text may lie over the
    // first button, and after the fifth the falling title stays on top of every menu element.
    if (hasTitle && !skipTitleDraw) {
        if (titleState) {
            DrawBrokenTitle(grid, cols, rows, title, titleCol, titleRow, titleScale, *titleState);
        } else {
            DrawBigText(grid, cols, rows, title, titleCol, titleRow, titleScale, seed);
        }
    }
}

// The start screen: CELL title with NEW GAME / CONTINUE / SETTINGS / EXIT. variantIndex is picked
// once on entering MENU so the composition does not drift on relayout. The base grid (atmosphere,
// buttons) is cached per (cols, rows, selection, seed, titleHovered, variantIndex) in static
// storage (single instance, not thread-safe); each call draws only the title on a copy.
void Build(Layout& out, int cols, int rows, int selection, int seed,
                          const TitleBreakupState* titleState,
                          bool titleHovered,
                          int variantIndex) {
    static int  s_cCols = -1, s_cRows = -1, s_cSelection = -1, s_cSeed = -1, s_cVariant = -1;
    static bool s_cTitleHovered = false;
    static bool s_cacheValid = false;
    static std::vector<unsigned char> s_baseGrid; // atmosphere+buttons, title area blank
    static ButtonRect s_baseNewGame, s_baseContinue, s_baseSettings, s_baseExit, s_baseTitleRect, s_baseTitleBoxRect;

    const bool sigMatches = s_cacheValid &&
        cols == s_cCols && rows == s_cRows && selection == s_cSelection &&
        seed == s_cSeed && variantIndex == s_cVariant && titleHovered == s_cTitleHovered;

    if (!sigMatches) {
        std::vector<ButtonRect> buttons;
        BuildButtonMenu(s_baseGrid, cols, rows, selection, seed,
                         "CELL", { "NEW GAME", "CONTINUE", "SETTINGS", "EXIT" }, buttons,
                         s_baseTitleRect, s_baseTitleBoxRect, nullptr, false, titleHovered,
                         variantIndex, /*skipTitleDraw=*/true, /*buttonScale=*/1.0f,
                         /*leftAligned=*/true);
        s_baseNewGame  = buttons[0];
        s_baseContinue = buttons[1];
        s_baseSettings = buttons[2];
        s_baseExit     = buttons[3];

        s_cCols = cols; s_cRows = rows; s_cSelection = selection; s_cSeed = seed;
        s_cVariant = variantIndex; s_cTitleHovered = titleHovered;
        s_cacheValid = true;
    }

    out.grid = s_baseGrid;
    out.newGameButton  = s_baseNewGame;
    out.continueButton = s_baseContinue;
    out.settingsButton = s_baseSettings;
    out.exitButton      = s_baseExit;
    out.titleRect       = s_baseTitleRect;
    out.titleBoxRect    = s_baseTitleBoxRect;

    if (cols <= 0 || rows <= 0) return;

    const float baseScale = ComputeMenuBaseScale(rows);
    const float titleScale = baseScale * 1.5f;
    if (titleState) {
        DrawBrokenTitle(out.grid, cols, rows, "CELL", out.titleRect.x0, out.titleRect.y0, titleScale, *titleState);
    } else {
        DrawBigText(out.grid, cols, rows, "CELL", out.titleRect.x0, out.titleRect.y0, titleScale, seed);
    }
}

// The pause menu: no title, a rune sigil fills the frame top. variantIndex is picked once on
// entering PAUSED (own counter, pauseOpenCount).
PauseLayout BuildPauseMenu(int cols, int rows, int selection, int seed,
                                    int variantIndex) {
    PauseLayout out;
    ButtonRect unusedTitleRect, unusedTitleBox;
    std::vector<ButtonRect> buttons;
    BuildButtonMenu(out.grid, cols, rows, selection, seed,
                     "", { "RESUME", "SAVE", "SETTINGS", "MENU" }, buttons,
                     unusedTitleRect, unusedTitleBox, nullptr, true, false,
                     variantIndex);
    out.resumeButton   = buttons[0];
    out.saveButton      = buttons[1];
    out.settingsButton = buttons[2];
    out.menuButton      = buttons[3];
    return out;
}

// Shared by the Continue/Save/NewGame menus: an identical "3 slots + BACK" layout differing only in
// the title. The caller prepares the labels; kCompactScale makes the buttons smaller than on the
// main menu.
static SlotMenuLayout BuildSlotMenu(int cols, int rows, int selection, int seed,
                              const char* title,
                              const std::string slotLabels[3],
                              const bool slotFilled[3],
                              int variantIndex) {
    SlotMenuLayout out;
    ButtonRect unusedTitleRect, unusedTitleBox;
    std::vector<ButtonRect> buttons;

    const std::vector<std::string> buttonTexts = {
        slotLabels[0], slotLabels[1], slotLabels[2], "BACK"
    };

    BuildButtonMenu(out.grid, cols, rows, selection, seed,
                     title, buttonTexts, buttons,
                     unusedTitleRect, unusedTitleBox, nullptr, /*pauseMenu=*/true, false,
                     variantIndex, /*skipTitleDraw=*/false, /*buttonScale=*/kCompactScale);

    out.slotButtons[0] = buttons[0];
    out.slotButtons[1] = buttons[1];
    out.slotButtons[2] = buttons[2];
    out.backButton      = buttons[3];
    out.slotFilled[0] = slotFilled[0];
    out.slotFilled[1] = slotFilled[1];
    out.slotFilled[2] = slotFilled[2];
    return out;
}

// The CONTINUE (load) screen, see BuildSlotMenu(). An empty slot is not clickable; the caller
// decides that from SlotMenuLayout::slotFilled, the layout only reports it.
SlotMenuLayout BuildContinueMenu(int cols, int rows, int selection, int seed,
                                  const std::string slotLabels[3],
                                  const bool slotFilled[3],
                                  int variantIndex) {
    return BuildSlotMenu(cols, rows, selection, seed, "LOAD", slotLabels, slotFilled, variantIndex);
}

SlotMenuLayout BuildSaveMenu(int cols, int rows, int selection, int seed,
                              const std::string slotLabels[3],
                              const bool slotFilled[3],
                              int variantIndex) {
    return BuildSlotMenu(cols, rows, selection, seed, "SAVE", slotLabels, slotFilled, variantIndex);
}

SlotMenuLayout BuildNewGameMenu(int cols, int rows, int selection, int seed,
                                 const std::string slotLabels[3],
                                 const bool slotFilled[3],
                                 int variantIndex) {
    return BuildSlotMenu(cols, rows, selection, seed, "NEW", slotLabels, slotFilled, variantIndex);
}

// Overwrite confirmation: does not use BuildButtonMenu(), whose giant-font title cannot hold a
// warning phrase. The message is drawn in a small font with YES/NO below it in one shared frame.
ConfirmLayout BuildConfirmMenu(int cols, int rows, int selection, int seed,
                                const std::vector<std::string>& messageLines,
                                int variantIndex) {
    ConfirmLayout out;
    out.grid.assign((size_t)std::max(0, cols) * std::max(0, rows), 0);
    if (cols <= 0 || rows <= 0 || messageLines.empty()) return out;

    const float baseScale = ComputeMenuBaseScale(rows);

    const float messageScale = ShrinkTextScale(baseScale * 0.5f, kCompactScale);
    const int messageLineH = BigGlyphHeight(messageScale);
    const int messageLineGap = std::max(1, ComputeFinalRes(messageScale));

    int messageBlockW = 0;
    for (const std::string& line : messageLines) {
        messageBlockW = std::max(messageBlockW, BigTextWidth(line, messageScale));
    }
    const int lineCount = (int)messageLines.size();
    const int messageBlockH = lineCount * messageLineH + std::max(0, lineCount - 1) * messageLineGap;

    const int msgBorderThickness = 2;
    const int msgInnerPadding = 2;
    const int msgBoxW = messageBlockW + msgInnerPadding * 2 + msgBorderThickness * 2;
    const int msgBoxH = messageBlockH + msgInnerPadding * 2 + msgBorderThickness * 2;

    // YES/NO buttons: the same arithmetic as BuildButtonMenu() (buttonScale = kCompactScale),
    // assembled manually so the message and the buttons share one atmosphere frame.
    const int buttonBorderThickness = 2;
    const int buttonInnerPadding = 1;
    const float buttonTextScale = ShrinkTextScale(baseScale * 0.5f, kCompactScale);
    const std::vector<std::string> buttonTexts = { "YES", "NO" };

    int buttonTextAreaW = 0;
    for (const std::string& t : buttonTexts) {
        buttonTextAreaW = std::max(buttonTextAreaW, BigTextWidth(t, buttonTextScale));
    }
    const int buttonTextAreaH = BigGlyphHeight(buttonTextScale);
    const int buttonW = buttonTextAreaW + buttonInnerPadding * 2 + buttonBorderThickness * 2;
    const int buttonH = buttonTextAreaH + buttonInnerPadding * 2 + buttonBorderThickness * 2;
    const int buttonGap = 3;
    const int buttonCount = (int)buttonTexts.size();

    const int msgToButtonsGap = std::max(3, rows / 20);
    const int contentHeight = msgBoxH + msgToButtonsGap +
        buttonCount * buttonH + std::max(0, buttonCount - 1) * buttonGap;
    const int contentTop = std::max(1, (rows - contentHeight) / 2);

    const int msgBoxCol = std::max(0, (cols - msgBoxW) / 2);
    const ButtonRect msgBox{ msgBoxCol, contentTop, msgBoxCol + msgBoxW - 1, contentTop + msgBoxH - 1 };

    const int buttonCol = std::max(0, (cols - buttonW) / 2);
    std::vector<ButtonRect> buttons(buttonCount);
    int nextY0 = msgBox.y1 + 1 + msgToButtonsGap;
    for (int i = 0; i < buttonCount; ++i) {
        buttons[i] = ButtonRect{ buttonCol, nextY0, buttonCol + buttonW - 1, nextY0 + buttonH - 1 };
        nextY0 = buttons[i].y1 + 1 + buttonGap;
    }
    out.yesButton = buttons[0];
    out.noButton   = buttons[1];

    AtmosphereBounds contentBounds;
    contentBounds.x0 = std::min(msgBox.x0, buttons.front().x0);
    contentBounds.y0 = msgBox.y0;
    contentBounds.x1 = std::max(msgBox.x1, buttons.front().x1);
    contentBounds.y1 = buttons.back().y1;
    DrawDarkFantasyAtmosphere(out.grid, cols, rows, seed, /*pauseMenu=*/true, contentBounds, variantIndex);

    DrawBox(out.grid, cols, rows, msgBox.x0, msgBox.y0, msgBox.x1, msgBox.y1,
            msgBorderThickness, false, seed + 3000);

    int lineY = msgBox.y0 + msgBorderThickness + msgInnerPadding;
    for (const std::string& line : messageLines) {
        const int lineW = BigTextWidth(line, messageScale);
        const int lineCol = msgBox.x0 + (msgBoxW - lineW) / 2;
        DrawBigText(out.grid, cols, rows, line, lineCol, lineY, messageScale, seed + 4000);
        lineY += messageLineH + messageLineGap;
    }

    auto centerBigText = [&](const ButtonRect& b, const std::string& text) {
        const int textW = BigTextWidth(text, buttonTextScale);
        const int textCol = b.x0 + (buttonW - textW) / 2;
        const int textRow = b.y0 + (buttonH - buttonTextAreaH) / 2;
        DrawBigText(out.grid, cols, rows, text, textCol, textRow, buttonTextScale, seed + 1000);
    };

    for (int i = 0; i < buttonCount; ++i) {
        DrawBox(out.grid, cols, rows, buttons[i].x0, buttons[i].y0, buttons[i].x1, buttons[i].y1,
                buttonBorderThickness, selection == i, seed + i);
        centerBigText(buttons[i], buttonTexts[(size_t)i]);
    }

    return out;
}

// The name entry screen: a regular (large) "NAME" title, below it the typed letters plus underscore
// placeholders up to maxLen, then OK and BACK. Confirming with ENTER is handled entirely in
// Application.cpp; this is only the picture.
NameEntryLayout BuildNameEntryMenu(int cols, int rows, int seed,
                                    const std::string& currentName, int maxLen,
                                    bool backHovered, bool confirmHovered,
                                    int variantIndex) {
    NameEntryLayout out;
    out.grid.assign((size_t)std::max(0, cols) * std::max(0, rows), 0);
    if (cols <= 0 || rows <= 0 || maxLen <= 0) return out;

    const float baseScale = ComputeMenuBaseScale(rows);

    const std::string title = "NAME";
    const float titleScale = ShrinkTextScale(baseScale * 1.5f, kCompactScale);
    const int titleTextW = BigTextWidth(title, titleScale);
    const int titleTextH = BigGlyphHeight(titleScale);

    const int titleBorderThickness = 2;
    const int titleInnerPadding = 2;
    const int titleBoxW = titleTextW + titleInnerPadding * 2 + titleBorderThickness * 2;
    const int titleBoxH = titleTextH + titleInnerPadding * 2 + titleBorderThickness * 2;

    // The typed name, with underscores for the letters not typed yet ("AB___" for maxLen = 5):
    // larger than the buttons but smaller than the title, so the eye follows TITLE -> input ->
    // buttons.
    std::string displayName = currentName;
    while ((int)displayName.size() < maxLen) displayName += '_';
    const float nameScale = ShrinkTextScale(baseScale * 0.9f, kCompactScale);
    const int nameW = BigTextWidth(displayName, nameScale);
    const int nameH = BigGlyphHeight(nameScale);

    const int buttonBorderThickness = 2;
    const int buttonInnerPadding = 1;
    const float buttonTextScale = ShrinkTextScale(baseScale * 0.5f, kCompactScale);
    const std::vector<std::string> buttonTexts = { "OK", "BACK" };

    int buttonTextAreaW = 0;
    for (const std::string& t : buttonTexts) {
        buttonTextAreaW = std::max(buttonTextAreaW, BigTextWidth(t, buttonTextScale));
    }
    const int buttonTextAreaH = BigGlyphHeight(buttonTextScale);
    const int buttonW = buttonTextAreaW + buttonInnerPadding * 2 + buttonBorderThickness * 2;
    const int buttonH = buttonTextAreaH + buttonInnerPadding * 2 + buttonBorderThickness * 2;
    const int buttonGap = 3;
    const int buttonCount = (int)buttonTexts.size();

    const int titleToNameGap = std::max(3, rows / 20);
    const int nameToButtonGap = std::max(3, rows / 20);
    const int contentHeight = titleBoxH + titleToNameGap + nameH + nameToButtonGap +
        buttonCount * buttonH + std::max(0, buttonCount - 1) * buttonGap;
    const int contentTop = std::max(1, (rows - contentHeight) / 2);

    const int titleBoxCol = std::max(0, (cols - titleBoxW) / 2);
    const ButtonRect titleBox{ titleBoxCol, contentTop, titleBoxCol + titleBoxW - 1, contentTop + titleBoxH - 1 };
    const ButtonRect titleTextRect{
        titleBox.x0 + titleBorderThickness + titleInnerPadding,
        titleBox.y0 + titleBorderThickness + titleInnerPadding,
        titleBox.x1 - titleBorderThickness - titleInnerPadding,
        titleBox.y1 - titleBorderThickness - titleInnerPadding
    };

    const int nameCol = std::max(0, (cols - nameW) / 2);
    const int nameRow = titleBox.y1 + 1 + titleToNameGap;
    const ButtonRect nameRect{ nameCol, nameRow, nameCol + std::max(0, nameW - 1), nameRow + std::max(0, nameH - 1) };

    const int buttonCol = std::max(0, (cols - buttonW) / 2);
    std::vector<ButtonRect> buttons(buttonCount);
    int nextY0 = nameRect.y1 + 1 + nameToButtonGap;
    for (int i = 0; i < buttonCount; ++i) {
        buttons[i] = ButtonRect{ buttonCol, nextY0, buttonCol + buttonW - 1, nextY0 + buttonH - 1 };
        nextY0 = buttons[i].y1 + 1 + buttonGap;
    }
    out.confirmButton = buttons[0];
    out.backButton    = buttons[1];

    AtmosphereBounds contentBounds;
    contentBounds.x0 = std::min({ titleBox.x0, nameRect.x0, buttons.front().x0 });
    contentBounds.y0 = titleBox.y0;
    contentBounds.x1 = std::max({ titleBox.x1, nameRect.x1, buttons.front().x1 });
    contentBounds.y1 = buttons.back().y1;
    DrawDarkFantasyAtmosphere(out.grid, cols, rows, seed, /*pauseMenu=*/true, contentBounds, variantIndex);

    DrawBox(out.grid, cols, rows, titleBox.x0, titleBox.y0, titleBox.x1, titleBox.y1,
            titleBorderThickness, false, seed + 2000);
    DrawBigText(out.grid, cols, rows, title, titleTextRect.x0, titleTextRect.y0, titleScale, seed + 4000);
    DrawBigText(out.grid, cols, rows, displayName, nameRect.x0, nameRect.y0, nameScale, seed + 5000);

    const bool buttonHovered[2] = { confirmHovered, backHovered };
    for (int i = 0; i < buttonCount; ++i) {
        DrawBox(out.grid, cols, rows, buttons[i].x0, buttons[i].y0, buttons[i].x1, buttons[i].y1,
                buttonBorderThickness, buttonHovered[i], seed + i);
        const int textW = BigTextWidth(buttonTexts[(size_t)i], buttonTextScale);
        const int textCol = buttons[i].x0 + (buttonW - textW) / 2;
        const int textRow = buttons[i].y0 + (buttonH - buttonTextAreaH) / 2;
        DrawBigText(out.grid, cols, rows, buttonTexts[(size_t)i], textCol, textRow, buttonTextScale, seed + 1000 + i);
    }

    return out;
}

// The settings screen (from the main menu and from pause): no title; rows SENSITIVITY, SHARPNESS,
// MUSIC, MASTER (sliders), COLOR, LENS (checkboxes) start at one shared column; only controls are
// framed, BACK is centered at the bottom. Slider values are already 0..1: this function knows no
// physical units.
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
                                          int variantIndex) {
    SettingsLayout out;
    out.grid.assign((size_t)std::max(0, cols) * std::max(0, rows), 0);
    out.backButton = ButtonRect{};
    out.sliderPanel = ButtonRect{};
    out.sharpnessSliderPanel = ButtonRect{};
    out.musicSliderPanel = ButtonRect{};
    out.masterSliderPanel = ButtonRect{};

    if (cols <= 0 || rows <= 0) return out;

    sensitivity01 = std::clamp(sensitivity01, 0.0f, 1.0f);
    sharpness01 = std::clamp(sharpness01, 0.0f, 1.0f);
    music01 = std::clamp(music01, 0.0f, 1.0f);
    master01 = std::clamp(master01, 0.0f, 1.0f);

    const bool pauseMenu = true; // the same compact atmosphere style as pause (no torch at the bottom)

    const float baseScale = ComputeMenuBaseScale(rows);
    const float smallScale = baseScale * 0.5f;

    const int borderThickness = 2;
    const int innerPadding = 1;

    const int topMargin = std::max(6, rows / 14);
    const int leftMargin = std::max(6, cols / 18);

    const std::string sliderLabel = "SENSITIVITY";
    const int labelW = BigTextWidth(sliderLabel, smallScale);
    const int labelH = BigGlyphHeight(smallScale);

    // All sliders and checkboxes align at one column (SHARPNESS under SENSITIVITY, LENS under
    // COLOR): the labels are measured up front and every control starts at the column set by the
    // widest one (usually SENSITIVITY), not after its own label.
    const std::string sharpnessLabel = "SHARPNESS";
    const int sharpnessLabelW = BigTextWidth(sharpnessLabel, smallScale);
    const int sharpnessLabelH = BigGlyphHeight(smallScale);

    const std::string musicLabel = "MUSIC";
    const int musicLabelW = BigTextWidth(musicLabel, smallScale);
    const int musicLabelH = BigGlyphHeight(smallScale);

    const std::string masterLabel = "MASTER";
    const int masterLabelW = BigTextWidth(masterLabel, smallScale);
    const int masterLabelH = BigGlyphHeight(smallScale);

    const std::string colorLabel = "COLOR";
    const int colorLabelW = BigTextWidth(colorLabel, smallScale);
    const int colorLabelH = BigGlyphHeight(smallScale);

    const std::string lensLabel = "LENS";
    const int lensLabelW = BigTextWidth(lensLabel, smallScale);
    const int lensLabelH = BigGlyphHeight(smallScale);

    const int labelToTrackGap = std::max(3, cols / 40);

    // Shared start column for all controls, computed from the widest of all labels, not from this
    // row's.
    const int maxLabelW = std::max({ labelW, sharpnessLabelW, musicLabelW, masterLabelW, colorLabelW, lensLabelW });
    const int controlCol0 = leftMargin + maxLabelW + labelToTrackGap;

    const int trackW = std::clamp(cols / 3, 40, 100);
    const int trackH = 1;

    const int trackPanelW = trackW + innerPadding * 2 + borderThickness * 2;
    const int trackPanelH = trackH + innerPadding * 2 + borderThickness * 2;

    const int labelRow = topMargin;
    const int labelCol = leftMargin;

    const int trackPanelCol0 = controlCol0;
    const int trackPanelRow0 = labelRow + (labelH - trackPanelH) / 2;
    const int trackPanelRow1 = trackPanelRow0 + trackPanelH - 1;
    const int trackPanelCol1 = trackPanelCol0 + trackPanelW - 1;

    const ButtonRect trackPanel{ trackPanelCol0, trackPanelRow0, trackPanelCol1, trackPanelRow1 };
    out.sliderPanel = trackPanel;

    const int trackX0 = trackPanelCol0 + borderThickness + innerPadding;
    const int trackX1 = trackX0 + trackW - 1;
    const int trackRow = trackPanelRow0 + borderThickness + innerPadding;

    out.trackX0 = trackX0;
    out.trackX1 = trackX1;
    out.trackRow = trackRow;

    // The value is a 0..100 percentage drawn with PutText(), matching the track's
    // one-glyph-per-cell font (the big font never fit that thin bar).
    const std::string sensitivityValueText = std::to_string((int)std::lround(sensitivity01 * 100.0f));
    const int sensitivityTrackCenterX = (trackX0 + trackX1) / 2;
    const int sensitivityValueCol = sensitivityTrackCenterX - (int)sensitivityValueText.size() / 2;
    const int sensitivityValueRow = trackRow;

    const int sharpnessRowGap = std::max(2, rows / 30);
    const int sharpnessLabelRow = labelRow + std::max(labelH, trackPanelH) + sharpnessRowGap;
    const int sharpnessLabelCol = leftMargin;

    const int sharpnessTrackPanelCol0 = controlCol0;
    const int sharpnessTrackPanelRow0 = sharpnessLabelRow + (sharpnessLabelH - trackPanelH) / 2;
    const int sharpnessTrackPanelRow1 = sharpnessTrackPanelRow0 + trackPanelH - 1;
    const int sharpnessTrackPanelCol1 = sharpnessTrackPanelCol0 + trackPanelW - 1;

    const ButtonRect sharpnessTrackPanel{ sharpnessTrackPanelCol0, sharpnessTrackPanelRow0,
                                           sharpnessTrackPanelCol1, sharpnessTrackPanelRow1 };
    out.sharpnessSliderPanel = sharpnessTrackPanel;

    const int sharpnessTrackX0 = sharpnessTrackPanelCol0 + borderThickness + innerPadding;
    const int sharpnessTrackX1 = sharpnessTrackX0 + trackW - 1;
    const int sharpnessTrackRow = sharpnessTrackPanelRow0 + borderThickness + innerPadding;

    out.sharpnessTrackX0 = sharpnessTrackX0;
    out.sharpnessTrackX1 = sharpnessTrackX1;
    out.sharpnessTrackRow = sharpnessTrackRow;

    // The value (cell size in px) is centered over the track and drawn after it, overlapping the
    // cells beneath; the panel size and dragging are unaffected.
    const std::string sharpnessValueText = std::to_string(sharpnessValue);
    const int sharpnessTrackCenterX = (sharpnessTrackX0 + sharpnessTrackX1) / 2;
    const int sharpnessValueCol = sharpnessTrackCenterX - (int)sharpnessValueText.size() / 2;
    const int sharpnessValueRow = sharpnessTrackRow;

    const int musicRowGap = std::max(2, rows / 30);
    const int musicLabelRow = sharpnessLabelRow + std::max(sharpnessLabelH, trackPanelH) + musicRowGap;
    const int musicLabelCol = leftMargin;

    const int musicTrackPanelCol0 = controlCol0;
    const int musicTrackPanelRow0 = musicLabelRow + (musicLabelH - trackPanelH) / 2;
    const int musicTrackPanelRow1 = musicTrackPanelRow0 + trackPanelH - 1;
    const int musicTrackPanelCol1 = musicTrackPanelCol0 + trackPanelW - 1;

    const ButtonRect musicTrackPanel{ musicTrackPanelCol0, musicTrackPanelRow0,
                                       musicTrackPanelCol1, musicTrackPanelRow1 };
    out.musicSliderPanel = musicTrackPanel;

    const int musicTrackX0 = musicTrackPanelCol0 + borderThickness + innerPadding;
    const int musicTrackX1 = musicTrackX0 + trackW - 1;
    const int musicTrackRow = musicTrackPanelRow0 + borderThickness + innerPadding;

    out.musicTrackX0 = musicTrackX0;
    out.musicTrackX1 = musicTrackX1;
    out.musicTrackRow = musicTrackRow;

    const std::string musicValueText = std::to_string((int)std::lround(music01 * 100.0f));
    const int musicTrackCenterX = (musicTrackX0 + musicTrackX1) / 2;
    const int musicValueCol = musicTrackCenterX - (int)musicValueText.size() / 2;
    const int musicValueRow = musicTrackRow;

    const int masterRowGap = std::max(2, rows / 30);
    const int masterLabelRow = musicLabelRow + std::max(musicLabelH, trackPanelH) + masterRowGap;
    const int masterLabelCol = leftMargin;

    const int masterTrackPanelCol0 = controlCol0;
    const int masterTrackPanelRow0 = masterLabelRow + (masterLabelH - trackPanelH) / 2;
    const int masterTrackPanelRow1 = masterTrackPanelRow0 + trackPanelH - 1;
    const int masterTrackPanelCol1 = masterTrackPanelCol0 + trackPanelW - 1;

    const ButtonRect masterTrackPanel{ masterTrackPanelCol0, masterTrackPanelRow0,
                                        masterTrackPanelCol1, masterTrackPanelRow1 };
    out.masterSliderPanel = masterTrackPanel;

    const int masterTrackX0 = masterTrackPanelCol0 + borderThickness + innerPadding;
    const int masterTrackX1 = masterTrackX0 + trackW - 1;
    const int masterTrackRow = masterTrackPanelRow0 + borderThickness + innerPadding;

    out.masterTrackX0 = masterTrackX0;
    out.masterTrackX1 = masterTrackX1;
    out.masterTrackRow = masterTrackRow;

    const std::string masterValueText = std::to_string((int)std::lround(master01 * 100.0f));
    const int masterTrackCenterX = (masterTrackX0 + masterTrackX1) / 2;
    const int masterValueCol = masterTrackCenterX - (int)masterValueText.size() / 2;
    const int masterValueRow = masterTrackRow;

    const int colorRowGap = std::max(2, rows / 30);
    const int colorLabelRow = masterLabelRow + std::max(masterLabelH, trackPanelH) + colorRowGap;
    const int colorLabelCol = leftMargin;

    const int checkboxInner = 1;
    const int checkboxSize = checkboxInner + innerPadding * 2 + borderThickness * 2;

    const int checkboxCol0 = controlCol0;
    const int checkboxRow0 = colorLabelRow + (colorLabelH - checkboxSize) / 2;
    const int checkboxCol1 = checkboxCol0 + checkboxSize - 1;
    const int checkboxRow1 = checkboxRow0 + checkboxSize - 1;

    const ButtonRect checkboxRect{ checkboxCol0, checkboxRow0, checkboxCol1, checkboxRow1 };
    out.colorCheckbox = checkboxRect;

    const int lensRowGap = std::max(2, rows / 30);
    const int lensLabelRow = colorLabelRow + std::max(colorLabelH, checkboxSize) + lensRowGap;
    const int lensLabelCol = leftMargin;

    const int lensCheckboxCol0 = controlCol0;
    const int lensCheckboxRow0 = lensLabelRow + (lensLabelH - checkboxSize) / 2;
    const int lensCheckboxCol1 = lensCheckboxCol0 + checkboxSize - 1;
    const int lensCheckboxRow1 = lensCheckboxRow0 + checkboxSize - 1;

    const ButtonRect lensCheckboxRect{ lensCheckboxCol0, lensCheckboxRow0, lensCheckboxCol1, lensCheckboxRow1 };
    out.lensCheckbox = lensCheckboxRect;

    // BACK: centered at the bottom with its own vertical anchor, independent of the rows above, so
    // it does not jump when a settings row is added.
    const std::string backText = "BACK";
    const int backTextW = BigTextWidth(backText, smallScale);
    const int backTextH = BigGlyphHeight(smallScale);
    const int backW = backTextW + innerPadding * 2 + borderThickness * 2;
    const int backH = backTextH + innerPadding * 2 + borderThickness * 2;

    const int backCol = std::max(0, (cols - backW) / 2);
    const int minBackRow = std::max({ trackPanelRow1, sharpnessTrackPanelRow1, musicTrackPanelRow1, masterTrackPanelRow1, checkboxRow1, lensCheckboxRow1 }) + 1 + std::max(4, rows / 10);
    const int preferredBackRow = (int)std::lround(rows * 0.70);
    const int backRow0 = std::clamp(preferredBackRow, minBackRow, std::max(minBackRow, rows - backH - 4));
    const int backRow1 = backRow0 + backH - 1;

    const ButtonRect backRect{ backCol, backRow0, backCol + backW - 1, backRow1 };
    out.backButton = backRect;

    AtmosphereBounds contentBounds;
    contentBounds.x0 = std::min(labelCol, backRect.x0);
    contentBounds.y0 = labelRow;
    contentBounds.x1 = std::max({ trackPanel.x1, sharpnessTrackPanel.x1, musicTrackPanel.x1, masterTrackPanel.x1, checkboxRect.x1, lensCheckboxRect.x1, backRect.x1 });
    contentBounds.y1 = backRect.y1;
    DrawDarkFantasyAtmosphere(out.grid, cols, rows, seed, pauseMenu, contentBounds, variantIndex);

    DrawBigText(out.grid, cols, rows, sliderLabel, labelCol, labelRow, smallScale, seed);

    DrawBox(out.grid, cols, rows, trackPanel.x0, trackPanel.y0, trackPanel.x1, trackPanel.y1,
            borderThickness, sliderHovered, seed + 3000);

    const int handleX = trackX0 + (int)std::lround(sensitivity01 * (float)(trackW - 1));
    for (int x = trackX0; x <= trackX1; ++x) {
        unsigned char g;
        if (x == handleX)      g = GLYPH_CIRCLE;
        else if (x < handleX)  g = GLYPH_HASH;
        else                    g = GLYPH_FLOOR;
        PutGlyph(out.grid, cols, rows, x, trackRow, g);
    }

    PutText(out.grid, cols, rows, sensitivityValueCol, sensitivityValueRow, sensitivityValueText);

    DrawBigText(out.grid, cols, rows, sharpnessLabel, sharpnessLabelCol, sharpnessLabelRow, smallScale, seed + 9000);

    DrawBox(out.grid, cols, rows, sharpnessTrackPanel.x0, sharpnessTrackPanel.y0,
            sharpnessTrackPanel.x1, sharpnessTrackPanel.y1,
            borderThickness, sharpnessSliderHovered, seed + 10000);

    const int sharpnessHandleX = sharpnessTrackX0 + (int)std::lround(sharpness01 * (float)(trackW - 1));
    for (int x = sharpnessTrackX0; x <= sharpnessTrackX1; ++x) {
        unsigned char g;
        if (x == sharpnessHandleX)     g = GLYPH_CIRCLE;
        else if (x < sharpnessHandleX) g = GLYPH_HASH;
        else                             g = GLYPH_FLOOR;
        PutGlyph(out.grid, cols, rows, x, sharpnessTrackRow, g);
    }

    PutText(out.grid, cols, rows, sharpnessValueCol, sharpnessValueRow, sharpnessValueText);

    DrawBigText(out.grid, cols, rows, musicLabel, musicLabelCol, musicLabelRow, smallScale, seed + 11000);

    DrawBox(out.grid, cols, rows, musicTrackPanel.x0, musicTrackPanel.y0,
            musicTrackPanel.x1, musicTrackPanel.y1,
            borderThickness, musicSliderHovered, seed + 12000);

    const int musicHandleX = musicTrackX0 + (int)std::lround(music01 * (float)(trackW - 1));
    for (int x = musicTrackX0; x <= musicTrackX1; ++x) {
        unsigned char g;
        if (x == musicHandleX)      g = GLYPH_CIRCLE;
        else if (x < musicHandleX)  g = GLYPH_HASH;
        else                         g = GLYPH_FLOOR;
        PutGlyph(out.grid, cols, rows, x, musicTrackRow, g);
    }

    PutText(out.grid, cols, rows, musicValueCol, musicValueRow, musicValueText);

    DrawBigText(out.grid, cols, rows, masterLabel, masterLabelCol, masterLabelRow, smallScale, seed + 13000);

    DrawBox(out.grid, cols, rows, masterTrackPanel.x0, masterTrackPanel.y0,
            masterTrackPanel.x1, masterTrackPanel.y1,
            borderThickness, masterSliderHovered, seed + 14000);

    const int masterHandleX = masterTrackX0 + (int)std::lround(master01 * (float)(trackW - 1));
    for (int x = masterTrackX0; x <= masterTrackX1; ++x) {
        unsigned char g;
        if (x == masterHandleX)      g = GLYPH_CIRCLE;
        else if (x < masterHandleX)  g = GLYPH_HASH;
        else                          g = GLYPH_FLOOR;
        PutGlyph(out.grid, cols, rows, x, masterTrackRow, g);
    }

    PutText(out.grid, cols, rows, masterValueCol, masterValueRow, masterValueText);

    DrawBigText(out.grid, cols, rows, colorLabel, colorLabelCol, colorLabelRow, smallScale, seed + 6000);

    DrawBox(out.grid, cols, rows, checkboxRect.x0, checkboxRect.y0, checkboxRect.x1, checkboxRect.y1,
            borderThickness, colorCheckboxHovered, seed + 7000);

    const int checkboxCenterX = checkboxRect.x0 + borderThickness + innerPadding;
    const int checkboxCenterY = checkboxRect.y0 + borderThickness + innerPadding;
    PutGlyph(out.grid, cols, rows, checkboxCenterX, checkboxCenterY,
             colorEnabled ? GLYPH_X : GLYPH_SPACE);

    DrawBigText(out.grid, cols, rows, lensLabel, lensLabelCol, lensLabelRow, smallScale, seed + 8000);

    DrawBox(out.grid, cols, rows, lensCheckboxRect.x0, lensCheckboxRect.y0, lensCheckboxRect.x1, lensCheckboxRect.y1,
            borderThickness, lensCheckboxHovered, seed + 9000);

    const int lensCheckboxCenterX = lensCheckboxRect.x0 + borderThickness + innerPadding;
    const int lensCheckboxCenterY = lensCheckboxRect.y0 + borderThickness + innerPadding;
    PutGlyph(out.grid, cols, rows, lensCheckboxCenterX, lensCheckboxCenterY,
             lensEnabled ? GLYPH_X : GLYPH_SPACE);

    DrawBox(out.grid, cols, rows, backRect.x0, backRect.y0, backRect.x1, backRect.y1,
            borderThickness, backHovered, seed + 4000);
    const int backTextCol = backCol + (backW - backTextW) / 2;
    const int backTextRow = backRow0 + (backH - backTextH) / 2;
    DrawBigText(out.grid, cols, rows, backText, backTextCol, backTextRow, smallScale, seed + 4500);

    return out;
}


// DrawHudIcons: torch/stone/diary count icons above the stamina bar. Three small square frames in a
// row, each with an item glyph inside and a number right of its bottom corner; the whole group is
// centered horizontally.
void DrawHudIcons(std::vector<unsigned char>& grid, int cols, int rows,
                   int torchCount, int stoneCount, int diaryCount,
                   int bottomRow, int seed) {
    if (cols <= 0 || rows <= 0) return;

    const int boxSize = 3;
    const int groupGap = 2; // between the three icons
    const int numberMaxW = 2; // headroom for two-digit numbers (up to 99)
    const int groupW = boxSize + numberMaxW;

    struct IconDef { unsigned char glyph; int count; };
    const IconDef icons[3] = {
        { GLYPH_TORCH_ICON, torchCount },
        { GLYPH_STONE_ICON, stoneCount },
        { GLYPH_DIARY_ICON, diaryCount },
    };

    const int totalW = groupW * 3 + groupGap * 2;
    int col = std::max(0, (cols - totalW) / 2);
    const int boxRow0 = std::max(0, bottomRow - boxSize + 1);
    const int boxRow1 = boxRow0 + boxSize - 1;

    for (int i = 0; i < 3; ++i) {
        const int boxCol0 = col;
        const int boxCol1 = boxCol0 + boxSize - 1;

        DrawBox(grid, cols, rows, boxCol0, boxRow0, boxCol1, boxRow1, 1, /*filled=*/false, seed + i * 100);
        PutGlyph(grid, cols, rows, boxCol0 + boxSize / 2, boxRow0 + boxSize / 2, icons[i].glyph);

        // The number sits beside the frame's bottom corner. The whole reserved area (numberMaxW
        // cells) is cleared first: grid is a reusable buffer, so a number that got shorter ("12" ->
        // "9") would otherwise leave an old digit behind.
        for (int cx = 0; cx < numberMaxW; ++cx) {
            PutGlyph(grid, cols, rows, boxCol1 + 1 + cx, boxRow1, GLYPH_SPACE);
        }
        const std::string countText = std::to_string(std::max(0, icons[i].count));
        PutText(grid, cols, rows, boxCol1 + 1, boxRow1, countText);

        col += groupW + groupGap;
    }
}

} // namespace MainMenu
