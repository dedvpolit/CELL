#include "Application.h"
#include "ProcessMemory.h"
#include "save/SaveSystem.h"
#include "audio/AudioMixer.h"
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

void Application::init(GLFWwindow* window, DungeonScene& scene, AsciiEffect& ascii)
{
    (void)window;
    (void)scene;
    (void)ascii;

    m_currentSceneW = m_normalSceneW;
    m_currentSceneH = m_normalSceneH;

    // Non-fatal if the TTF renderer fails to init (e.g. the font file is missing): isReady() is
    // checked before every use, and diary text just does not show (the frame and blood drips still
    // render through the grid path).
    if (!m_textRenderer.create())
        std::fprintf(stderr, "Application::init: TextRenderer failed to initialize\n");

    m_menuVariant = MainMenu::PickAtmosphereVariant(m_menuOpenCount, m_menuSeed);
    m_pauseVariant = MainMenu::PickAtmosphereVariant(m_pauseOpenCount, m_menuSeed);
}

void Application::confirmNameEntry(DungeonScene& scene)
{
    // Default name if the player left it blank: SLOT1/SLOT2/SLOT3 by slot number, not an empty
    // string and not a blocked confirm.
    const int slot = m_nameEntryForNewGame ? m_pendingNewGameSlot : m_pendingSaveSlot;
    std::string effectiveName = m_saveNameBuffer;
    if (effectiveName.empty() && slot >= 0) {
        effectiveName = "SLOT" + std::to_string(slot + 1);
    }

    if (m_nameEntryForNewGame) {
        // Do not save now: the new game has not been generated yet. Stash the name in
        // m_saveNameBuffer (it survives FADE_TO_BLACK) and fade to black like a plain NEW GAME; the
        // map is regenerated once the screen is fully black.
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

        // Console FPS counter: average fps and frame time once a second. Console-only on purpose
        // (no on-screen overlay), so it can be dropped once real numbers are collected from the
        // target (weak) hardware.
        {
            static int s_fpsFrameCount = 0;
            static double s_fpsAccum = 0.0;
            s_fpsFrameCount++;
            s_fpsAccum += (double)deltaTime;
            if (s_fpsAccum >= 1.0)
            {
                // MB, matching Windows Task Manager; computed once a second with the fps, not every
                // frame.
                const size_t ramBytes = GetProcessWorkingSetBytes();
                const double ramMb = (double)ramBytes / (1024.0 * 1024.0);

                std::printf(
                    "[perf] fps = %.1f (avg frame time %.2f ms) | tris=%d chunks=%d torches=%d particles=%d lens=%d | ram=%.1f MB\n",
                    (double)s_fpsFrameCount / s_fpsAccum,
                    1000.0 * s_fpsAccum / (double)s_fpsFrameCount,
                    scene.getLastVisibleTriangles(),
                    scene.getLastVisibleChunks(),
                    scene.getLastActiveTorchCount(),
                    scene.getLastVisibleParticles(),
                    m_lensEnabled ? 1 : 0,
                    ramMb
                );
                s_fpsFrameCount = 0;
                s_fpsAccum = 0.0;
            }
        }

        // ESC pauses in gameplay and acts as BACK in the menu states. It never quits the app: EXIT
        // on the start screen (QUIT_APP) or ESC on the credits screen does.
        const bool escKeyDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (escKeyDown && !m_escKeyWasDown) {
            if (m_appState == AppState::PLAYING && scene.isReadingOverlayOpen()) {
                // A diary is open: Escape closes it instead of opening pause (the same key means
                // back/close everywhere else).
                scene.closeDiaryOrJournal();
            } else if (m_appState == AppState::PLAYING) {
                m_appState = AppState::PAUSED;
                m_hoveredButton = -1;
                m_titleHovered = false;
                m_pauseLayoutDirty = true;
                ++m_pauseOpenCount;
                // Every pause entry gets a fresh look: the previous variant is not repeated.
                m_pauseVariant = MainMenu::PickNextAtmosphereVariant(
                    m_pauseVariant, m_pauseOpenCount, m_menuSeed + 777);
            } else if (m_appState == AppState::PAUSED) {
                m_appState = AppState::PLAYING;
            } else if (m_appState == AppState::SETTINGS) {
                m_appState = m_settingsReturnState; // back to wherever it was opened from (MENU or PAUSED)
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
                // ESC acts as BACK: it discards the typed name and returns to where the screen was
                // opened from (SAVE from pause or NEW GAME from the main menu, see
                // m_nameEntryForNewGame).
                m_appState = m_nameEntryForNewGame ? AppState::NEWGAME_SELECT : AppState::SAVE_SELECT;
                m_hoveredButton = -1;
                m_saveNameBuffer.clear();
                if (m_nameEntryForNewGame) m_newGameLayoutDirty = true;
                else                       m_saveLayoutDirty = true;
            } else if (m_appState == AppState::CREDITS) {
                // The one exception to "ESC never quits the app": the credits screen already is the
                // end of the game (PendingAction::SHOW_CREDITS), so ESC is the only way out from
                // here.
                glfwSetWindowShouldClose(window, true);
            }
        }
        m_escKeyWasDown = escKeyDown;

        // Letters are plain text input in SAVE_NAME_ENTRY (F included), so the fullscreen hotkey is
        // disabled there.
        bool fKeyDown = m_appState != AppState::SAVE_NAME_ENTRY &&
                        glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        if (fKeyDown && !m_fKeyWasDown) {
            windowManager.toggleFullscreen();
        }
        m_fKeyWasDown = fKeyDown;

        // Performance/Stability mode (F1): locks to a stable ~30 fps instead of an unstable "up to
        // 60" that dips under sustained load on weak or thermally limited hardware (integrated-GPU
        // laptops, see README_PERF.txt).
        bool f1KeyDown = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
        if (f1KeyDown && !m_f1KeyWasDown) {
            windowManager.togglePerformanceMode();
            std::printf(
                "[perf] performance mode = %s\n",
                windowManager.isPerformanceMode() ? "ON (~30fps cap)" : "OFF (~60fps cap)"
            );
        }
        m_f1KeyWasDown = f1KeyDown;

        const bool gameplayActive =
            (m_appState == AppState::FADE_TO_GAME || m_appState == AppState::PLAYING);

        // Push the volume sliders to the mixer before update() refills the streaming chunk, so a
        // slider drag takes effect this frame.
        AudioMixer::instance().setMusicVolume(m_musicVolume);
        AudioMixer::instance().setMasterVolume(m_masterVolume);

        // Every frame, unconditionally: the duck ramp and the streamed chunks must keep advancing
        // even while gameplay logic is paused (e.g. the diary overlay).
        AudioMixer::instance().update(deltaTime);

        // Active only during real gameplay; it stops or idles itself otherwise.
        scene.tickAmbientMusic(deltaTime, gameplayActive);

        // No mouse look while the reading screen is open: movement/E/Tab are frozen there, and
        // unconscious mouse movement would otherwise turn the camera, leaving the diary out of view
        // when the screen closes.
        m_mouseLookEnabled = gameplayActive && !scene.isReadingOverlayOpen();

        // The cursor is visible and free until gameplay starts (menu/fade), then hidden and
        // captured for mouse look.
        if (gameplayActive != m_cursorCaptured) {
            if (gameplayActive) {
                // GLFW_CURSOR_DISABLED can move the system cursor: reset the mouse-look baseline
                // before glfwSetInputMode() so the first event after the switch is not read as a
                // camera turn.
                scene.resetMouseLook();
            }

            glfwSetInputMode(
                window,
                GLFW_CURSOR,
                gameplayActive ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
            );

            m_cursorCaptured = gameplayActive;
        }

        // SETTINGS has no background of its own: the screen it was opened from shows through
        // (m_settingsReturnState), so the pause check below is extended with settingsFromPause.
        // From the start screen it falls into the "menu camera stays put" branch.
        const bool settingsFromPause =
            m_appState == AppState::SETTINGS && m_settingsReturnState == AppState::PAUSED;

        if (gameplayActive && scene.isReadingOverlayOpen()) {
            // Diary screen open: movement and E are frozen (see
            // DungeonScene::isReadingOverlayOpen()); only reading input (E/Tab/arrows) and the idle
            // camera sway (as in pause) work.
            scene.tickReadingOverlayInput(window);
            scene.tickPauseCameraIdle(deltaTime);
        } else if (gameplayActive) {
            scene.processInput(window, deltaTime);

            // Check the credits request right after processInput(), in the frame it may have been
            // set.
            if (m_appState == AppState::PLAYING && scene.consumeCreditsRequest()) {
                m_pendingAction = PendingAction::SHOW_CREDITS;
                m_appState = AppState::FADE_TO_BLACK;
            }
        } else if (m_appState == AppState::PAUSED || settingsFromPause) {
            // Gameplay input is frozen in pause, but the camera keeps its idle motion, as if the
            // player just stood still.
            scene.tickPauseCameraIdle(deltaTime);
        }
        // Menu/fade, or SETTINGS from the start screen: the camera does not update and the frame
        // sits still (the menu background must not spin).

        // Stamina must finish draining in step with the fade after death
        // (PlayerController::tickDeathFade()). processInput() has already stopped by then
        // (gameplayActive is false), so the ticks do not overlap.
        if (!gameplayActive) {
            scene.tickDeathFade(deltaTime);
        }

        // Autosave periodically during real gameplay so CONTINUE reflects actual progress. It does
        // not accumulate in pause: no extra disk I/O while the game is frozen.
        if (m_appState == AppState::PLAYING) {
            m_autosaveTimer += deltaTime;
            if (m_autosaveTimer >= kAutosaveIntervalSeconds) {
                m_autosaveTimer = 0.0f;
                scene.saveActiveSlot();
            }
        }

        // Death: fade out once (consumeDeathFadeTrigger()) without scene.saveActiveSlot(), so the
        // save is not overwritten with "died at zero health". PAUSED is handled too: the death
        // sequence still plays out while paused.
        const bool canProcessDeathFade =
            m_appState == AppState::PLAYING || m_appState == AppState::PAUSED;
        if (canProcessDeathFade && scene.consumeDeathFadeTrigger()) {
            // DIED, not RETURN_TO_MENU (which assumes an open pause menu over the fade): a clean
            // fade with no menu that also generates a fresh map.
            m_pendingAction = PendingAction::DIED;
            ++m_menuOpenCount;
            m_menuVariant = MainMenu::PickAtmosphereVariant(m_menuOpenCount, m_menuSeed);
            m_menuLayoutDirty = true;
            m_appState = AppState::FADE_TO_BLACK;
        }

        // CELL title shatter animation: only on the start screen. Shard and tilt physics update
        // every frame while the title is visible, so speeds change over time instead of following a
        // linear interpolation.
        const bool mainMenuVisible =
            (m_appState == AppState::MENU) ||
            (m_appState == AppState::FADE_TO_MENU) ||
            (m_appState == AppState::FADE_TO_BLACK &&
             m_pendingAction != PendingAction::RETURN_TO_MENU &&
             m_pendingAction != PendingAction::DIED);

        if (mainMenuVisible) {
            MainMenu::UpdateTitleBreakup(m_titleBreakup, deltaTime);

            // Rebuild the overlay every frame while CELL is moving, otherwise the UI texture would
            // stay at the old position.
            if (MainMenu::HasActiveTitleAnimation(m_titleBreakup) ||
                m_titleBreakup.clickCount > 0) {
                m_menuLayoutDirty = true;
            }
        }

        // Menu hover/confirm (start screen and pause): m_hoveredButton is recomputed every frame
        // from the cursor position (no "last selection"); click/Enter confirms only the button
        // under the cursor. Shared by MENU and PAUSED: the same mechanic over different buttons.
        if (m_appState == AppState::MENU || m_appState == AppState::PAUSED ||
            m_appState == AppState::SETTINGS || m_appState == AppState::CONTINUE_SELECT ||
            m_appState == AppState::SAVE_SELECT || m_appState == AppState::SAVE_CONFIRM ||
            m_appState == AppState::NEWGAME_SELECT || m_appState == AppState::NEWGAME_CONFIRM ||
            m_appState == AppState::SAVE_NAME_ENTRY) {
            // Cursor pixels -> the menu grid the UI overlay uses (menuCellSizeForWindow());
            // independent of the SHARPNESS cell size, otherwise clicks would drift.
            double mx = 0.0, my = 0.0;
            glfwGetCursorPos(window, &mx, &my);

            int winW = 0, winH = 0;
            glfwGetFramebufferSize(window, &winW, &winH);

            const int cellSize = AsciiEffect::menuCellSizeForWindow(winW, winH);
            const int uiRows = ascii.getMenuGridRowsForWindow(winW, winH);

            int hoverCol = -1, hoverRow = -1;
            if (cellSize > 0 && uiRows > 0) {
                hoverCol = (int)std::floor(mx / (double)cellSize);
                // The shader counts cells bottom-up (cellIndex.y = 0 at the bottom edge) while the
                // MainMenu layout is top-down (row 0 at the title): the same flip as in the shader
                // (see AsciiEffect.cpp).
                const int cellIndexY = (int)std::floor((winH - my) / (double)cellSize);
                hoverRow = uiRows - 1 - cellIndexY;
            }

            auto insideRect = [&](const MainMenu::ButtonRect& r) {
                return hoverCol >= r.x0 && hoverCol <= r.x1 &&
                       hoverRow >= r.y0 && hoverRow <= r.y1;
            };

            // Confirm is LMB or Enter/Space. For MENU/PAUSED it only counts when the cursor is over
            // a button; SETTINGS handles it separately below (LMB can also start dragging a
            // slider).
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
                if (newBackHovered != m_backHovered || newSliderHovered != m_sliderHovered ||
                    newSharpnessSliderHovered != m_sharpnessSliderHovered ||
                    newMusicSliderHovered != m_musicSliderHovered ||
                    newMasterSliderHovered != m_masterSliderHovered ||
                    newColorCheckboxHovered != m_colorCheckboxHovered ||
                    newLensCheckboxHovered != m_lensCheckboxHovered) {
                    m_backHovered = newBackHovered;
                    m_sliderHovered = newSliderHovered;
                    m_sharpnessSliderHovered = newSharpnessSliderHovered;
                    m_musicSliderHovered = newMusicSliderHovered;
                    m_masterSliderHovered = newMasterSliderHovered;
                    m_colorCheckboxHovered = newColorCheckboxHovered;
                    m_lensCheckboxHovered = newLensCheckboxHovered;
                    m_settingsLayoutDirty = true;
                }

                const bool lmbDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

                // Dragging starts only by clicking the slider panel, but once started it holds
                // while LMB is down even if the cursor leaves the track vertically (standard slider
                // behavior).
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

                // SHARPNESS: like SENSITIVITY but calls ascii.setUserCellSize(). The atlas
                // regeneration is costly, but the value is rounded to an int so real calls are far
                // fewer than frames.
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

                // MUSIC slider: the same drag pattern; the value is already a plain 0..1 volume and
                // is stored here rather than in scene/ascii (see m_musicVolume in Application.h).
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

                // COLOR checkbox: a plain edge-triggered click/Enter/Space that flips a bool (not a
                // drag, so it does not touch m_sliderDragging).
                if (confirmDown && !m_confirmKeyWasDown && !m_sliderDragging && !m_sharpnessSliderDragging && m_colorCheckboxHovered) {
                    m_colorEnabled = !m_colorEnabled;
                    m_settingsLayoutDirty = true;
                }

                if (confirmDown && !m_confirmKeyWasDown && !m_sliderDragging && !m_sharpnessSliderDragging && m_lensCheckboxHovered) {
                    m_lensEnabled = !m_lensEnabled;
                    m_settingsLayoutDirty = true;
                    std::printf("[lens] toggled -> %s\n", m_lensEnabled ? "ON" : "OFF");
                }

                if (confirmDown && !m_confirmKeyWasDown && !m_sliderDragging && !m_sharpnessSliderDragging && m_backHovered) {
                    m_appState = m_settingsReturnState;
                    m_backHovered = false;
                    m_sliderHovered = false;
                    m_sharpnessSliderHovered = false;
                    m_musicSliderHovered = false;
                    m_masterSliderHovered = false;
                    m_colorCheckboxHovered = false;
                }
            } else if (m_appState == AppState::CONTINUE_SELECT) {
                // Slots highlight and click only if filled (m_continueLayout.slotFilled[i]): an
                // empty slot never gets newHoveredButton (it stays -1, or hover falls through to
                // BACK), so clicking it does nothing.
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
                        // The slot is guaranteed filled (see above): load it through the normal
                        // black fade, like NEW GAME/EXIT. The heavy map regeneration happens once
                        // the screen is fully black (see FADE_TO_BLACK).
                        m_pendingLoadSlot = m_hoveredButton;
                        m_pendingAction = PendingAction::LOAD_GAME;
                        m_appState = AppState::FADE_TO_BLACK;
                        m_hoveredButton = -1;
                    }
                }
            } else if (m_appState == AppState::SAVE_SELECT) {
                // Unlike CONTINUE_SELECT, all 3 slots are clickable here: an empty one goes
                // straight to name entry (AppState::SAVE_NAME_ENTRY), a filled one asks for
                // overwrite confirmation first (AppState::SAVE_CONFIRM).
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
                        // reset: it could stay true from an earlier NEW GAME flow
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
                        // YES: go to name entry (nothing is saved yet), prefilled with the name
                        // already in this slot, so the player can press ENTER to keep it or clear
                        // it and type a new one.
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
                // Same pattern as SAVE_SELECT (all 3 slots clickable, filled ones included), for
                // NEW GAME from the main menu.
                int newHoveredButton = -1;
                for (int i = 0; i < 3; ++i) {
                    if (insideRect(m_newGameLayout.slotButtons[i])) {
                        newHoveredButton = i;
                    }
                }
                if (newHoveredButton == -1 && insideRect(m_newGameLayout.backButton)) {
                    newHoveredButton = 3;
                }

                if (newHoveredButton != m_hoveredButton) {
                    m_hoveredButton = newHoveredButton;
                    m_newGameLayoutDirty = true;
                }

                if (confirmDown && !m_confirmKeyWasDown && m_hoveredButton != -1) {
                    if (m_hoveredButton == 3) {
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
                        // YES: go to name entry. Unlike SAVE_CONFIRM the buffer is not prefilled
                        // with the old name: this is a new game, not a continuation.
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
                // The mouse handles only BACK (cancel) and OK (confirm, the same logic as ENTER
                // below, see confirmNameEntry()); typing is handled by the keyboard block further
                // down.
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
                        // CELL is a clickable area, not a button: it shatters the logo. The hitbox
                        // is titleBoxRect (same as the hover box) but the particle origin is
                        // titleRect (the bare text); swapping them desyncs the scatter from the
                        // letters.
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
                            // NEW GAME and CONTINUE open a slot picker; SETTINGS is instant (no
                            // fade); EXIT is the only way to close the app.
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
                            // RESUME, SAVE (slot picker) and SETTINGS are instant; MENU fades and
                            // does not close the app.
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
                                // Pick the menu variant now so FADE_TO_MENU uses it from the first
                                // frame, and autosave first so CONTINUE reflects the progress at
                                // the moment of leaving.
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
            m_confirmKeyWasDown = confirmDown;
        }

        // Save-name text entry: separate from the menu handling above (that is mouse, this is
        // keyboard). Keys are polled (glfwGetKey) and compared with m_textEntryKeyWasDown for edge
        // triggering, otherwise a held key would type every frame.
        if (m_appState == AppState::SAVE_NAME_ENTRY) {
            auto keyPressed = [&](int glfwKey) {
                const bool down = glfwGetKey(window, glfwKey) == GLFW_PRESS;
                const bool wasDown = m_textEntryKeyWasDown[glfwKey];
                m_textEntryKeyWasDown[glfwKey] = down;
                return down && !wasDown;
            };

            bool bufferChanged = false;

            // Letters A-Z and digits 0-9: the full set BigFont supports (GetBigGlyph() in
            // BigFont.cpp).
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
            // Death gets its own, slower speed (kDeathFadeOutSpeed); the normal menu exit
            // (RETURN_TO_MENU etc.) is untouched.
            const float fadeOutSpeed =
                (m_pendingAction == PendingAction::DIED) ? kDeathFadeOutSpeed : kFadeOutSpeed;
            m_fadeAlpha += fadeOutSpeed * deltaTime;
            if (m_fadeAlpha >= 1.0f) {
                m_fadeAlpha = 1.0f;

                // The screen is fully black: decide what comes next from PendingAction. QUIT_APP is
                // the only place the app actually closes.
                if (m_pendingAction == PendingAction::QUIT_APP) {
                    glfwSetWindowShouldClose(window, true);
                } else if (m_pendingAction == PendingAction::RETURN_TO_MENU) {
                    m_appState = AppState::FADE_TO_MENU;
                } else if (m_pendingAction == PendingAction::SHOW_CREDITS) {
                    // Credits fade from alpha 1.0 like FADE_TO_MENU, so the text stays bright. To
                    // keep the dungeon from showing through, the camera moves outside the map
                    // geometry (teleportCameraForCredits()): nothing is rendered over a black clear
                    // color.
                    scene.teleportCameraForCredits();
                    m_appState = AppState::CREDITS;
                    m_creditsScrollPx = 0.0f;
                } else if (m_pendingAction == PendingAction::DIED) {
                    // Generate a fresh map instead of leaving the death map behind the menu;
                    // newGame() also resets the player state that would still read as "just died".
                    scene.newGame();
                    m_appState = AppState::FADE_TO_MENU;
                } else if (m_pendingAction == PendingAction::NEW_GAME) {
                    // The screen is fully black: map/GL regeneration is invisible. Not reachable
                    // through the current UI (NEW GAME always goes through NEW_GAME_IN_SLOT below)
                    // but kept as a safe fallback.
                    scene.newGame();
                    m_autosaveTimer = 0.0f;
                    m_appState = AppState::FADE_TO_GAME;
                } else if (m_pendingAction == PendingAction::NEW_GAME_IN_SLOT) {
                    // The slot and name were chosen before the fade (confirmNameEntry());
                    // m_saveNameBuffer already holds the default "SLOT{n}" if the player left it
                    // empty.
                    scene.newGame(m_pendingNewGameSlot, m_saveNameBuffer);
                    m_pendingNewGameSlot = -1;
                    m_saveNameBuffer.clear();
                    m_nameEntryForNewGame = false;
                    m_autosaveTimer = 0.0f;
                    m_appState = AppState::FADE_TO_GAME;
                } else if (m_pendingAction == PendingAction::LOAD_GAME) {
                    if (!scene.loadSlot(m_pendingLoadSlot)) {
                        // Should not happen (the UI does not let an empty slot be picked), but a
                        // fallback new game beats hanging on a black screen without a scene.
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
                // m_menuVariant was already picked when MENU was clicked in pause; picking it again
                // here would skip a variant.
            }
        } else if (m_appState == AppState::CREDITS) {
            // Same fade-out as FADE_TO_MENU, but on reaching 0 it stays in CREDITS instead of
            // transitioning.
            if (m_fadeAlpha > 0.0f) {
                m_fadeAlpha -= kFadeInSpeed * deltaTime;
                if (m_fadeAlpha < 0.0f) m_fadeAlpha = 0.0f;
            }
        }

        const bool noclipEnabled = gameplayActive && scene.isNoclipEnabled();
        const bool wantCinematicBoost =
            gameplayActive && noclipEnabled && scene.isCinematicResolutionEnabled();

        if (wantCinematicBoost != m_appliedCinematicBoost) {
            m_currentSceneW = wantCinematicBoost ? scene.getCinematicSceneWidth()  : m_normalSceneW;
            m_currentSceneH = wantCinematicBoost ? scene.getCinematicSceneHeight() : m_normalSceneH;
            ascii.resize(m_currentSceneW, m_currentSceneH);
            ascii.setCinematicMode(wantCinematicBoost, scene.getCinematicCellSize());
            m_appliedCinematicBoost = wantCinematicBoost;
            m_menuLayoutDirty = true;
        }

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);

        ascii.begin();
        scene.render(m_currentSceneW, m_currentSceneH, gameplayActive);

        // Stamina bar: gameplay only, not in menu/fade. The fill is stamina, the "dripping" frame
        // is health. It fades out in noclip (a debug mode for trailer shots, where the HUD gets in
        // the way).
        ascii.setStamina(scene.getStaminaFraction(), /*enabled=*/gameplayActive && !noclipEnabled);
        ascii.setHealth(scene.getHealthFraction());

        ascii.setWallGlitch(
            gameplayActive && !noclipEnabled && scene.glyphGlitchActive(),
            scene.glyphGlitchUV().x, scene.glyphGlitchUV().y,
            scene.glyphGlitchRadiusCells());

        // Pick the UI overlay for the current state. The FADE_TO_BLACK overlay depends on what
        // triggered it (start screen, or the slot list for PendingAction::LOAD_GAME); save screens
        // never fade because the write is instant. SETTINGS shows the same overlay wherever it was
        // opened from.
        enum class OverlayMode { NONE, MAIN_MENU, CONTINUE_MENU, SAVE_MENU, SAVE_CONFIRM_MENU, SAVE_NAME_MENU, NEWGAME_MENU, NEWGAME_CONFIRM_MENU, PAUSE_MENU, SETTINGS_MENU, DIARY_READING, DIARY_HINT, WIN_BLOCKED_MSG, WIN_READY_MSG, WIN_ACTIVATE_HINT, TORCH_HINT, TORCH_EMPTY_MSG, CREDITS_SCREEN };
        OverlayMode overlay = OverlayMode::NONE;
        if (m_appState == AppState::MENU || m_appState == AppState::FADE_TO_MENU) {
            overlay = OverlayMode::MAIN_MENU;
        } else if (m_appState == AppState::CREDITS) {
            overlay = OverlayMode::CREDITS_SCREEN;
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
                // Death can happen mid-gameplay with no pause open: the screen just darkens on its
                // own.
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

        // The diary screen is not its own AppState (an overlay on top of PLAYING), so it is checked
        // separately from the chain above.
        if (m_appState == AppState::PLAYING && scene.isReadingOverlayOpen()) {
            overlay = OverlayMode::DIARY_READING;
        } else if (m_appState == AppState::PLAYING && scene.winReadyToPressE()) {
            // "[E] PRESS TO WIN" has the highest priority among the hints below (no real conflict:
            // the player cannot be at two points of interest at once).
            overlay = OverlayMode::WIN_READY_MSG;
        } else if (m_appState == AppState::PLAYING && scene.nearWinMonumentReadyToActivate()) {
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

        // The grid is built from the actual window size (as passed to ascii.end()), not the
        // internal 1280x720 FBO, or menu text would drift on fullscreen/resize. cols/rows are also
        // needed outside the overlay branch: the HUD icons above the stamina bar are visible
        // throughout gameplay.
        const int cols = ascii.getMenuGridColsForWindow(w, h);
        const int rows = ascii.getMenuGridRowsForWindow(w, h);
        const size_t expectedGridSize = (size_t)std::max(0, cols) * std::max(0, rows);

        // Torch/stone/diary count icons: shown exactly when the stamina bar is, hidden while
        // reading a diary. Rebuilt every frame (small grid). hudIconsGrid is the base layer for the
        // hint branches below, so icons stay visible with a hint.
        const bool showHudIcons =
            gameplayActive && !scene.isNoclipEnabled() && !scene.isReadingOverlayOpen();

        // Reusable m_hudIconsGridScratch. The icon position depends on the live cellSize, not on
        // the grid size, so a size-preserving cellSize change would leave the old icon stamped:
        // hence a full std::fill every frame.
        if (m_hudIconsGridScratch.size() != expectedGridSize) {
            m_hudIconsGridScratch.assign(expectedGridSize, 0);
        }
        if (showHudIcons) {
            std::fill(m_hudIconsGridScratch.begin(), m_hudIconsGridScratch.end(), 0);
            // The stamina bar lives in the shader's live grid, the icons in the fixed menu grid:
            // two coordinate systems. They are converted through window pixels (bar top edge from
            // the live cellSize -> menu row).
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
                    // Full load of each slot (not just SlotExists()): the button label needs the
                    // actual name. The files are tiny, so parsing three of them once per screen
                    // entry costs nothing.
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
                    m_newGameLayout = MainMenu::BuildNewGameMenu(
                        cols, rows, m_hoveredButton, m_menuSeed, slotLabels, slotFilled, m_menuVariant);
                    m_newGameLayoutDirty = false;
                }
                ascii.setUIOverlay(true, m_newGameLayout.grid, cols, rows);
            } else if (overlay == OverlayMode::SAVE_CONFIRM_MENU) {
                if (m_saveConfirmLayoutDirty || m_saveConfirmLayout.grid.size() != expectedGridSize) {
                    // Compact multi-line message in a small font instead of a giant "OVERWRITE"
                    // title: the full warning would not fit that way, width-wise or stylistically.
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
                // Reusable m_diaryReadingGridScratch: rebuilt every frame without allocating
                // (E/Tab/arrows change it too often to track invalidation).
                scene.buildReadingOverlayGrid(m_diaryReadingGridScratch, cols, rows);
                ascii.setUIOverlay(true, m_diaryReadingGridScratch, cols, rows);
            } else if (overlay == OverlayMode::DIARY_HINT) {
                // BigFont cannot go below a 5x7 cell block, a jump of about 7x over regular text.
                // Instead of sizing it up, draw a "key" outline around a normal-size E, so it reads
                // as a keyboard button and not a giant letter.
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
                // Start from m_hudIconsGridScratch so the icons do not disappear while the hint is
                // shown: it can stay up for a while as the player admires the donut without
                // pressing E.
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
                const int need = std::max(0, PlayerController::kMinDiariesToWin - have);
                const std::string msg = "NEED " + std::to_string(need) + " MORE DIARIES";
                m_hintGridScratch = m_hudIconsGridScratch;
                MainMenu::PutText(m_hintGridScratch, cols, rows,
                                   cols / 2 - (int)msg.size() / 2, rows / 2 + 2, msg);
                ascii.setUIOverlay(true, m_hintGridScratch, cols, rows);
            } else if (overlay == OverlayMode::CREDITS_SCREEN) {
                // Just the frame (grid UI font); the text is a separate TTF layer drawn after
                // ascii.end() (see the block below, gated on m_appState == CREDITS). Reuses
                // m_hintGridScratch because the credits screen can stay open for minutes.
                m_hintGridScratch.assign(expectedGridSize, 0);
                const int margin = std::max(2, cols / 12);
                MainMenu::DrawBox(m_hintGridScratch, cols, rows,
                                   margin, margin, cols - 1 - margin, rows - 1 - margin,
                                   1, /*filled=*/false, /*seed=*/777);
                ascii.setUIOverlay(true, m_hintGridScratch, cols, rows);
            } else {
                // Rebuild every frame in which the value could have changed: dragging a slider
                // changes it continuously, not only at hover/click boundaries like regular buttons.
                if (m_settingsLayoutDirty || m_sliderDragging || m_sharpnessSliderDragging ||
                    m_musicSliderDragging || m_masterSliderDragging ||
                    m_settingsLayout.grid.size() != expectedGridSize) {
                    // The settings atmosphere uses the same composition as its "parent" screen
                    // (menu or pause), so it does not flash a different pattern on entry/exit.
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
        // The compass/minimap are drawn by a separate shader after ascii.end(), so the color mode
        // is passed through here too.
        scene.setColorEnabled(m_colorEnabled);

        ascii.end(w, h);

        if (gameplayActive) {
            scene.renderCompassOverlay(w, h);

            scene.renderDebugMap(w, h);

            // Diary prose is a separate TTF layer drawn after ascii.end() (the ASCII post-process
            // would break the letters into glyphs); only the text is added here, inside the frame
            // from getReadingBoxBounds().
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
                    // No "LOG" heading: the list starts almost at the frame's top edge with a
                    // compact step (journalRowScale), so all entries fit without clipping at the
                    // bottom.
                    const int total = scene.diariesTotalCount();
                    const float rowH = m_textRenderer.lineHeight(journalRowScale);
                    float rowY = boxTopPx + 40.0f;
                    for (int i = 0; i < total; ++i) {
                        if (rowY > boxBottomPx - 30.0f) break;
                        const bool read = scene.diaryReadAt(i);
                        const std::string label = "ENTRY " + std::to_string(i + 1) + (read ? "" : " ---");

                        // Highlight: the same layer and coordinates as the line's text, added to
                        // the batch before that line's drawLine() so the text ends up on top.
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

        // Credits (AppState::CREDITS): a standalone block, not gameplay. The same TTF layer as the
        // diary prose, scrolling because the text does not fit the frame.
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

            // Text as paragraphs: wrapText() wraps only on spaces, not on embedded "\n", so
            // paragraphs are assembled as separate calls with a blank line between them.
            static const std::vector<std::string> kCreditsParagraphs = {
                "Thank you for playing this game. This is my first game where I focused more on technology and the technical side than on gameplay. I started this project out of boredom, and decided to just show the world this little game to say: \"LOOK, I CAN DO THIS, MUNDFISH, HIRE ME\" but unfortunately that hasn't happened yet. I probably won't keep developing or improving this project. This \"pseudo-engine\" isn't really worth anything and anyone could make it (you can check the source code on GitHub: dedvpolit).",
                "I want to say thank you to Mishiki for writing the music for me.",
                "Thanks to Acerola for letting me study his ASCII-shader project so I could later build it into this game.",
                "And of course, thank you to my parents for putting up with me. I love you.",
                "Finally, thank you to everyone who downloaded this and is reading these credits right now (and an enormous thank you to those who, despite the boring mechanics, collected all 12 diaries and saw every \"glitch\" effect). I hope you caught the reference to the ASCII donut from those YouTube videos at the end.",
                "I also want to say that the main character of this game is still out there, wandering this endless maze. I hope your hardware didn't die while you were playing this.",
                "Good luck, dear player! I hope you'll see my future projects too. I promise there will be an actual interesting story and interesting gameplay next time.",
                "P.S. MUNDFISH, TAKE ME ON AS AN INTERN. I'LL SOON BE IN MY 3RD YEAR.",
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

            // Do not scroll past the end of the text (with a small margin so the last line does not
            // stick to the frame).
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
                if (lineY > boxBottomPx + creditsLineH) break; // below the frame — no need to keep counting
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
}
