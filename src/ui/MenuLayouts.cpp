#include "MenuLayouts.h"
#include "TextGrid.h"
#include "BigFont.h"
#include "Atmosphere.h"
#include "UiGlyphs.h"
#include "scene/Difficulty.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace MainMenu {

// rows / 60, capped at 1. The grid always has at least kMenuMinGridRows rows; the 0.15 floor is a
// safety net.
static float ComputeMenuBaseScale(int rows) {
    return std::clamp((float)rows / 60.0f, 0.15f, 1.0f);
}

// One finalRes step smaller; returns the input unchanged at the floor (finalRes == 1).
static float ShrinkTextScale(float baseTextScale, float scaleMultiplier) {
    if (scaleMultiplier >= 1.0f) return baseTextScale;
    const int normalRes = ComputeFinalRes(baseTextScale);
    int reducedRes = std::max(1, (int)std::lround((double)normalRes * scaleMultiplier));
    if (reducedRes >= normalRes && normalRes > 1) {
        reducedRes = normalRes - 1;
    }
    return (float)reducedRes / (float)kMaskUpsample;
}

// Compact screens (slots, confirmation, name entry): about 20% smaller than the main menu.
static constexpr float kCompactScale = 0.8f;

// Left margin of the main menu: cols / kLeftMarginDiv, at least kLeftMarginMin.
static constexpr int kLeftMarginDiv = 16;
static constexpr int kLeftMarginMin = 6;


// Title plus N stacked buttons of equal width: main menu, pause and slot screens. seed is fixed per
// session so ragged frames do not change on hover.
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
                             bool leftAligned,
                             const ButtonRect* reservedArea) {
    grid.assign((size_t)std::max(0, cols) * std::max(0, rows), 0);
    outButtons.assign(buttonTexts.size(), ButtonRect{});
    outTitle = ButtonRect{};
    outTitleBox = ButtonRect{};

    if (cols <= 0 || rows <= 0 || buttonTexts.empty()) return;

    // Pause has no title; then only the buttons are centered. The atmosphere is drawn later, once
    // the real content box is known.
    const bool hasTitle = !title.empty();

    // Title and button scales derive separately from one base, so buttons stay much smaller than
    // the title.
    const float baseScale = ComputeMenuBaseScale(rows);

    // Compact screens pass kCompactScale; the main menu and pause use 1.
    const float titleScaleBase = baseScale * 1.5f;
    const float titleScale = ShrinkTextScale(titleScaleBase, buttonScale);
    const int letterH = hasTitle ? BigGlyphHeight(titleScale) : 0;
    const int titleWidth = hasTitle ? BigTextWidth(title, titleScale) : 0;

    // Only the text shrinks; frames and padding stay the same across screens.
    const int buttonBorderThickness = 2;
    const int buttonInnerPadding = 1; // padding between text and frame inside a button

    // The title frame extends past the letters; count it in contentHeight or its top clips.
    const int titleInnerPadding = 2;
    const int titleOverhang = hasTitle ? (titleInnerPadding + buttonBorderThickness) : 0;

    // Tightened by the second fitting pass below.
    int buttonGap = pauseMenu ? 4 : 3;
    int titleToButtonsGap = hasTitle ? std::max(4, rows / 10) : 0;
    const int buttonCount = (int)buttonTexts.size();

    // Shrink the button font until the block fits; the title keeps its size.
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

    // Font at its floor: squeeze the gaps instead.
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

    // Centered, or left-aligned for the main menu. The title frame is the widest element, so it
    // clamps the shared left edge.
    const int widestW = hasTitle ? (titleWidth + 2 * titleOverhang) : buttonW;
    const int leftEdge = std::min(std::max(kLeftMarginMin, cols / kLeftMarginDiv),
                                  std::max(0, cols - widestW));
    const int titleCol = leftAligned ? leftEdge + titleOverhang
                                     : std::max(0, (cols - titleWidth) / 2);
    // Offset so the frame above the letters stays inside contentTop.
    const int titleRow = contentTop + titleOverhang;

    if (hasTitle) {
        outTitle = ButtonRect{
            titleCol,
            titleRow,
            titleCol + std::max(0, titleWidth - 1),
            titleRow + std::max(0, letterH - 1)
        };

        // More padding than buttons (the text is bigger), same frame thickness.
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

    // Atmosphere around the content box, drawn before the buttons so they overwrite any decoration
    // that reaches them.
    AtmosphereBounds contentBounds;
    contentBounds.x0 = hasTitle ? std::min(outTitleBox.x0, buttons.front().x0) : buttons.front().x0;
    contentBounds.y0 = hasTitle ? outTitleBox.y0 : buttons.front().y0;
    contentBounds.x1 = hasTitle ? std::max(outTitleBox.x1, buttons.front().x1) : buttons.front().x1;
    contentBounds.y1 = buttons.back().y1;
    if (reservedArea) {
        contentBounds.x0 = std::min(contentBounds.x0, reservedArea->x0);
        contentBounds.y0 = std::min(contentBounds.y0, reservedArea->y0);
        contentBounds.x1 = std::max(contentBounds.x1, reservedArea->x1);
        contentBounds.y1 = std::max(contentBounds.y1, reservedArea->y1);
    }
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

    // After the buttons: the shattered title may fall over them and must stay on top.
    if (hasTitle && !skipTitleDraw) {
        if (titleState) {
            DrawBrokenTitle(grid, cols, rows, title, titleCol, titleRow, titleScale, *titleState);
        } else {
            DrawBigText(grid, cols, rows, title, titleCol, titleRow, titleScale, seed);
        }
    }
}

// Start screen. The variant is picked once per visit; the base grid is cached and only the title
// animation is redrawn.
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

// Pause: no title, a rune sigil at the top of the frame.
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

// Continue, Save and New Game share a 3 slots + BACK layout.
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

// Empty slots are reported through slotFilled; the caller keeps them unclickable.
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

namespace {

std::vector<std::string> DifficultyGoalLines(const DifficultyRules& rules) {
    std::vector<std::string> lines;
    if (rules.oneHitKills)
        lines.push_back("SURVIVE");
    if (rules.diariesToWin > 0)
        lines.push_back("READ " + std::to_string(rules.diariesToWin) + " DIARIES");
    lines.push_back("FIND THE EXIT");
    return lines;
}

std::vector<std::string> DifficultyDebuffLines(const DifficultyRules& rules) {
    if (rules.oneHitKills)
        return { "ONE HIT KILLS YOU." };
    return { "NONE." };
}

void ClearRect(std::vector<unsigned char>& grid, int cols, int rows, const ButtonRect& r) {
    for (int y = std::max(0, r.y0); y <= std::min(rows - 1, r.y1); ++y)
        for (int x = std::max(0, r.x0); x <= std::min(cols - 1, r.x1); ++x)
            grid[(size_t)y * cols + x] = 0;
}

} // namespace

SlotMenuLayout BuildNewGameMenu(int cols, int rows, int selection, int seed,
                                 const std::string slotLabels[3],
                                 const bool slotFilled[3],
                                 int variantIndex,
                                 int selectedDifficulty,
                                 int hoveredDifficulty) {
    SlotMenuLayout out;
    const std::vector<std::string> buttonTexts = { slotLabels[0], slotLabels[1], slotLabels[2], "BACK" };
    std::vector<ButtonRect> buttons;
    ButtonRect titleRect, titleBox;

    // The first pass only measures the centered column; the panels go beside it.
    BuildButtonMenu(out.grid, cols, rows, selection, seed, "NEW", buttonTexts, buttons, titleRect, titleBox,
                    nullptr, /*pauseMenu=*/true, false, variantIndex, false, kCompactScale);
    if (buttons.size() != 4) return out;

    // Difficulty names use the smallest big-font size; descriptions use the one-cell UI font.
    const float nameScale = 1.0f / (float)kMaskUpsample;
    const int nameW = BigTextWidth("NORMAL", nameScale);
    const int nameH = BigGlyphHeight(nameScale);
    const int panelPad = 2;
    const int columnGap = std::max(3, cols / 32);

    // The selection is an underline rather than a frame per name, which keeps the picker compact.
    const int nameStep = nameH + 3; // name, underline row, gap
    const int pickerW = nameW + 6;
    const int pickerH = 2 + 2 + kDifficultyCount * nameStep;
    const int infoW = pickerW * 5 / 4;
    const int infoH = pickerH * 5 / 4;

    const int contentX0 = std::min(titleBox.x0, buttons.front().x0);
    const int contentX1 = std::max(titleBox.x1, buttons.front().x1);
    const int centerY = (int)std::lround(rows * 0.56);
    auto placeY = [&](int h) {
        const int y0 = std::max(1, centerY - h / 2);
        return std::pair<int, int>(y0, std::min(rows - 2, y0 + h - 1));
    };
    const auto [pickerY0, pickerY1] = placeY(pickerH);
    const auto [infoY0, infoY1] = placeY(infoH);

    const int pickerX0 = contentX1 + columnGap;
    const ButtonRect picker{ pickerX0, pickerY0, std::min(cols - 2, pickerX0 + pickerW - 1), pickerY1 };
    const int infoX1 = contentX0 - columnGap;
    const ButtonRect info{ std::max(1, infoX1 - infoW + 1), infoY0, infoX1, infoY1 };

    const ButtonRect reserved{ info.x0 - 1, std::min(titleBox.y0, infoY0 - 1),
                               picker.x1 + 1, std::max(buttons.back().y1, infoY1 + 1) };
    BuildButtonMenu(out.grid, cols, rows, selection, seed, "NEW", buttonTexts, buttons, titleRect, titleBox,
                    nullptr, true, false, variantIndex, false, kCompactScale, false, &reserved);

    for (int i = 0; i < 3; ++i) {
        out.slotButtons[i] = buttons[(size_t)i];
        out.slotFilled[i] = slotFilled[i];
    }
    out.backButton = buttons[3];

    // Selected: '=' underline; hovered: '-' underline.
    ClearRect(out.grid, cols, rows, picker);
    DrawBox(out.grid, cols, rows, picker.x0, picker.y0, picker.x1, picker.y1, 1, false, seed + 6000);
    PutText(out.grid, cols, rows, picker.x0 + 2, picker.y0 + 1, "DIFFICULTY");
    const int nameX0 = picker.x0 + 2;
    int nameY = picker.y0 + 3;
    for (int d = 0; d < kDifficultyCount; ++d) {
        const char* name = kDifficultyRules[d].name;
        const int textX = nameX0 + (nameW - BigTextWidth(name, nameScale)) / 2;
        DrawBigText(out.grid, cols, rows, name, textX, nameY, nameScale, seed + 6200 + d);
        const int underlineY = nameY + nameH;
        const bool selected = d == selectedDifficulty;
        if (selected || d == hoveredDifficulty) {
            for (int x = textX; x < textX + BigTextWidth(name, nameScale); ++x)
                PutGlyph(out.grid, cols, rows, x, underlineY, selected ? GLYPH_HLINE : GLYPH_DASH);
        }
        out.difficultyButtons[d] = ButtonRect{ nameX0, nameY - 1, nameX0 + nameW - 1, underlineY + 1 };
        nameY += nameStep;
    }

    const int shown = std::clamp(hoveredDifficulty >= 0 ? hoveredDifficulty : selectedDifficulty,
                                 0, kDifficultyCount - 1);
    const DifficultyRules& rules = kDifficultyRules[shown];
    ClearRect(out.grid, cols, rows, info);
    DrawBox(out.grid, cols, rows, info.x0, info.y0, info.x1, info.y1, 1, false, seed + 6300);
    // The UI font is one cell high, so its lines are double-spaced; the block is centered.
    const std::vector<std::string> goal = DifficultyGoalLines(rules);
    const std::vector<std::string> debuffs = DifficultyDebuffLines(rules);
    const int lineStep = 2;
    const int sectionGap = 3;
    const int blockH = nameH + sectionGap + lineStep * (1 + (int)goal.size()) + sectionGap +
                       lineStep * (1 + (int)debuffs.size());
    const int textX = info.x0 + panelPad + 1;
    int textY = info.y0 + std::max(panelPad, (info.y1 - info.y0 + 1 - blockH) / 2);
    DrawBigText(out.grid, cols, rows, rules.name, textX, textY, nameScale, seed + 6400);
    textY += nameH + sectionGap;
    PutText(out.grid, cols, rows, textX, textY, "GOAL:");
    for (const std::string& line : goal)
        PutText(out.grid, cols, rows, textX + 2, textY += lineStep, line);
    textY += sectionGap + lineStep - 1;
    PutText(out.grid, cols, rows, textX, textY, "DEBUFFS:");
    for (const std::string& line : debuffs)
        PutText(out.grid, cols, rows, textX + 2, textY += lineStep, line);

    return out;
}

// Overwrite confirmation: a small-font message with YES/NO in one frame; a big-font title cannot
// hold a sentence.
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

    // Same arithmetic as BuildButtonMenu().
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

// Name entry: title, the typed name with placeholders, OK and BACK. Input is handled in
// Application.cpp.
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

    // Underscores for untyped letters; sized between the title and the buttons.
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

// Settings: slider and checkbox rows aligned to one column, BACK centered at the bottom.
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

    // Controls start at a shared column set by the widest label.
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

    // 0..100, one glyph per cell to match the thin track.
    const std::string sensitivityValueText = std::to_string((int)std::lround(sensitivity01 * 100.0f));
    const int sensitivityTrackCenterX = (trackX0 + trackX1) / 2;
    const int sensitivityValueCol = sensitivityTrackCenterX - (int)sensitivityValueText.size() / 2;
    const int sensitivityValueRow = trackRow;

    // One gap for every row; it shrinks when the optional rows would push BACK off the screen.
    const int rowCount = shadersUnlocked ? 8 : 7;
    const int rowH = std::max(labelH, trackPanelH);
    const int spareRows = rows - topMargin - rowCount * rowH - (rowH + 4);
    const int rowGap = std::clamp(spareRows / rowCount, 1, std::max(2, rows / 30));
    const int sharpnessRowGap = rowGap;
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

    // Cell size in pixels, centered over the track.
    const std::string sharpnessValueText = std::to_string(sharpnessValue);
    const int sharpnessTrackCenterX = (sharpnessTrackX0 + sharpnessTrackX1) / 2;
    const int sharpnessValueCol = sharpnessTrackCenterX - (int)sharpnessValueText.size() / 2;
    const int sharpnessValueRow = sharpnessTrackRow;

    const int musicRowGap = rowGap;
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

    const int masterRowGap = rowGap;
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

    const int colorRowGap = rowGap;
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

    const int lensRowGap = rowGap;
    const int lensLabelRow = colorLabelRow + std::max(colorLabelH, checkboxSize) + lensRowGap;
    const int lensLabelCol = leftMargin;

    const int lensCheckboxCol0 = controlCol0;
    const int lensCheckboxRow0 = lensLabelRow + (lensLabelH - checkboxSize) / 2;
    const int lensCheckboxCol1 = lensCheckboxCol0 + checkboxSize - 1;
    const int lensCheckboxRow1 = lensCheckboxRow0 + checkboxSize - 1;

    const ButtonRect lensCheckboxRect{ lensCheckboxCol0, lensCheckboxRow0, lensCheckboxCol1, lensCheckboxRow1 };
    out.lensCheckbox = lensCheckboxRect;

    const int optionRowStep = std::max(lensLabelH, checkboxSize) + lensRowGap;

    const std::string crtLabel = "CRT";
    const int crtLabelRow = lensLabelRow + optionRowStep;
    const int crtCheckboxRow0 = crtLabelRow + (lensLabelH - checkboxSize) / 2;
    const ButtonRect crtCheckboxRect{ controlCol0, crtCheckboxRow0,
                                      controlCol0 + checkboxSize - 1, crtCheckboxRow0 + checkboxSize - 1 };
    out.crtCheckbox = crtCheckboxRect;

    const std::string shadersLabel = "SHADERS";
    const int shadersLabelRow = crtLabelRow + optionRowStep;
    const int shadersCheckboxRow0 = shadersLabelRow + (lensLabelH - checkboxSize) / 2;
    const ButtonRect shadersCheckboxRect{ controlCol0, shadersCheckboxRow0,
                                          controlCol0 + checkboxSize - 1, shadersCheckboxRow0 + checkboxSize - 1 };
    out.hasShadersCheckbox = shadersUnlocked;
    if (shadersUnlocked)
        out.shadersCheckbox = shadersCheckboxRect;
    const int lastRowBottom = shadersUnlocked ? shadersCheckboxRect.y1 : crtCheckboxRect.y1;

    // BACK has its own anchor, so adding a row does not move it.
    const std::string backText = "BACK";
    const int backTextW = BigTextWidth(backText, smallScale);
    const int backTextH = BigGlyphHeight(smallScale);
    const int backW = backTextW + innerPadding * 2 + borderThickness * 2;
    const int backH = backTextH + innerPadding * 2 + borderThickness * 2;

    const int backCol = std::max(0, (cols - backW) / 2);
    const int controlsBottom = std::max({ trackPanelRow1, sharpnessTrackPanelRow1, musicTrackPanelRow1,
                                          masterTrackPanelRow1, checkboxRow1, lastRowBottom });
    // The gap above BACK shrinks before BACK is pushed off the bottom edge.
    const int maxBackRow = std::max(controlsBottom + 2, rows - backH - 2);
    const int minBackRow = std::min(controlsBottom + 1 + std::max(4, rows / 10), maxBackRow);
    const int preferredBackRow = (int)std::lround(rows * 0.70);
    const int backRow0 = std::clamp(preferredBackRow, minBackRow, maxBackRow);
    const int backRow1 = backRow0 + backH - 1;

    const ButtonRect backRect{ backCol, backRow0, backCol + backW - 1, backRow1 };
    out.backButton = backRect;

    AtmosphereBounds contentBounds;
    contentBounds.x0 = std::min(labelCol, backRect.x0);
    contentBounds.y0 = labelRow;
    contentBounds.x1 = std::max({ trackPanel.x1, sharpnessTrackPanel.x1, musicTrackPanel.x1, masterTrackPanel.x1, checkboxRect.x1, lensCheckboxRect.x1, crtCheckboxRect.x1, backRect.x1 });
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

    DrawBigText(out.grid, cols, rows, crtLabel, leftMargin, crtLabelRow, smallScale, seed + 8200);
    DrawBox(out.grid, cols, rows, crtCheckboxRect.x0, crtCheckboxRect.y0, crtCheckboxRect.x1,
            crtCheckboxRect.y1, borderThickness, crtCheckboxHovered, seed + 9200);
    PutGlyph(out.grid, cols, rows, crtCheckboxRect.x0 + borderThickness + innerPadding,
             crtCheckboxRect.y0 + borderThickness + innerPadding, crtEnabled ? GLYPH_X : GLYPH_SPACE);

    if (shadersUnlocked) {
        DrawBigText(out.grid, cols, rows, shadersLabel, leftMargin, shadersLabelRow, smallScale, seed + 8100);
        DrawBox(out.grid, cols, rows, shadersCheckboxRect.x0, shadersCheckboxRect.y0, shadersCheckboxRect.x1,
                shadersCheckboxRect.y1, borderThickness, shadersCheckboxHovered, seed + 9100);
        PutGlyph(out.grid, cols, rows, shadersCheckboxRect.x0 + borderThickness + innerPadding,
                 shadersCheckboxRect.y0 + borderThickness + innerPadding, shadersEnabled ? GLYPH_X : GLYPH_SPACE);
    }

    DrawBox(out.grid, cols, rows, backRect.x0, backRect.y0, backRect.x1, backRect.y1,
            borderThickness, backHovered, seed + 4000);
    const int backTextCol = backCol + (backW - backTextW) / 2;
    const int backTextRow = backRow0 + (backH - backTextH) / 2;
    DrawBigText(out.grid, cols, rows, backText, backTextCol, backTextRow, smallScale, seed + 4500);

    return out;
}


// Torch/stone/diary counters above the stamina bar, centered as a group.
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

        // Clear the whole number area: the grid is reused and a shorter number would leave a stale
        // digit.
        for (int cx = 0; cx < numberMaxW; ++cx) {
            PutGlyph(grid, cols, rows, boxCol1 + 1 + cx, boxRow1, GLYPH_SPACE);
        }
        const std::string countText = std::to_string(std::max(0, icons[i].count));
        PutText(grid, cols, rows, boxCol1 + 1, boxRow1, countText);

        col += groupW + groupGap;
    }
}

} // namespace MainMenu
