#include "Application.h"
#include "ProcessMemory.h"
#include "save/SaveSystem.h"
#include "audio/AudioMixer.h"
#include "audio/UiAudio.h"
#if __has_include("dev/DevTools.h")
#include "dev/DevTools.h"
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>

float Application::sensitivityToSlider01(float sensitivity) {
    return std::clamp(
        (sensitivity - DungeonScene::kMinMouseSensitivity) /
            (DungeonScene::kMaxMouseSensitivity - DungeonScene::kMinMouseSensitivity),
        0.0f, 1.0f);
}

float Application::slider01ToSensitivity(float t) {
    return DungeonScene::kMinMouseSensitivity +
        std::clamp(t, 0.0f, 1.0f) *
            (DungeonScene::kMaxMouseSensitivity - DungeonScene::kMinMouseSensitivity);
}

float Application::cellSizeToSlider01(int cellSize) {
    return std::clamp(
        (float)(cellSize - AsciiEffect::kMinCellSize) /
            (float)(AsciiEffect::kMaxCellSize - AsciiEffect::kMinCellSize),
        0.0f, 1.0f);
}

int Application::slider01ToCellSize(float t) {
    const float raw = (float)AsciiEffect::kMinCellSize +
        std::clamp(t, 0.0f, 1.0f) *
            (float)(AsciiEffect::kMaxCellSize - AsciiEffect::kMinCellSize);
    return std::clamp((int)std::lround(raw), AsciiEffect::kMinCellSize, AsciiEffect::kMaxCellSize);
}

namespace {

// Blank lines end a stanza (see StoryText)
const std::vector<std::string> kIntroText = {
    "It was a dream",
    "A dream of the Sun",
    "",
    "I saw the Sun",
    "I saw my eyes",
    "I saw my face",
    "",
    "I saw nothing",
    "",
    "I lost my eyes",
    "I lost my face",
    "",
    "The Sun burned me",
    "",
    "It was a dream",
    "",
    "Hey, Mom?",
    "",
    "I'm still dreaming.",
};

const std::vector<std::string> kOutroText = {
    "Hey, Mom?",
    "",
    "I'm awake.",
};

} // namespace

int Application::currentUiHoverId() const
{
    if (m_appState == AppState::SETTINGS) {
        const bool hovered[] = { m_backHovered, m_sliderHovered, m_sharpnessSliderHovered,
                                 m_musicSliderHovered, m_masterSliderHovered, m_colorCheckboxHovered,
                                 m_lensCheckboxHovered, m_crtCheckboxHovered, m_shadersCheckboxHovered };
        for (int i = 0; i < (int)std::size(hovered); ++i)
            if (hovered[i]) return 1000 + i;
        return -1;
    }
    if (m_appState == AppState::MENU && m_titleHovered)
        return 900;
    if (m_hoveredButton >= 0)
        return (int)m_appState * 100 + m_hoveredButton;
    return -1;
}

void Application::startStory(bool leadsToCredits)
{
    m_storyText.start(leadsToCredits ? kOutroText : kIntroText, m_menuSeed);
    m_storyLeadsToCredits = leadsToCredits;
    // A Space held over from the previous screen does not count toward skipping
    m_storySkipArmed = false;
    m_storySkipHold = 0.0f;
    // The scene is not rendered, so the screen stays black without the fade
    m_fadeAlpha = 0.0f;
    m_appState = AppState::STORY_TEXT;
}

void Application::init(GLFWwindow* window, DungeonScene& scene, AsciiEffect& ascii)
{
    (void)window;
    (void)scene;
    (void)ascii;

    m_gpuTimer.init();
    m_crt.init();

    // Non-fatal: without the TTF renderer diary text is simply not shown
    if (!m_textRenderer.create())
        std::fprintf(stderr, "Application::init: TextRenderer failed to initialize\n");

    m_menuVariant = MainMenu::PickAtmosphereVariant(m_menuOpenCount, m_menuSeed);
    m_pauseVariant = MainMenu::PickAtmosphereVariant(m_pauseOpenCount, m_menuSeed);
}

void Application::confirmNameEntry(DungeonScene& scene)
{
    // An empty name defaults to SLOT{n}.
    const int slot = m_nameEntryForNewGame ? m_pendingNewGameSlot : m_pendingSaveSlot;
    std::string effectiveName = m_saveNameBuffer;
    if (effectiveName.empty() && slot >= 0) {
        effectiveName = "SLOT" + std::to_string(slot + 1);
    }

    if (m_nameEntryForNewGame) {
        // The new game does not exist yet: keep the name and fade to black; the map is generated
        // once the screen is black
        m_saveNameBuffer = effectiveName;
        m_pendingAction = PendingAction::NEW_GAME_IN_SLOT;
        m_appState = AppState::FADE_TO_BLACK;
    } else {
        scene.saveToSlot(m_pendingSaveSlot, effectiveName);
        m_pendingSaveSlot = -1;
        m_saveNameBuffer.clear();
        m_appState = AppState::PAUSED;
    }
    m_hoveredButton = -1;
}

void Application::tick(GLFWwindow* window, DungeonScene& scene, AsciiEffect& ascii,
                       WindowManager& windowManager, float deltaTime)
{
        deltaTime = std::min(deltaTime, 0.05f); // guard against a dt spike after pause/lag

        // Once a second: fps, GPU time per section and what the scene submitted
        // Uses deltaTime is clamped to 50 ms, which would hide anything below 20 fps
        {
            const double now = glfwGetTime();
            if (m_perfLastTime < 0.0)
                m_perfLastTime = now;
            ++m_perfFrameCount;
            const double elapsed = now - m_perfLastTime;
            if (elapsed >= 1.0)
            {
                const double ramMb = (double)GetProcessWorkingSetBytes() / (1024.0 * 1024.0);
                std::printf(
                    "[perf] fps = %.1f (avg frame time %.2f ms) | gpu scene=%.2f ascii=%.2f crt=%.2f ms | "
                    "tris=%d chunks=%d torches=%d particles=%d lens=%d | ram=%.1f MB\n",
                    m_perfFrameCount / elapsed,
                    1000.0 * elapsed / m_perfFrameCount,
                    m_gpuTimer.averageMs(GpuTimer::Scene),
                    m_gpuTimer.averageMs(GpuTimer::Ascii),
                    m_gpuTimer.averageMs(GpuTimer::Crt),
                    scene.getLastVisibleTriangles(),
                    scene.getLastVisibleChunks(),
                    scene.getLastActiveTorchCount(),
                    scene.getLastVisibleParticles(),
                    m_lensEnabled ? 1 : 0,
                    ramMb);
                m_perfFrameCount = 0;
                m_perfLastTime = now;
                m_gpuTimer.resetAverages();
            }
        }

        // ESC pause
        const bool escKeyDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (escKeyDown && !m_escKeyWasDown) {
            if (m_appState == AppState::PLAYING && scene.isReadingOverlayOpen()) {
                // Escape closes an open diary
                scene.closeDiaryOrJournal();
            } else if (m_appState == AppState::PLAYING) {
                m_appState = AppState::PAUSED;
                m_hoveredButton = -1;
                m_titleHovered = false;
                m_pauseLayoutDirty = true;
                ++m_pauseOpenCount;
                // Never repeat the previous pause
                m_pauseVariant = MainMenu::PickNextAtmosphereVariant(
                    m_pauseVariant, m_pauseOpenCount, m_menuSeed + 777);
            } else if (m_appState == AppState::PAUSED) {
                m_appState = AppState::PLAYING;
            } else if (m_appState == AppState::SETTINGS) {
                m_appState = m_settingsReturnState; // back to wherever it was opened
                m_backHovered = false;
                m_sliderHovered = false;
                m_sliderDragging = false;
                m_colorCheckboxHovered = false;
            } else if (m_appState == AppState::CONTINUE_SELECT) {
                m_appState = AppState::MENU;
                m_hoveredButton = -1;
            } else if (m_appState == AppState::SAVE_SELECT) {
                m_appState = AppState::PAUSED;
                m_hoveredButton = -1;
            } else if (m_appState == AppState::NEWGAME_SELECT) {
                m_appState = AppState::MENU;
                m_hoveredButton = -1;
            } else if (m_appState == AppState::NEWGAME_CONFIRM) {
                m_appState = AppState::NEWGAME_SELECT;
                m_hoveredButton = -1;
                m_newGameLayoutDirty = true;
            } else if (m_appState == AppState::SAVE_CONFIRM) {
                m_appState = AppState::SAVE_SELECT;
                m_hoveredButton = -1;
                m_saveLayoutDirty = true;
            } else if (m_appState == AppState::SAVE_NAME_ENTRY) {
                // BACK
                m_appState = m_nameEntryForNewGame ? AppState::NEWGAME_SELECT : AppState::SAVE_SELECT;
                m_hoveredButton = -1;
                m_saveNameBuffer.clear();
                if (m_nameEntryForNewGame) m_newGameLayoutDirty = true;
                else                       m_saveLayoutDirty = true;
            } else if (m_appState == AppState::CREDITS) {
                // Credits are the end of the game; ESC is way out
                glfwSetWindowShouldClose(window, true);
            }
        }
        m_escKeyWasDown = escKeyDown;

        // F is a letter while typing a save name
        bool fKeyDown = m_appState != AppState::SAVE_NAME_ENTRY &&
                        glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        if (fKeyDown && !m_fKeyWasDown) {
            windowManager.toggleFullscreen();
        }
        m_fKeyWasDown = fKeyDown;

        // F1: stable ~30 fps instead of an unstable "up to 60" on thermally limited hardware
        // (see README_PERF.txt)
        bool f1KeyDown = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
        if (f1KeyDown && !m_f1KeyWasDown) {
            windowManager.togglePerformanceMode();
            std::printf(
                "[perf] performance mode = %s\n",
                windowManager.isPerformanceMode() ? "ON (~30fps cap)" : "OFF (~60fps cap)"
            );
        }
        m_f1KeyWasDown = f1KeyDown;

        // F3: vsync off (see README_PERF.txt).
        const bool f3KeyDown = glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS;
        if (f3KeyDown && !m_f3KeyWasDown) {
            windowManager.toggleUncapped();
            std::printf("[perf] vsync = %s\n", windowManager.isUncapped() ? "OFF (benchmark)" : "ON");
        }
        m_f3KeyWasDown = f3KeyDown;

        // The story and credits screens show no scene, so they skip the ASCII pipeline.
        const bool sceneVisible = m_appState != AppState::STORY_TEXT && m_appState != AppState::CREDITS;
        AsciiEffect::RenderView renderView = (m_shadersEnabled && sceneVisible)
            ? AsciiEffect::RenderView::Ascii
            : AsciiEffect::RenderView::Plain;
#ifdef DEV_TOOLS_ACTIVE
        // T is a letter while typing a save name. A dev view overrides the player's choice.
        if (m_appState != AppState::SAVE_NAME_ENTRY)
            DevTools::CycleRenderView(window);
        if (DevTools::s_renderView != 0)
            renderView = static_cast<AsciiEffect::RenderView>(DevTools::s_renderView);
#endif
        ascii.setRenderView(renderView);

        const bool gameplayActive =
            (m_appState == AppState::FADE_TO_GAME || m_appState == AppState::PLAYING);

        // Apply slider values before update() fills the next chunk
        AudioMixer::instance().setMusicVolume(m_musicVolume);
        AudioMixer::instance().setMasterVolume(m_masterVolume);

        // Every frame, even while gameplay logic is paused
        AudioMixer::instance().update(deltaTime);

        scene.tickAmbientMusic(deltaTime, gameplayActive);

        // No mouse look while reading: stray mouse movement would turn the camera away from where
        // the diary was
        m_mouseLookEnabled = gameplayActive && !scene.isReadingOverlayOpen();

        // Cursor free in menus, captured for mouse look in gameplay
        if (gameplayActive != m_cursorCaptured) {
            if (gameplayActive) {
                // Reset the mouse-look baseline first, or the cursor jump reads as a camera turn
                scene.resetMouseLook();
            }

            glfwSetInputMode(
                window,
                GLFW_CURSOR,
                gameplayActive ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
            );

            m_cursorCaptured = gameplayActive;
        }

        // SETTINGS draws over the screen it was opened from
        const bool settingsFromPause =
            m_appState == AppState::SETTINGS && m_settingsReturnState == AppState::PAUSED;

        if (gameplayActive && scene.isReadingOverlayOpen()) {
            // Reading: movement and E are frozen
            scene.tickReadingOverlayInput(window);
            scene.tickPauseCameraIdle(deltaTime);
        } else if (gameplayActive) {
            scene.processInput(window, deltaTime);

            // In the same frame processInput() may have set it
            if (m_appState == AppState::PLAYING && scene.consumeCreditsRequest()) {
                m_pendingAction = PendingAction::SHOW_CREDITS;
                m_appState = AppState::FADE_TO_BLACK;
            }
        } else if (m_appState == AppState::PAUSED || settingsFromPause) {
            // Paused: the camera keeps its idle motion
            scene.tickPauseCameraIdle(deltaTime);
        }
        // Menu and fades: the camera stays still

        // Stamina finishes draining in step with the death fade; processInput() is no longer
        // running here
        if (!gameplayActive) {
            scene.tickDeathFade(deltaTime);
        }

        // Periodic autosave during gameplay
        if (m_appState == AppState::PLAYING) {
            m_autosaveTimer += deltaTime;
            if (m_autosaveTimer >= kAutosaveIntervalSeconds) {
                m_autosaveTimer = 0.0f;
                scene.saveActiveSlot();
            }
        }

        // Death fades out without saving
        const bool canProcessDeathFade =
            m_appState == AppState::PLAYING || m_appState == AppState::PAUSED;
        if (canProcessDeathFade && scene.consumeDeathFadeTrigger()) {
            // DIED fades without a pause menu and generates a fresh map
            m_pendingAction = PendingAction::DIED;
            ++m_menuOpenCount;
            m_menuVariant = MainMenu::PickAtmosphereVariant(m_menuOpenCount, m_menuSeed);
            m_menuLayoutDirty = true;
            m_appState = AppState::FADE_TO_BLACK;
        }

        // Title shatter animation; start screen only
        const bool mainMenuVisible =
            (m_appState == AppState::MENU) ||
            (m_appState == AppState::FADE_TO_MENU) ||
            (m_appState == AppState::FADE_TO_BLACK &&
             m_pendingAction != PendingAction::RETURN_TO_MENU &&
             m_pendingAction != PendingAction::DIED);

        if (mainMenuVisible) {
            MainMenu::UpdateTitleBreakup(m_titleBreakup, deltaTime);

            if (!m_shadersUnlocked && m_titleBreakup.fullFalling &&
                m_titleBreakup.fallTime >= m_titleBreakup.fallDuration) {
                m_shadersUnlocked = true;
                m_settingsLayoutDirty = true;
            }

            // The overlay must follow the moving title
            if (MainMenu::HasActiveTitleAnimation(m_titleBreakup) ||
                m_titleBreakup.clickCount > 0) {
                m_menuLayoutDirty = true;
            }
        }

        // Menu and pause
        if (m_appState == AppState::MENU || m_appState == AppState::PAUSED ||
            m_appState == AppState::SETTINGS || m_appState == AppState::CONTINUE_SELECT ||
            m_appState == AppState::SAVE_SELECT || m_appState == AppState::SAVE_CONFIRM ||
            m_appState == AppState::NEWGAME_SELECT || m_appState == AppState::NEWGAME_CONFIRM ||
            m_appState == AppState::SAVE_NAME_ENTRY) {
            // Cursor to menu-grid cells
            double mx = 0.0, my = 0.0;
            glfwGetCursorPos(window, &mx, &my);

            int winW = 0, winH = 0;
            glfwGetFramebufferSize(window, &winW, &winH);

            const int cellSize = AsciiEffect::menuCellSizeForWindow(winW, winH);
            const int uiRows = ascii.getMenuGridRowsForWindow(winW, winH);

            int hoverCol = -1, hoverRow = -1;
            if (cellSize > 0 && uiRows > 0) {
                hoverCol = (int)std::floor(mx / (double)cellSize);
                // The shader counts rows bottom-up, the layout top-down
                const int cellIndexY = (int)std::floor((winH - my) / (double)cellSize);
                hoverRow = uiRows - 1 - cellIndexY;
            }

            auto insideRect = [&](const MainMenu::ButtonRect& r) {
                return hoverCol >= r.x0 && hoverCol <= r.x1 &&
                       hoverRow >= r.y0 && hoverRow <= r.y1;
            };

            const bool confirmDown =
                (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS ||
                 glfwGetKey(window, GLFW_KEY_ENTER)    == GLFW_PRESS ||
                 glfwGetKey(window, GLFW_KEY_KP_ENTER) == GLFW_PRESS ||
                 glfwGetKey(window, GLFW_KEY_SPACE)    == GLFW_PRESS);

            if (m_appState == AppState::SETTINGS) {
                const bool newBackHovered = insideRect(m_settingsLayout.backButton);
                const bool newSliderHovered = insideRect(m_settingsLayout.sliderPanel);
                const bool newSharpnessSliderHovered = insideRect(m_settingsLayout.sharpnessSliderPanel);
                const bool newMusicSliderHovered = insideRect(m_settingsLayout.musicSliderPanel);
                const bool newMasterSliderHovered = insideRect(m_settingsLayout.masterSliderPanel);
                const bool newColorCheckboxHovered = insideRect(m_settingsLayout.colorCheckbox);
                const bool newLensCheckboxHovered = insideRect(m_settingsLayout.lensCheckbox);
                const bool newCrtCheckboxHovered = insideRect(m_settingsLayout.crtCheckbox);
                const bool newShadersCheckboxHovered =
                    m_settingsLayout.hasShadersCheckbox && insideRect(m_settingsLayout.shadersCheckbox);
                if (newBackHovered != m_backHovered || newSliderHovered != m_sliderHovered ||
                    newSharpnessSliderHovered != m_sharpnessSliderHovered ||
                    newMusicSliderHovered != m_musicSliderHovered ||
                    newMasterSliderHovered != m_masterSliderHovered ||
                    newColorCheckboxHovered != m_colorCheckboxHovered ||
                    newLensCheckboxHovered != m_lensCheckboxHovered ||
                    newCrtCheckboxHovered != m_crtCheckboxHovered ||
                    newShadersCheckboxHovered != m_shadersCheckboxHovered) {
                    m_backHovered = newBackHovered;
                    m_sliderHovered = newSliderHovered;
                    m_sharpnessSliderHovered = newSharpnessSliderHovered;
                    m_musicSliderHovered = newMusicSliderHovered;
                    m_masterSliderHovered = newMasterSliderHovered;
                    m_colorCheckboxHovered = newColorCheckboxHovered;
                    m_lensCheckboxHovered = newLensCheckboxHovered;
                    m_crtCheckboxHovered = newCrtCheckboxHovered;
                    m_shadersCheckboxHovered = newShadersCheckboxHovered;
                    m_settingsLayoutDirty = true;
                }

                const bool lmbDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

                // A drag starts on the slider panel and holds while LMB is down, even if the cursor
                // leaves the track.
                if (lmbDown && !m_sliderDragging && newSliderHovered) {
                    m_sliderDragging = true;
                }
                if (!lmbDown) {
                    m_sliderDragging = false;
                }

                if (m_sliderDragging) {
                    const int trackSpan = std::max(1, m_settingsLayout.trackX1 - m_settingsLayout.trackX0);
                    const float t = (float)(hoverCol - m_settingsLayout.trackX0) / (float)trackSpan;
                    scene.setMouseSensitivity(slider01ToSensitivity(t));
                    m_settingsLayoutDirty = true;
                }

                // SHARPNESS rebuilds glyph atlases; the value is an int, so that happens far less
                // often than every frame
                if (lmbDown && !m_sharpnessSliderDragging && newSharpnessSliderHovered) {
                    m_sharpnessSliderDragging = true;
                }
                if (!lmbDown) {
                    m_sharpnessSliderDragging = false;
                }

                if (m_sharpnessSliderDragging) {
                    const int trackSpan = std::max(1, m_settingsLayout.sharpnessTrackX1 - m_settingsLayout.sharpnessTrackX0);
                    const float t = (float)(hoverCol - m_settingsLayout.sharpnessTrackX0) / (float)trackSpan;
                    ascii.setUserCellSize(slider01ToCellSize(t));
                    m_settingsLayoutDirty = true;
                }

                // MUSIC: plain 0..1 volume, stored in Application
                if (lmbDown && !m_musicSliderDragging && newMusicSliderHovered) {
                    m_musicSliderDragging = true;
                }
                if (!lmbDown) {
                    m_musicSliderDragging = false;
                }

                if (m_musicSliderDragging) {
                    const int trackSpan = std::max(1, m_settingsLayout.musicTrackX1 - m_settingsLayout.musicTrackX0);
                    const float t = (float)(hoverCol - m_settingsLayout.musicTrackX0) / (float)trackSpan;
                    m_musicVolume = std::clamp(t, 0.0f, 1.0f);
                    m_settingsLayoutDirty = true;
                }

                if (lmbDown && !m_masterSliderDragging && newMasterSliderHovered) {
                    m_masterSliderDragging = true;
                }
                if (!lmbDown) {
                    m_masterSliderDragging = false;
                }

                if (m_masterSliderDragging) {
                    const int trackSpan = std::max(1, m_settingsLayout.masterTrackX1 - m_settingsLayout.masterTrackX0);
                    const float t = (float)(hoverCol - m_settingsLayout.masterTrackX0) / (float)trackSpan;
                    m_masterVolume = std::clamp(t, 0.0f, 1.0f);
                    m_settingsLayoutDirty = true;
                }

                // COLOR: edge-triggered toggle
                if (confirmDown && !m_confirmKeyWasDown && !m_sliderDragging && !m_sharpnessSliderDragging && m_colorCheckboxHovered) {
                    m_colorEnabled = !m_colorEnabled;
                    m_settingsLayoutDirty = true;
                }

                if (confirmDown && !m_confirmKeyWasDown && !m_sliderDragging && !m_sharpnessSliderDragging && m_lensCheckboxHovered) {
                    m_lensEnabled = !m_lensEnabled;
                    m_settingsLayoutDirty = true;
                    std::printf("[lens] toggled -> %s\n", m_lensEnabled ? "ON" : "OFF");
                }

                if (confirmDown && !m_confirmKeyWasDown && !m_sliderDragging && !m_sharpnessSliderDragging && m_crtCheckboxHovered) {
                    m_crtEnabled = !m_crtEnabled;
                    m_settingsLayoutDirty = true;
                }

                if (confirmDown && !m_confirmKeyWasDown && !m_sliderDragging && !m_sharpnessSliderDragging && m_shadersCheckboxHovered) {
                    m_shadersEnabled = !m_shadersEnabled;
                    m_settingsLayoutDirty = true;
                }

                if (confirmDown && !m_confirmKeyWasDown && !m_sliderDragging && !m_sharpnessSliderDragging && m_backHovered) {
                    m_appState = m_settingsReturnState;
                    m_backHovered = false;
                    m_sliderHovered = false;
                    m_sharpnessSliderHovered = false;
                    m_musicSliderHovered = false;
                    m_masterSliderHovered = false;
                    m_colorCheckboxHovered = false;
                    m_lensCheckboxHovered = false;
                    m_crtCheckboxHovered = false;
                    m_shadersCheckboxHovered = false;
                }
            } else if (m_appState == AppState::CONTINUE_SELECT) {
                // Only filled slots can be hovered or clicked
                int newHoveredButton = -1;
                for (int i = 0; i < 3; ++i) {
                    if (m_continueLayout.slotFilled[i] && insideRect(m_continueLayout.slotButtons[i])) {
                        newHoveredButton = i;
                    }
                }
                if (newHoveredButton == -1 && insideRect(m_continueLayout.backButton)) {
                    newHoveredButton = 3;
                }

                if (newHoveredButton != m_hoveredButton) {
                    m_hoveredButton = newHoveredButton;
                    m_continueLayoutDirty = true;
                }

                if (confirmDown && !m_confirmKeyWasDown && m_hoveredButton != -1) {
                    if (m_hoveredButton == 3) {
                        m_appState = AppState::MENU;
                        m_hoveredButton = -1;
                    } else {
                        // Load through the black fade; the map is rebuilt once the screen is black
                        m_pendingLoadSlot = m_hoveredButton;
                        m_pendingAction = PendingAction::LOAD_GAME;
                        m_appState = AppState::FADE_TO_BLACK;
                        m_hoveredButton = -1;
                    }
                }
            } else if (m_appState == AppState::SAVE_SELECT) {
                // All slots are clickable: an empty one goes to name entry, a filled one asks to
                // overwrite first
                int newHoveredButton = -1;
                for (int i = 0; i < 3; ++i) {
                    if (insideRect(m_saveLayout.slotButtons[i])) {
                        newHoveredButton = i;
                    }
                }
                if (newHoveredButton == -1 && insideRect(m_saveLayout.backButton)) {
                    newHoveredButton = 3;
                }

                if (newHoveredButton != m_hoveredButton) {
                    m_hoveredButton = newHoveredButton;
                    m_saveLayoutDirty = true;
                }

                if (confirmDown && !m_confirmKeyWasDown && m_hoveredButton != -1) {
                    if (m_hoveredButton == 3) {
                        m_appState = AppState::PAUSED;
                        m_hoveredButton = -1;
                    } else if (m_saveLayout.slotFilled[m_hoveredButton]) {
                        m_pendingSaveSlot = m_hoveredButton;
                        m_appState = AppState::SAVE_CONFIRM;
                        m_hoveredButton = -1;
                        m_saveConfirmLayoutDirty = true;
                    } else {
                        m_pendingSaveSlot = m_hoveredButton;
                        // May be left over from a NEW GAME flow
                        m_nameEntryForNewGame = false;
                        m_saveNameBuffer.clear();
                        m_appState = AppState::SAVE_NAME_ENTRY;
                        m_hoveredButton = -1;
                        m_backHoveredNameEntry = false;
                        m_confirmHoveredNameEntry = false;
                        m_nameEntryLayoutDirty = true;
                    }
                }
            } else if (m_appState == AppState::SAVE_CONFIRM) {
                int newHoveredButton = -1;
                if (insideRect(m_saveConfirmLayout.yesButton)) newHoveredButton = 0;
                else if (insideRect(m_saveConfirmLayout.noButton)) newHoveredButton = 1;

                if (newHoveredButton != m_hoveredButton) {
                    m_hoveredButton = newHoveredButton;
                    m_saveConfirmLayoutDirty = true;
                }

                if (confirmDown && !m_confirmKeyWasDown && m_hoveredButton != -1) {
                    if (m_hoveredButton == 0) {
                        // Name entry, prefilled with the slot's current name
                        m_saveNameBuffer = SaveSystem::LoadSlot(m_pendingSaveSlot).name;
                        m_nameEntryForNewGame = false; // reset, see SAVE_SELECT above
                        m_appState = AppState::SAVE_NAME_ENTRY;
                        m_backHoveredNameEntry = false;
                        m_confirmHoveredNameEntry = false;
                        m_nameEntryLayoutDirty = true;
                    } else {
                        m_pendingSaveSlot = -1;
                        m_appState = AppState::SAVE_SELECT;
                        m_saveLayoutDirty = true;
                    }
                    m_hoveredButton = -1;
                }
            } else if (m_appState == AppState::NEWGAME_SELECT) {
                // Same as SAVE_SELECT, for NEW GAME
                int newHoveredButton = -1;
                for (int i = 0; i < 3; ++i) {
                    if (insideRect(m_newGameLayout.slotButtons[i])) {
                        newHoveredButton = i;
                    }
                }
                if (newHoveredButton == -1 && insideRect(m_newGameLayout.backButton)) {
                    newHoveredButton = 3;
                }
                // 4.. = difficulty picker entries
                for (int d = 0; d < kDifficultyCount && newHoveredButton == -1; ++d) {
                    if (insideRect(m_newGameLayout.difficultyButtons[d])) {
                        newHoveredButton = 4 + d;
                    }
                }

                if (newHoveredButton != m_hoveredButton) {
                    m_hoveredButton = newHoveredButton;
                    m_newGameLayoutDirty = true;
                }

                if (confirmDown && !m_confirmKeyWasDown && m_hoveredButton != -1) {
                    if (m_hoveredButton >= 4) {
                        m_selectedDifficulty = (Difficulty)(m_hoveredButton - 4);
                        m_newGameLayoutDirty = true;
                    } else if (m_hoveredButton == 3) {
                        m_appState = AppState::MENU;
                        m_hoveredButton = -1;
                    } else if (m_newGameLayout.slotFilled[m_hoveredButton]) {
                        m_pendingNewGameSlot = m_hoveredButton;
                        m_appState = AppState::NEWGAME_CONFIRM;
                        m_hoveredButton = -1;
                        m_newGameConfirmLayoutDirty = true;
                    } else {
                        m_pendingNewGameSlot = m_hoveredButton;
                        m_nameEntryForNewGame = true;
                        m_saveNameBuffer.clear();
                        m_appState = AppState::SAVE_NAME_ENTRY;
                        m_hoveredButton = -1;
                        m_backHoveredNameEntry = false;
                        m_confirmHoveredNameEntry = false;
                        m_nameEntryLayoutDirty = true;
                    }
                }
            } else if (m_appState == AppState::NEWGAME_CONFIRM) {
                int newHoveredButton = -1;
                if (insideRect(m_newGameConfirmLayout.yesButton)) newHoveredButton = 0;
                else if (insideRect(m_newGameConfirmLayout.noButton)) newHoveredButton = 1;

                if (newHoveredButton != m_hoveredButton) {
                    m_hoveredButton = newHoveredButton;
                    m_newGameConfirmLayoutDirty = true;
                }

                if (confirmDown && !m_confirmKeyWasDown && m_hoveredButton != -1) {
                    if (m_hoveredButton == 0) {
                        // A new game starts with an empty name
                        m_nameEntryForNewGame = true;
                        m_saveNameBuffer.clear();
                        m_appState = AppState::SAVE_NAME_ENTRY;
                        m_backHoveredNameEntry = false;
                        m_confirmHoveredNameEntry = false;
                        m_nameEntryLayoutDirty = true;
                    } else {
                        m_pendingNewGameSlot = -1;
                        m_appState = AppState::NEWGAME_SELECT;
                        m_newGameLayoutDirty = true;
                    }
                    m_hoveredButton = -1;
                }
            } else if (m_appState == AppState::SAVE_NAME_ENTRY) {
                // Mouse handles BACK and OK only; typing is below
                const bool newBackHovered = insideRect(m_nameEntryLayout.backButton);
                const bool newConfirmHovered = insideRect(m_nameEntryLayout.confirmButton);
                if (newBackHovered != m_backHoveredNameEntry || newConfirmHovered != m_confirmHoveredNameEntry) {
                    m_backHoveredNameEntry = newBackHovered;
                    m_confirmHoveredNameEntry = newConfirmHovered;
                    m_nameEntryLayoutDirty = true;
                }
                if (confirmDown && !m_confirmKeyWasDown) {
                    if (m_backHoveredNameEntry) {
                        m_appState = m_nameEntryForNewGame ? AppState::NEWGAME_SELECT : AppState::SAVE_SELECT;
                        m_saveNameBuffer.clear();
                        if (m_nameEntryForNewGame) m_newGameLayoutDirty = true;
                        else                       m_saveLayoutDirty = true;
                    } else if (m_confirmHoveredNameEntry) {
                        confirmNameEntry(scene);
                    }
                }
            } else {
                int newHoveredButton = -1;
                bool newTitleHovered = false;
                if (m_appState == AppState::MENU) {
                    if (insideRect(m_menuLayout.titleBoxRect)) {
                        newTitleHovered = true;
                    } else if (insideRect(m_menuLayout.newGameButton)) {
                        newHoveredButton = 0;
                    } else if (insideRect(m_menuLayout.continueButton)) {
                        newHoveredButton = 1;
                    } else if (insideRect(m_menuLayout.settingsButton)) {
                        newHoveredButton = 2;
                    } else if (insideRect(m_menuLayout.exitButton)) {
                        newHoveredButton = 3;
                    }
                } else {
                    if (insideRect(m_pauseLayout.resumeButton))         newHoveredButton = 0;
                    else if (insideRect(m_pauseLayout.saveButton))      newHoveredButton = 1;
                    else if (insideRect(m_pauseLayout.settingsButton))  newHoveredButton = 2;
                    else if (insideRect(m_pauseLayout.menuButton))      newHoveredButton = 3;
                }

                if (newHoveredButton != m_hoveredButton || newTitleHovered != m_titleHovered) {
                    m_hoveredButton = newHoveredButton;
                    m_titleHovered = newTitleHovered;
                    if (m_appState == AppState::MENU) m_menuLayoutDirty = true;
                    else                              m_pauseLayoutDirty = true;
                }

                if (confirmDown && !m_confirmKeyWasDown) {
                    if (m_appState == AppState::MENU && m_titleHovered) {
                        // The title is a click area that shatters the logo. Hit test uses
                        // titleBoxRect, the particles originate from titleRect (the text itself)
                        const int titleScaleBase = std::max(1, uiRows / 60);
                        const float titleScale = (float)titleScaleBase * 1.5f;

                        MainMenu::ApplyTitleClick(
                            m_titleBreakup,
                            std::max(0, ascii.getMenuGridColsForWindow(winW, winH)),
                            std::max(0, uiRows),
                            "CELL",
                            m_menuLayout.titleRect.x0,
                            m_menuLayout.titleRect.y0,
                            titleScale,
                            m_menuSeed
                        );

                        m_menuLayoutDirty = true;
                    } else if (m_hoveredButton != -1) {
                        if (m_appState == AppState::MENU) {
                            // NEW GAME and CONTINUE open a slot picker; SETTINGS is instant; EXIT
                            // closes the app
                            if (m_hoveredButton == 0) {
                                m_appState = AppState::NEWGAME_SELECT;
                                m_hoveredButton = -1;
                                m_newGameLayoutDirty = true;
                            } else if (m_hoveredButton == 1) {
                                m_appState = AppState::CONTINUE_SELECT;
                                m_hoveredButton = -1;
                                m_continueLayoutDirty = true;
                            } else if (m_hoveredButton == 2) {
                                m_settingsReturnState = AppState::MENU;
                                m_appState = AppState::SETTINGS;
                                m_hoveredButton = -1;
                                m_backHovered = false;
                                m_sliderHovered = false;
                                m_settingsLayoutDirty = true;
                            } else {
                                m_pendingAction = PendingAction::QUIT_APP;
                                m_appState = AppState::FADE_TO_BLACK;
                            }
                        } else {
                            // RESUME, SAVE and SETTINGS are instant; MENU fades to the start screen
                            if (m_hoveredButton == 0) {
                                m_appState = AppState::PLAYING;
                            } else if (m_hoveredButton == 1) {
                                m_appState = AppState::SAVE_SELECT;
                                m_hoveredButton = -1;
                                m_saveLayoutDirty = true;
                            } else if (m_hoveredButton == 2) {
                                m_settingsReturnState = AppState::PAUSED;
                                m_appState = AppState::SETTINGS;
                                m_hoveredButton = -1;
                                m_backHovered = false;
                                m_sliderHovered = false;
                                m_settingsLayoutDirty = true;
                            } else {
                                // Pick the menu variant now so the fade uses it from its first
                                // frame; autosave before leaving
                                scene.saveActiveSlot();
                                m_pendingAction = PendingAction::RETURN_TO_MENU;
                                ++m_menuOpenCount;
                                m_menuVariant = MainMenu::PickAtmosphereVariant(m_menuOpenCount, m_menuSeed);
                                m_menuLayoutDirty = true;
                                m_appState = AppState::FADE_TO_BLACK;
                            }
                        }
                    }
                }
            }

            // Interface sounds
            const int hoverId = currentUiHoverId();
            if (confirmDown && !m_confirmKeyWasDown && m_uiHoverId != -1)
                UiAudio::PlayClick();
            else if (!confirmDown && hoverId != -1 && hoverId != m_uiHoverId)
                UiAudio::PlayHover();
            m_uiHoverId = hoverId;

            m_confirmKeyWasDown = confirmDown;
        } else {
            m_uiHoverId = -1;
        }

        // Save-name entry÷
        if (m_appState == AppState::SAVE_NAME_ENTRY) {
            auto keyPressed = [&](int glfwKey) {
                const bool down = glfwGetKey(window, glfwKey) == GLFW_PRESS;
                const bool wasDown = m_textEntryKeyWasDown[glfwKey];
                m_textEntryKeyWasDown[glfwKey] = down;
                return down && !wasDown;
            };

            bool bufferChanged = false;

            // The characters BigFont can draw
            for (int k = GLFW_KEY_A; k <= GLFW_KEY_Z; ++k) {
                if (keyPressed(k) && (int)m_saveNameBuffer.size() < SaveSystem::kNameMaxLen) {
                    m_saveNameBuffer.push_back((char)('A' + (k - GLFW_KEY_A)));
                    bufferChanged = true;
                }
            }
            for (int k = GLFW_KEY_0; k <= GLFW_KEY_9; ++k) {
                if (keyPressed(k) && (int)m_saveNameBuffer.size() < SaveSystem::kNameMaxLen) {
                    m_saveNameBuffer.push_back((char)('0' + (k - GLFW_KEY_0)));
                    bufferChanged = true;
                }
            }
            if (keyPressed(GLFW_KEY_BACKSPACE) && !m_saveNameBuffer.empty()) {
                m_saveNameBuffer.pop_back();
                bufferChanged = true;
            }
            if (bufferChanged) {
                m_nameEntryLayoutDirty = true;
            }

            if (keyPressed(GLFW_KEY_ENTER)) {
                confirmNameEntry(scene);
            }
        }

        if (m_appState == AppState::FADE_TO_BLACK) {
            const float fadeOutSpeed =
                (m_pendingAction == PendingAction::DIED) ? kDeathFadeOutSpeed : kFadeOutSpeed;
            m_fadeAlpha += fadeOutSpeed * deltaTime;
            if (m_fadeAlpha >= 1.0f) {
                m_fadeAlpha = 1.0f;

                // Fully black: run the pending action. QUIT_APP is the only place the app closes
                if (m_pendingAction == PendingAction::QUIT_APP) {
                    glfwSetWindowShouldClose(window, true);
                } else if (m_pendingAction == PendingAction::RETURN_TO_MENU) {
                    m_appState = AppState::FADE_TO_MENU;
                } else if (m_pendingAction == PendingAction::SHOW_CREDITS) {
                    startStory(/*leadsToCredits=*/true);
                } else if (m_pendingAction == PendingAction::DIED) {
                    // Fresh map, the menu is not backed by the death scene; newGame() also
                    // resets the player
                    scene.newGame();
                    m_appState = AppState::FADE_TO_MENU;
                } else if (m_pendingAction == PendingAction::NEW_GAME) {
                    // Fallback: UI always goes through NEW_GAME_IN_SLOT
                    scene.newGame();
                    m_autosaveTimer = 0.0f;
                    m_appState = AppState::FADE_TO_GAME;
                } else if (m_pendingAction == PendingAction::NEW_GAME_IN_SLOT) {
                    // Slot, name and difficulty were chosen before the fade
                    scene.setDifficulty(m_selectedDifficulty);
                    scene.newGame(m_pendingNewGameSlot, m_saveNameBuffer);
                    m_pendingNewGameSlot = -1;
                    m_saveNameBuffer.clear();
                    m_nameEntryForNewGame = false;
                    m_autosaveTimer = 0.0f;
                    startStory(/*leadsToCredits=*/false);
                } else if (m_pendingAction == PendingAction::LOAD_GAME) {
                    if (!scene.loadSlot(m_pendingLoadSlot)) {
                        scene.newGame();
                    }
                    m_pendingLoadSlot = -1;
                    m_autosaveTimer = 0.0f;
                    m_appState = AppState::FADE_TO_GAME;
                } else { // NONE, shouldn't happen
                    m_appState = AppState::FADE_TO_GAME;
                }
            }
        } else if (m_appState == AppState::FADE_TO_GAME) {
            m_fadeAlpha -= kFadeInSpeed * deltaTime;
            if (m_fadeAlpha <= 0.0f) {
                m_fadeAlpha = 0.0f;
                m_appState = AppState::PLAYING;
                m_pendingAction = PendingAction::NONE;
            }
        } else if (m_appState == AppState::FADE_TO_MENU) {
            m_fadeAlpha -= kFadeInSpeed * deltaTime;
            if (m_fadeAlpha <= 0.0f) {
                m_fadeAlpha = 0.0f;
                m_appState = AppState::MENU;
                m_pendingAction = PendingAction::NONE;
                m_hoveredButton = -1;
                m_titleHovered = false;
                m_menuLayoutDirty = true;
                // The variant was picked when MENU was clicked
            }
        } else if (m_appState == AppState::STORY_TEXT) {
            const bool spaceDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
            if (!spaceDown)
                m_storySkipArmed = true;
            m_storySkipHold = (spaceDown && m_storySkipArmed) ? m_storySkipHold + deltaTime : 0.0f;
            if (m_storySkipHold >= kStorySkipHoldSeconds)
                m_storyText.skip();

            m_storyText.update(deltaTime);
            if (m_storyText.finished()) {
                m_fadeAlpha = 1.0f;
                if (m_storyLeadsToCredits) {
                    // Credits fade in from black; the camera moves outside the map so only the
                    // clear color shows behind the text.
                    scene.teleportCameraForCredits();
                    m_appState = AppState::CREDITS;
                    m_creditsScrollPx = 0.0f;
                } else {
                    m_appState = AppState::FADE_TO_GAME;
                }
            }
        } else if (m_appState == AppState::CREDITS) {
            // Fade in and stay on CREDITS
            if (m_fadeAlpha > 0.0f) {
                m_fadeAlpha -= kFadeInSpeed * deltaTime;
                if (m_fadeAlpha < 0.0f) m_fadeAlpha = 0.0f;
            }
        }

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);

        const bool noclipEnabled = gameplayActive && scene.isNoclipEnabled();

        m_gpuTimer.begin(GpuTimer::Scene);
        ascii.begin();
        if (m_appState != AppState::STORY_TEXT)
            scene.render(ascii.sceneWidth(), ascii.sceneHeight(), gameplayActive);
        m_gpuTimer.end();

        // Stamina bar (fill = stamina, dripping frame = health) during gameplay; hidden in noclip
        ascii.setStamina(scene.getStaminaFraction(), /*enabled=*/gameplayActive && !noclipEnabled);
        ascii.setHealth(scene.getHealthFraction());

        ascii.setWallGlitch(
            gameplayActive && !noclipEnabled && scene.glyphGlitchActive(),
            scene.glyphGlitchUV().x, scene.glyphGlitchUV().y,
            scene.glyphGlitchRadiusCells());

        // UI overlay for the current state. The FADE_TO_BLACK overlay depends on what triggered it
        // SETTINGS shows the same overlay wherever it was opened from
        enum class OverlayMode { NONE, MAIN_MENU, CONTINUE_MENU, SAVE_MENU, SAVE_CONFIRM_MENU, SAVE_NAME_MENU, NEWGAME_MENU, NEWGAME_CONFIRM_MENU, PAUSE_MENU, SETTINGS_MENU, DIARY_READING, DIARY_HINT, WIN_BLOCKED_MSG, WIN_READY_MSG, WIN_ACTIVATE_HINT, TORCH_HINT, TORCH_EMPTY_MSG, CREDITS_SCREEN, STORY_SCREEN };
        OverlayMode overlay = OverlayMode::NONE;
        if (m_appState == AppState::MENU || m_appState == AppState::FADE_TO_MENU) {
            overlay = OverlayMode::MAIN_MENU;
        } else if (m_appState == AppState::CREDITS) {
            overlay = OverlayMode::CREDITS_SCREEN;
        } else if (m_appState == AppState::STORY_TEXT) {
            overlay = OverlayMode::STORY_SCREEN;
        } else if (m_appState == AppState::CONTINUE_SELECT) {
            overlay = OverlayMode::CONTINUE_MENU;
        } else if (m_appState == AppState::SAVE_SELECT) {
            overlay = OverlayMode::SAVE_MENU;
        } else if (m_appState == AppState::SAVE_CONFIRM) {
            overlay = OverlayMode::SAVE_CONFIRM_MENU;
        } else if (m_appState == AppState::NEWGAME_SELECT) {
            overlay = OverlayMode::NEWGAME_MENU;
        } else if (m_appState == AppState::NEWGAME_CONFIRM) {
            overlay = OverlayMode::NEWGAME_CONFIRM_MENU;
        } else if (m_appState == AppState::SAVE_NAME_ENTRY) {
            overlay = OverlayMode::SAVE_NAME_MENU;
        } else if (m_appState == AppState::FADE_TO_BLACK) {
            if (m_pendingAction == PendingAction::RETURN_TO_MENU) {
                overlay = OverlayMode::PAUSE_MENU;
            } else if (m_pendingAction == PendingAction::LOAD_GAME) {
                overlay = OverlayMode::CONTINUE_MENU;
            } else if (m_pendingAction == PendingAction::DIED) {
                // Death during gameplay: just darken.
                overlay = OverlayMode::NONE;
            } else if (m_pendingAction == PendingAction::SHOW_CREDITS) {
                overlay = OverlayMode::NONE; // same logic as DIED
            } else {
                overlay = OverlayMode::MAIN_MENU;
            }
        } else if (m_appState == AppState::PAUSED) {
            overlay = OverlayMode::PAUSE_MENU;
        } else if (m_appState == AppState::SETTINGS) {
            overlay = OverlayMode::SETTINGS_MENU;
        }

        // The diary screen is an overlay on PLAYING, not an AppState
        if (m_appState == AppState::PLAYING && scene.isReadingOverlayOpen()) {
            overlay = OverlayMode::DIARY_READING;
        } else if (m_appState == AppState::PLAYING && scene.winReadyToPressE()) {
            // Highest priority hint; the player cannot be at two points of interest at once
            overlay = OverlayMode::WIN_READY_MSG;
        } else if (m_appState == AppState::PLAYING && scene.nearExitDoorReadyToActivate()) {
            overlay = OverlayMode::WIN_ACTIVATE_HINT;
        } else if (m_appState == AppState::PLAYING && scene.showWinBlockedMessage()) {
            overlay = OverlayMode::WIN_BLOCKED_MSG; // short-lived; takes priority over DIARY_HINT
        } else if (m_appState == AppState::PLAYING && scene.nearbyDiaryIndex() != -1) {
            overlay = OverlayMode::DIARY_HINT;
        } else if (m_appState == AppState::PLAYING && scene.showTorchEmptyMessage()) {
            overlay = OverlayMode::TORCH_EMPTY_MSG;
        } else if (m_appState == AppState::PLAYING && scene.nearbyWallTorchIndex() != -1) {
            overlay = OverlayMode::TORCH_HINT;
        }

        // Grid from the real window size, not the 1280x720 scene FBO, or menu text drifts on
        // resize. Also needed for the HUD icons during gameplay
        const int cols = ascii.getMenuGridColsForWindow(w, h);
        const int rows = ascii.getMenuGridRowsForWindow(w, h);
        const size_t expectedGridSize = (size_t)std::max(0, cols) * std::max(0, rows);

        // Item icons show with the stamina bar and hide while reading
        const bool showHudIcons =
            gameplayActive && !scene.isNoclipEnabled() && !scene.isReadingOverlayOpen();

        // The icon row depends on the live cell size, so the grid is cleared every frame
        if (m_hudIconsGridScratch.size() != expectedGridSize) {
            m_hudIconsGridScratch.assign(expectedGridSize, 0);
        }
        if (showHudIcons) {
            std::fill(m_hudIconsGridScratch.begin(), m_hudIconsGridScratch.end(), 0);
            // The stamina bar lives in the live scene grid, the icons in the menu grid; converted
            // through window pixels
            const int liveCellSize = ascii.getUserCellSize();
            const float staminaBarTopPx = 6.0f * (float)liveCellSize; // BOTTOM_PAD(2)+4 rows
            const float staminaBarTopFrac = (h > 0) ? (staminaBarTopPx / (float)h) : 0.0f;
            const int hudIconGap = std::max(1, rows / 40);
            const int hudIconBottomRow =
                rows - (int)std::lround(staminaBarTopFrac * (float)rows) - hudIconGap;

            MainMenu::DrawHudIcons(m_hudIconsGridScratch, cols, rows,
                                    scene.playerTorchInventoryCount(),
                                    scene.playerStoneCount(),
                                    scene.diariesReadCount(),
                                    hudIconBottomRow, m_menuSeed);
        }

        if (overlay != OverlayMode::NONE) {
            auto slotLabel = [](bool filled, const std::string& name, int idx) -> std::string {
                if (!filled) return "EMPTY";
                return name.empty() ? ("SLOT" + std::to_string(idx + 1)) : name;
            };

            if (overlay == OverlayMode::MAIN_MENU) {
                if (m_menuLayoutDirty || m_menuLayout.grid.size() != expectedGridSize) {
                    MainMenu::Build(m_menuLayout, cols, rows, m_hoveredButton, m_menuSeed,
                                     &m_titleBreakup, m_titleHovered, m_menuVariant);
                    m_menuLayoutDirty = false;
                }
                ascii.setUIOverlay(true, m_menuLayout.grid, cols, rows);
            } else if (overlay == OverlayMode::PAUSE_MENU) {
                if (m_pauseLayoutDirty || m_pauseLayout.grid.size() != expectedGridSize) {
                    m_pauseLayout = MainMenu::BuildPauseMenu(cols, rows, m_hoveredButton, m_menuSeed, m_pauseVariant);
                    m_pauseLayoutDirty = false;
                }
                ascii.setUIOverlay(true, m_pauseLayout.grid, cols, rows);
            } else if (overlay == OverlayMode::CONTINUE_MENU) {
                if (m_continueLayoutDirty || m_continueLayout.grid.size() != expectedGridSize) {
                    // Full load, not SlotExists(): the button shows the save name
                    const bool slotFilled[3] = {
                        SaveSystem::SlotExists(0),
                        SaveSystem::SlotExists(1),
                        SaveSystem::SlotExists(2)
                    };
                    const std::string slotLabels[3] = {
                        slotLabel(slotFilled[0], SaveSystem::LoadSlot(0).name, 0),
                        slotLabel(slotFilled[1], SaveSystem::LoadSlot(1).name, 1),
                        slotLabel(slotFilled[2], SaveSystem::LoadSlot(2).name, 2)
                    };
                    m_continueLayout = MainMenu::BuildContinueMenu(
                        cols, rows, m_hoveredButton, m_menuSeed, slotLabels, slotFilled, m_menuVariant);
                    m_continueLayoutDirty = false;
                }
                ascii.setUIOverlay(true, m_continueLayout.grid, cols, rows);
            } else if (overlay == OverlayMode::SAVE_MENU) {
                if (m_saveLayoutDirty || m_saveLayout.grid.size() != expectedGridSize) {
                    const bool slotFilled[3] = {
                        SaveSystem::SlotExists(0),
                        SaveSystem::SlotExists(1),
                        SaveSystem::SlotExists(2)
                    };
                    const std::string slotLabels[3] = {
                        slotLabel(slotFilled[0], SaveSystem::LoadSlot(0).name, 0),
                        slotLabel(slotFilled[1], SaveSystem::LoadSlot(1).name, 1),
                        slotLabel(slotFilled[2], SaveSystem::LoadSlot(2).name, 2)
                    };
                    m_saveLayout = MainMenu::BuildSaveMenu(
                        cols, rows, m_hoveredButton, m_menuSeed, slotLabels, slotFilled, m_pauseVariant);
                    m_saveLayoutDirty = false;
                }
                ascii.setUIOverlay(true, m_saveLayout.grid, cols, rows);
            } else if (overlay == OverlayMode::NEWGAME_MENU) {
                if (m_newGameLayoutDirty || m_newGameLayout.grid.size() != expectedGridSize) {
                    const bool slotFilled[3] = {
                        SaveSystem::SlotExists(0),
                        SaveSystem::SlotExists(1),
                        SaveSystem::SlotExists(2)
                    };
                    const std::string slotLabels[3] = {
                        slotLabel(slotFilled[0], SaveSystem::LoadSlot(0).name, 0),
                        slotLabel(slotFilled[1], SaveSystem::LoadSlot(1).name, 1),
                        slotLabel(slotFilled[2], SaveSystem::LoadSlot(2).name, 2)
                    };
                    const int hoveredDifficulty = m_hoveredButton >= 4 ? m_hoveredButton - 4 : -1;
                    m_newGameLayout = MainMenu::BuildNewGameMenu(
                        cols, rows, m_hoveredButton < 4 ? m_hoveredButton : -1, m_menuSeed, slotLabels,
                        slotFilled, m_menuVariant, (int)m_selectedDifficulty, hoveredDifficulty);
                    m_newGameLayoutDirty = false;
                }
                ascii.setUIOverlay(true, m_newGameLayout.grid, cols, rows);
            } else if (overlay == OverlayMode::SAVE_CONFIRM_MENU) {
                if (m_saveConfirmLayoutDirty || m_saveConfirmLayout.grid.size() != expectedGridSize) {
                    // A small multi-line message
                    static const std::vector<std::string> kOverwriteMessage = {
                        "ARE YOU SURE YOU",
                        "WANT TO OVERWRITE",
                        "THIS SAVE"
                    };
                    m_saveConfirmLayout = MainMenu::BuildConfirmMenu(
                        cols, rows, m_hoveredButton, m_menuSeed, kOverwriteMessage, m_pauseVariant);
                    m_saveConfirmLayoutDirty = false;
                }
                ascii.setUIOverlay(true, m_saveConfirmLayout.grid, cols, rows);
            } else if (overlay == OverlayMode::NEWGAME_CONFIRM_MENU) {
                if (m_newGameConfirmLayoutDirty || m_newGameConfirmLayout.grid.size() != expectedGridSize) {
                    static const std::vector<std::string> kOverwriteMessage = {
                        "ARE YOU SURE YOU",
                        "WANT TO OVERWRITE",
                        "THIS SAVE"
                    };
                    m_newGameConfirmLayout = MainMenu::BuildConfirmMenu(
                        cols, rows, m_hoveredButton, m_menuSeed, kOverwriteMessage, m_menuVariant);
                    m_newGameConfirmLayoutDirty = false;
                }
                ascii.setUIOverlay(true, m_newGameConfirmLayout.grid, cols, rows);
            } else if (overlay == OverlayMode::SAVE_NAME_MENU) {
                if (m_nameEntryLayoutDirty || m_nameEntryLayout.grid.size() != expectedGridSize) {
                    m_nameEntryLayout = MainMenu::BuildNameEntryMenu(
                        cols, rows, m_menuSeed, m_saveNameBuffer, SaveSystem::kNameMaxLen,
                        m_backHoveredNameEntry, m_confirmHoveredNameEntry,
                        m_nameEntryForNewGame ? m_menuVariant : m_pauseVariant);
                    m_nameEntryLayoutDirty = false;
                }
                ascii.setUIOverlay(true, m_nameEntryLayout.grid, cols, rows);
            } else if (overlay == OverlayMode::DIARY_READING) {
                // Rebuilt every frame into a reused buffer
                scene.buildReadingOverlayGrid(m_diaryReadingGridScratch, cols, rows);
                ascii.setUIOverlay(true, m_diaryReadingGridScratch, cols, rows);
            } else if (overlay == OverlayMode::DIARY_HINT) {
                // BigFont is far larger than regular text, so the key is drawn as an outline around
                // a normal E
                m_hintGridScratch = m_hudIconsGridScratch;

                const std::string text = "E READ DIARY";
                const int startCol = cols / 2 - (int)text.size() / 2;
                const int textRow = rows / 2;

                MainMenu::PutText(m_hintGridScratch, cols, rows, startCol, textRow, text);
                MainMenu::DrawBox(m_hintGridScratch, cols, rows,
                                   startCol - 1, textRow - 1, startCol + 1, textRow + 1,
                                   1, /*filled=*/false, /*seed=*/555);

                ascii.setUIOverlay(true, m_hintGridScratch, cols, rows);
            } else if (overlay == OverlayMode::TORCH_HINT) {
                m_hintGridScratch = m_hudIconsGridScratch;

                const std::string text = "E TAKE TORCH";
                const int startCol = cols / 2 - (int)text.size() / 2;
                const int textRow = rows / 2;

                MainMenu::PutText(m_hintGridScratch, cols, rows, startCol, textRow, text);
                MainMenu::DrawBox(m_hintGridScratch, cols, rows,
                                   startCol - 1, textRow - 1, startCol + 1, textRow + 1,
                                   1, /*filled=*/false, /*seed=*/555);

                ascii.setUIOverlay(true, m_hintGridScratch, cols, rows);
            } else if (overlay == OverlayMode::TORCH_EMPTY_MSG) {
                const std::string msg = "NO TORCHES LEFT";
                m_hintGridScratch = m_hudIconsGridScratch;
                MainMenu::PutText(m_hintGridScratch, cols, rows,
                                   cols / 2 - (int)msg.size() / 2, rows / 2 + 2, msg);
                ascii.setUIOverlay(true, m_hintGridScratch, cols, rows);
            } else if (overlay == OverlayMode::WIN_READY_MSG) {
                // On top of the HUD icons
                const std::string msg = "[E] PRESS TO WIN";
                m_hintGridScratch = m_hudIconsGridScratch;
                MainMenu::PutText(m_hintGridScratch, cols, rows,
                                   cols / 2 - (int)msg.size() / 2, rows / 2 + 2, msg);
                ascii.setUIOverlay(true, m_hintGridScratch, cols, rows);
            } else if (overlay == OverlayMode::WIN_ACTIVATE_HINT) {
                const std::string msg = "[E] ACTIVATE";
                m_hintGridScratch = m_hudIconsGridScratch;
                MainMenu::PutText(m_hintGridScratch, cols, rows,
                                   cols / 2 - (int)msg.size() / 2, rows / 2 + 2, msg);
                ascii.setUIOverlay(true, m_hintGridScratch, cols, rows);
            } else if (overlay == OverlayMode::WIN_BLOCKED_MSG) {
                const int have = scene.diariesReadCount();
                const int need = std::max(0, scene.diariesToWin() - have);
                const std::string msg = "NEED " + std::to_string(need) + " MORE DIARIES";
                m_hintGridScratch = m_hudIconsGridScratch;
                MainMenu::PutText(m_hintGridScratch, cols, rows,
                                   cols / 2 - (int)msg.size() / 2, rows / 2 + 2, msg);
                ascii.setUIOverlay(true, m_hintGridScratch, cols, rows);
            } else if (overlay == OverlayMode::STORY_SCREEN) {
                m_storyGrid.resize(expectedGridSize);
                m_storyText.draw(m_storyGrid, cols, rows);

                // Bottom-right hint; while Space is held it turns into '#' from left to right
                const std::string hint = "HOLD SPACE TO SKIP";
                const int hintCol = cols - (int)hint.size() - 2;
                const int hintRow = rows - 2;
                const float progress = std::min(m_storySkipHold / kStorySkipHoldSeconds, 1.0f);
                const int filled = (int)(progress * (float)hint.size());
                for (int i = 0; i < (int)hint.size(); ++i) {
                    MainMenu::PutGlyph(m_storyGrid, cols, rows, hintCol + i, hintRow,
                                       i < filled ? MainMenu::GLYPH_HASH : MainMenu::CharToGlyph(hint[(size_t)i]));
                }
                ascii.setUIOverlay(true, m_storyGrid, cols, rows);
            } else if (overlay == OverlayMode::CREDITS_SCREEN) {
                // Frame only; the text is a TTF layer drawn after ascii.end()
                m_hintGridScratch.assign(expectedGridSize, 0);
                const int margin = std::max(2, cols / 12);
                MainMenu::DrawBox(m_hintGridScratch, cols, rows,
                                   margin, margin, cols - 1 - margin, rows - 1 - margin,
                                   1, /*filled=*/false, /*seed=*/777);
                ascii.setUIOverlay(true, m_hintGridScratch, cols, rows);
            } else {
                // Sliders change continuously while dragged
                if (m_settingsLayoutDirty || m_sliderDragging || m_sharpnessSliderDragging ||
                    m_musicSliderDragging || m_masterSliderDragging ||
                    m_settingsLayout.grid.size() != expectedGridSize) {
                    // Same composition as the parent screen, so entering settings does not flash
                    const int settingsVariant =
                        (m_settingsReturnState == AppState::PAUSED) ? m_pauseVariant : m_menuVariant;
                    m_settingsLayout = MainMenu::BuildSettingsMenu(
                        cols, rows, m_menuSeed,
                        sensitivityToSlider01(scene.getMouseSensitivity()),
                        cellSizeToSlider01(ascii.getUserCellSize()),
                        ascii.getUserCellSize(),
                        m_musicVolume, m_masterVolume,
                        m_backHovered, m_sliderHovered, m_sharpnessSliderHovered,
                        m_musicSliderHovered, m_masterSliderHovered,
                        m_colorEnabled, m_colorCheckboxHovered,
                        m_lensEnabled, m_lensCheckboxHovered,
                        m_crtEnabled, m_crtCheckboxHovered,
                        m_shadersUnlocked, m_shadersEnabled, m_shadersCheckboxHovered,
                        settingsVariant);
                    m_settingsLayoutDirty = false;
                }
                ascii.setUIOverlay(true, m_settingsLayout.grid, cols, rows);
            }
        } else {
            if (showHudIcons) {
                ascii.setUIOverlay(true, m_hudIconsGridScratch, cols, rows);
            } else {
                ascii.setUIOverlay(false, m_menuLayout.grid, 0, 0);
            }
        }

        ascii.setFadeAlpha(m_fadeAlpha);
        ascii.setColorEnabled(m_colorEnabled);
        ascii.setLensEffectEnabled(m_lensEnabled);
        ascii.setTime((float)glfwGetTime());
        // The compass is drawn by its own shader after ascii.end()
        scene.setColorEnabled(m_colorEnabled);

        ascii.setDepthRange(scene.nearPlane(), scene.farPlane());
        // Persistence trails smear ASCII glyphs, so they are only used for the plain 3D view
        m_crt.setEnabled(m_crtEnabled);
        m_crt.setPersistence(!m_shadersEnabled);
        ascii.setWindowFramebuffer(m_crt.beginFrame(w, h));
        m_gpuTimer.begin(GpuTimer::Ascii);
        ascii.end(w, h);
        m_gpuTimer.end();
        m_gpuTimer.endFrame();

        if (gameplayActive) {
            scene.renderCompassOverlay(w, h);

            scene.renderDebugMap(w, h);

            // Diary prose is a TTF layer after ascii.end()
            //inside the frame from getReadingBoxBounds()

            if (scene.isReadingOverlayOpen() && m_textRenderer.isReady()) {
                const int cols = ascii.getMenuGridColsForWindow(w, h);
                const int rows = ascii.getMenuGridRowsForWindow(w, h);
                int boxX0, boxY0, boxX1, boxY1;
                scene.getReadingBoxBounds(cols, rows, boxX0, boxY0, boxX1, boxY1);

                const float cellPx = (float)AsciiEffect::menuCellSizeForWindow(w, h);
                const float boxLeftPx   = boxX0 * cellPx;
                const float boxRightPx  = boxX1 * cellPx;
                const float boxTopPx    = boxY0 * cellPx;
                const float boxBottomPx = boxY1 * cellPx;
                const float boxWidthPx  = boxRightPx - boxLeftPx;
                const float boxCenterX  = (boxLeftPx + boxRightPx) * 0.5f;

                const glm::vec3 textColor(0.86f, 0.80f, 0.62f);

                const float diaryReadScale = 0.85f;
                const float journalRowScale = 0.68f;

                m_textRenderer.beginFrame(w, h);

                if (const Diaries::PlacedDiary* d = scene.openDiary()) {
                    const float textAreaWidth = boxWidthPx - 60.0f; // left/right padding inside the frame
                    const std::vector<std::string> lines =
                        m_textRenderer.wrapText(d->text, textAreaWidth, diaryReadScale);

                    float lineY = boxTopPx + 80.0f;
                    const float lineH = m_textRenderer.lineHeight(diaryReadScale);
                    for (const std::string& line : lines) {
                        if (lineY > boxBottomPx - 40.0f) break; // don't spill past the frame's bottom edge
                        const float lineW = m_textRenderer.textWidth(line, diaryReadScale);
                        m_textRenderer.drawLine(line, boxCenterX - lineW * 0.5f, lineY, diaryReadScale, textColor);
                        lineY += lineH;
                    }
                } else if (scene.isJournalListMode()) {
                    // The list scrolls with the selection when the frame cannot hold every row
                    const int total = scene.diariesTotalCount();
                    const int selected = scene.journalSelectedIndex();
                    const float rowH = m_textRenderer.lineHeight(journalRowScale);
                    const float listTop = boxTopPx + 40.0f;
                    const float listBottom = boxBottomPx - 30.0f;
                    const int visible = std::max(1, (int)((listBottom - listTop) / rowH) + 1);
                    const int first = std::clamp(selected - visible / 2, 0, std::max(0, total - visible));
                    const int last = std::min(total, first + visible);

                    const glm::vec3 hintColor(0.55f, 0.50f, 0.40f);
                    if (first > 0)
                        m_textRenderer.drawLine("...", boxLeftPx + 50.0f, listTop - rowH * 0.9f, 0.5f, hintColor);
                    if (last < total)
                        m_textRenderer.drawLine("...", boxLeftPx + 50.0f, listBottom + rowH * 0.6f, 0.5f, hintColor);
                    const std::string hint = (total > visible) ? "UP/DOWN TO SCROLL - E TO READ - TAB TO CLOSE"
                                                               : "UP/DOWN TO SELECT - E TO READ - TAB TO CLOSE";
                    const float hintW = m_textRenderer.textWidth(hint, 0.5f);
                    m_textRenderer.drawLine(hint, boxCenterX - hintW * 0.5f, boxBottomPx + 10.0f, 0.5f, hintColor);

                    float rowY = listTop;
                    for (int i = first; i < last; ++i) {
                        const bool read = scene.journalEntryRead(i);
                        const std::string label = "ENTRY " + std::to_string(i + 1) + (read ? "" : " ---");

                        // Queued before the line so the text draws on top
                        if (i == scene.journalSelectedIndex()) {
                            m_textRenderer.drawRect(
                                boxLeftPx + 40.0f, rowY - rowH * 0.75f,
                                boxRightPx - 40.0f, rowY + rowH * 0.30f,
                                glm::vec3(0.35f, 0.15f, 0.10f), 0.55f);
                        }

                        const glm::vec3 rowColor = (i == scene.journalSelectedIndex())
                            ? glm::vec3(1.0f, 0.92f, 0.55f) : textColor;
                        m_textRenderer.drawLine(label, boxLeftPx + 50.0f, rowY, journalRowScale, rowColor);
                        rowY += rowH;
                    }
                }

                m_textRenderer.endFrame();
            }
        }

        // Credits: scrolling TTF text
        if (m_appState == AppState::CREDITS && m_textRenderer.isReady()) {
            const int cols = ascii.getMenuGridColsForWindow(w, h);
            const int rows = ascii.getMenuGridRowsForWindow(w, h);
            const float cellPx = (float)AsciiEffect::menuCellSizeForWindow(w, h);
            const int margin = std::max(2, cols / 12);
            const float boxLeftPx   = margin * cellPx;
            const float boxRightPx  = (cols - 1 - margin) * cellPx;
            const float boxTopPx    = margin * cellPx;
            const float boxBottomPx = (rows - 1 - margin) * cellPx;
            const float boxWidthPx  = boxRightPx - boxLeftPx;
            const float boxCenterX  = (boxLeftPx + boxRightPx) * 0.5f;

            const bool upDown   = glfwGetKey(window, GLFW_KEY_UP)   == GLFW_PRESS;
            const bool downDown = glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS;
            const float creditsScale = 0.72f;
            const float creditsLineH = m_textRenderer.lineHeight(creditsScale);
            if (upDown && !m_creditsUpKeyWasDown)     m_creditsScrollPx = std::max(0.0f, m_creditsScrollPx - creditsLineH * 3.0f);
            if (downDown && !m_creditsDownKeyWasDown) m_creditsScrollPx += creditsLineH * 3.0f;
            m_creditsUpKeyWasDown = upDown;
            m_creditsDownKeyWasDown = downDown;

            // wrapText() ignores embedded newlines, so paragraphs are wrapped separately
            static const std::vector<std::string> kCreditsParagraphs = {
                "THANK YOU FOR PLAYING THIS GAME!",
                "This is my first game where I focused more on technology and the technical side than on gameplay. I started this project out of boredom, and decided to just show the world this little game to say: \"LOOK, I CAN DO THIS\" but unfortunately that hasn't happened yet. I probably won't keep developing or improving this project. This \"pseudo-engine\" isn't really worth anything and anyone could make it.",
                "(you can check the source code on GitHub: dedvpolit).",
                "Thanks to Acerola for letting me study his ASCII-shader project so I could later build it into this game.",
                "And of course, thanks to my parents for putting up with me. I love you.",
                "Finally, thank YOU who downloaded this and is reading these credits right now (and an enormous thanks to those who, despite the boring mechanics, collected all 12 diaries and saw every \"glitch\" effect). I hope you caught the reference to the ASCII donut from those YouTube videos at the end.",
                "I also want to say that the main character of this game is still out there, wandering this endless maze. AND I hope your hardware didn't die while you were playing this.",
                "Good luck! I hope you'll see my future projects too.",
                "I promise there will be an actual interesting story and interesting gameplay next time.",
                "P.S: MUNDFISH, TAKE ME ON AS AN INTERN. I'LL SOON BE IN MY 3RD YEAR",
                "CREDITS",
                "ASCII shader: based on AcerolaFX ASCII by Acerola",
                "Enemy model: THE WRAPPED by Codyanka (CC0)",
                "Music: Horror Soundscape Ambience by cartoon_music (Pixabay)",
                "Sound effects: Mixkit, Universfield and yodguard (Pixabay), qubodup (OpenGameArt, CC0). Player footsteps: dedvpolit",
                "Wall texture: modified from Torment by strideh (strideh.itch.io/torment)",
                "itch.io arts/banner: gubaduber(@opezlol42)",
                "Font: VT323 by The VT323 Project Authors (SIL Open Font License)",
                "Libraries: GLFW, GLEW, cgltf, stb, minimp3",
            };

            const float textAreaWidth = boxWidthPx - 60.0f;
            if (!m_creditsLinesBuilt || m_creditsLinesCachedWidth != textAreaWidth) {
                m_creditsLinesCache.clear();
                for (size_t p = 0; p < kCreditsParagraphs.size(); ++p) {
                    const std::vector<std::string> wrapped =
                        m_textRenderer.wrapText(kCreditsParagraphs[p], textAreaWidth, creditsScale);
                    for (const std::string& l : wrapped) m_creditsLinesCache.push_back(l);
                    if (p + 1 < kCreditsParagraphs.size()) m_creditsLinesCache.push_back(std::string());
                }
                m_creditsLinesBuilt = true;
                m_creditsLinesCachedWidth = textAreaWidth;
            }
            const std::vector<std::string>& allLines = m_creditsLinesCache;

            // Stop scrolling at the end of the text with a small margin
            const float totalTextH = (float)allLines.size() * creditsLineH;
            const float visibleH = boxBottomPx - boxTopPx - 80.0f;
            const float maxScrollPx = std::max(0.0f, totalTextH - visibleH);
            m_creditsScrollPx = std::min(m_creditsScrollPx, maxScrollPx);

            const glm::vec3 textColor(0.86f, 0.80f, 0.62f);

            m_textRenderer.beginFrame(w, h);
            float lineY = boxTopPx + 60.0f - m_creditsScrollPx;
            for (const std::string& line : allLines) {
                if (lineY > boxTopPx + 40.0f && lineY < boxBottomPx - 20.0f && !line.empty()) {
                    const float lineW = m_textRenderer.textWidth(line, creditsScale);
                    m_textRenderer.drawLine(line, boxCenterX - lineW * 0.5f, lineY, creditsScale, textColor);
                }
                lineY += creditsLineH;
                if (lineY > boxBottomPx + creditsLineH) break; // below the frame: no need to keep counting
            }

            if (maxScrollPx > 0.0f) {
                const std::string hint = "UP/DOWN TO SCROLL - ESC TO EXIT";
                const float hintW = m_textRenderer.textWidth(hint, 0.5f);
                m_textRenderer.drawLine(hint, boxCenterX - hintW * 0.5f, boxBottomPx + 10.0f, 0.5f,
                                         glm::vec3(0.55f, 0.50f, 0.40f));
            } else {
                const std::string hint = "ESC TO EXIT";
                const float hintW = m_textRenderer.textWidth(hint, 0.5f);
                m_textRenderer.drawLine(hint, boxCenterX - hintW * 0.5f, boxBottomPx + 10.0f, 0.5f,
                                         glm::vec3(0.55f, 0.50f, 0.40f));
            }

            m_textRenderer.endFrame();
        }

        // Everything above was drawn into the CRT target when the effect is on
        m_gpuTimer.begin(GpuTimer::Crt);
        m_crt.present((float)glfwGetTime(), deltaTime);
        m_gpuTimer.end();
}
