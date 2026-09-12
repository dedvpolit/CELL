#include "Application.h"
#include "ProcessMemory.h"
#include "save/SaveSystem.h"
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
    // Округляем к ближайшему целому — размер ячейки всегда целое число
    // пикселей (см. AsciiEffect::setUserCellSize()), плавных дробных
    // промежуточных значений тут не бывает в принципе.
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

    // Дневники/журнал — см. ui/TextRenderer.h. Несмертельно, если не
    // удалось (например, файл шрифта не нашёлся) — isReady() ниже
    // проверяется перед каждым использованием, при провале инициализации
    // просто не будет текста дневников (рамка/капли крови всё равно
    // отрисуются старым сеточным путём, см. DungeonScene::
    // buildReadingOverlayGrid()), не крах всего приложения.
    if (!m_textRenderer.create())
        std::fprintf(stderr, "Application::init: TextRenderer failed to initialize\n");

    m_menuVariant = MainMenu::PickAtmosphereVariant(m_menuOpenCount, m_menuSeed);
    m_pauseVariant = MainMenu::PickAtmosphereVariant(m_pauseOpenCount, m_menuSeed);
}

void Application::tick(GLFWwindow* window, DungeonScene& scene, AsciiEffect& ascii,
                       WindowManager& windowManager, float deltaTime)
{
        deltaTime = std::min(deltaTime, 0.05f); // защита от скачка dt после паузы/лагов

        // ---- Console FPS counter (perf diagnostics) ----
        // Prints average FPS + average frame time once a second. This is
        // deliberately simple (console only, no on-screen overlay) so it
        // can be dropped once real hardware numbers are collected — the
        // dev machine this was built/tested on doesn't represent the
        // weak hardware this engine targets, so real numbers have to
        // come from testing on the actual target machine.
        {
            static int s_fpsFrameCount = 0;
            static double s_fpsAccum = 0.0;
            s_fpsFrameCount++;
            s_fpsAccum += (double)deltaTime;
            if (s_fpsAccum >= 1.0)
            {
                // ОЗУ — см. ProcessMemory.h — считаем в МБ (в тех же
                // единицах, что и Диспетчер задач Windows), рядом с fps
                // в той же строке, раз в секунду (не каждый кадр — сам
                // запрос дешёвый, но незачем считать чаще, чем реально
                // читается лог).
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

        // ---- ESC: пауза во время игры, НИКОГДА выход из приложения ----
        // Edge-triggered (реагируем только на нажатие, не на удержание).
        // Работает только на границе PLAYING<->PAUSED; во всех остальных
        // состояниях (стартовое меню, затемнения) ESC сейчас ничего не
        // делает — единственный способ закрыть игру целиком остался
        // прежним: кнопка EXIT на стартовом экране (см. обработку
        // AppState::MENU ниже, PendingAction::QUIT_APP).
        const bool escKeyDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (escKeyDown && !m_escKeyWasDown) {
            if (m_appState == AppState::PLAYING && scene.isReadingOverlayOpen()) {
                // Дневник/журнал открыт — Escape закрывает ЕГО, а не
                // открывает паузу (см. DungeonScene::closeDiaryOrJournal()).
                // Та же клавиша, что и везде в игре для "назад"/"закрыть",
                // не нужно учить новому смыслу только для этого экрана.
                scene.closeDiaryOrJournal();
            } else if (m_appState == AppState::PLAYING) {
                m_appState = AppState::PAUSED;
                m_hoveredButton = -1;
                m_titleHovered = false;
                m_pauseLayoutDirty = true;
                ++m_pauseOpenCount;
                // Каждый новый вход в паузу обязан получить новый дизайн:
                // псевдослучайный выбор сохраняется, но повтор предыдущего
                // варианта запрещён, чтобы пауза визуально действительно менялась.
                m_pauseVariant = MainMenu::PickNextAtmosphereVariant(
                    m_pauseVariant, m_pauseOpenCount, m_menuSeed + 777);
            } else if (m_appState == AppState::PAUSED) {
                m_appState = AppState::PLAYING; // мгновенно, без затемнения — как и кнопка RESUME
            } else if (m_appState == AppState::SETTINGS) {
                // ESC работает как BACK — возвращает туда, откуда открыли
                // настройки (MENU или PAUSED), тоже мгновенно, без затемнения.
                m_appState = m_settingsReturnState;
                m_backHovered = false;
                m_sliderHovered = false;
                m_sliderDragging = false;
                m_colorCheckboxHovered = false;
            } else if (m_appState == AppState::CONTINUE_SELECT) {
                // ESC работает как BACK — возвращает в главное меню,
                // мгновенно, без затемнения (тот же стиль, что и SETTINGS).
                m_appState = AppState::MENU;
                m_hoveredButton = -1;
            } else if (m_appState == AppState::SAVE_SELECT) {
                // ESC работает как BACK — возвращает в меню паузы (SAVE
                // открывается только из паузы, см. кнопку SAVE ниже).
                m_appState = AppState::PAUSED;
                m_hoveredButton = -1;
            } else if (m_appState == AppState::SAVE_CONFIRM) {
                // ESC работает как NO — отменяет перезапись и возвращает
                // к выбору слота (не сразу в паузу, чтобы можно было
                // выбрать другой слот, см. кнопку NO ниже).
                m_appState = AppState::SAVE_SELECT;
                m_hoveredButton = -1;
                m_saveLayoutDirty = true;
            } else if (m_appState == AppState::SAVE_NAME_ENTRY) {
                // ESC работает как BACK — отменяет ввод имени и
                // возвращает к выбору слота (набранный текст отбрасывается).
                m_appState = AppState::SAVE_SELECT;
                m_hoveredButton = -1;
                m_saveNameBuffer.clear();
                m_saveLayoutDirty = true;
            }
        }
        m_escKeyWasDown = escKeyDown;

        bool fKeyDown = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        if (fKeyDown && !m_fKeyWasDown) {
            windowManager.toggleFullscreen();
        }
        m_fKeyWasDown = fKeyDown;

        // Performance/Stability mode (F1) — locks to a solid ~30fps
        // (half the monitor's refresh, see WindowManager::
        // setPerformanceMode()) instead of an unstable "up to 60fps"
        // that can dip lower under sustained load on weak/thermally-
        // limited hardware (integrated GPU laptops especially — see
        // README_PERF.txt). Available at any point (menu or gameplay),
        // takes effect immediately.
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

        // БАГФИКС ("дневник пропадает с пола после E->E") — раньше здесь
        // было просто `= gameplayActive`, и мышь продолжала вращать
        // камеру (см. main.cpp::mouseCallback -> processMouse()) всё
        // время, пока открыт экран чтения: движение/E/Tab заморожены
        // (см. tickReadingOverlayInput() ниже), а ПОВОРОТ взгляда — нет.
        // Игрок непроизвольно двигал мышь, читая текст, и к моменту
        // закрытия экрана камера смотрела уже в другую сторону — дневник
        // не пропадал, просто оказывался вне поля зрения.
        m_mouseLookEnabled = gameplayActive && !scene.isReadingOverlayOpen();

        // Курсор виден и свободен, пока идёт геймплей ещё не начался
        // (меню/затемнение перед стартом); как только начинается реальный
        // геймплей — прячем и захватываем его под FPS-обзор мышью.
        if (gameplayActive != m_cursorCaptured) {
            if (gameplayActive) {
                // GLFW_CURSOR_DISABLED может переместить/синхронизировать
                // системный курсор. Первое событие после этого НЕ должно
                // считаться поворотом камеры: оно только устанавливает
                // новую базовую точку mouse-look. Важно сделать это ДО
                // glfwSetInputMode(), чтобы возможный callback от самого
                // переключения режима тоже был безопасным.
                scene.resetMouseLook();
            }

            glfwSetInputMode(
                window,
                GLFW_CURSOR,
                gameplayActive ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
            );

            m_cursorCaptured = gameplayActive;
        }

        // SETTINGS не имеет собственного фона — под ним виден фон того
        // экрана, откуда его открыли (см. m_settingsReturnState), поэтому
        // ниже логика, которая раньше проверяла "мы в паузе", расширена
        // флагом settingsFromPause. Отдельного settingsFromMenu не нужно:
        // SETTINGS, открытые со стартового экрана, просто не попадают ни
        // в freezeScene, ни в ветку PAUSED ниже и естественным образом
        // проваливаются в "иначе — крутим камеру меню".
        const bool settingsFromPause =
            m_appState == AppState::SETTINGS && m_settingsReturnState == AppState::PAUSED;

        // Замороженный кадр: пауза сама (PAUSED, и SETTINGS, открытые ИЗ
        // паузы), и затемнение экрана, которое из паузы ведёт обратно в
        // меню (FADE_TO_BLACK с PendingAction::RETURN_TO_MENU) — во всех
        // этих случаях под меню должен быть видно ИМЕННО остановленный
        // кадр игры, а не авто-вращающаяся камера стартового меню и не
        // продолжающееся движение игрока. DIED (смерть) — та же логика:
        // без этого сюда бы попал tickMenuCameraSpin() и перебил бы
        // камеру, застывшую в позе смерти (см. PlayerController::
        // tickDeathFade() — она и так уже сама двигает камеру все то
        // время, что играет фейд, отдельно от этого блока).
        const bool freezeScene =
            (m_appState == AppState::PAUSED) || settingsFromPause ||
            (m_appState == AppState::FADE_TO_BLACK &&
             (m_pendingAction == PendingAction::RETURN_TO_MENU ||
              m_pendingAction == PendingAction::DIED));

        if (gameplayActive && scene.isReadingOverlayOpen()) {
            // Экран дневника/журнала открыт: обычное движение/E-взаимодействие
            // заморожено (тот же эффект, что и freezeScene у паузы, но не
            // через полноценный AppState — см. большой комментарий у
            // DungeonScene::isReadingOverlayOpen()), только чтение E/Tab/
            // стрелок для самого экрана + idle-покачивание камеры, как в паузе.
            scene.tickReadingOverlayInput(window);
            scene.tickPauseCameraIdle(deltaTime);
        } else if (gameplayActive) {
            scene.processInput(window, deltaTime);
        } else if (m_appState == AppState::PAUSED || settingsFromPause) {
            // В паузе (и в SETTINGS, открытых из неё) игровой ввод
            // полностью заморожен, но idle-движение камеры продолжается,
            // как когда игрок просто стоит в игре.
            scene.tickPauseCameraIdle(deltaTime);
        } else if (!freezeScene) {
            // Меню/затемнение перед стартом или выходом, и SETTINGS,
            // открытые со стартового экрана: игрок ещё не управляет
            // камерой — только атмосферное авто-вращение вокруг стартовой
            // safe-zone (settingsFromMenu тоже попадает сюда, т.к. не
            // входит ни в один из freezeScene/PAUSED случаев выше).
            scene.tickMenuCameraSpin(deltaTime);
        }
        // else: freezeScene — намеренно ничего не обновляем, кадр стоит на месте.

        // Стамина должна допадать до нуля ОДНОВРЕМЕННО с затемнением
        // экрана после смерти (см. PlayerController::tickDeathFade()) —
        // а обычный scene.processInput() выше уже перестал вызываться
        // (gameplayActive стал false, как только начался переход в
        // меню). Не пересекается по времени с processInput() — тот сам
        // прекращает вызываться в тот же момент, когда становится нужен
        // этот тик.
        if (!gameplayActive) {
            scene.tickDeathFade(deltaTime);
        }

        // ---- Автосохранение прогресса текущей игры (см. DungeonScene::
        // saveActiveSlot()) — периодически во время реального геймплея,
        // чтобы "Продолжить" отражало актуальный прогресс, а не только
        // момент старта/загрузки (см. saveActiveSlot() в newGame()/
        // loadSlot() выше) и явный автосейв при выходе в меню из паузы
        // (см. RETURN_TO_MENU выше). Не копится во время паузы —
        // freezeScene и так замораживает игру, лишний диск-I/O не нужен.
        if (m_appState == AppState::PLAYING) {
            m_autosaveTimer += deltaTime;
            if (m_autosaveTimer >= kAutosaveIntervalSeconds) {
                m_autosaveTimer = 0.0f;
                scene.saveActiveSlot();
            }
        }

        // ---- Смерть игрока (здоровье дошло до нуля) ----
        // См. PlayerController::applyDamage()/updateDeathSequence() —
        // сама последовательность (камера падает+крутится, стамина
        // падает) уже целиком отыграна к этому моменту;
        // consumeDeathFadeTrigger() взводится РОВНО один раз, когда пора
        // начинать переход в меню. Тот же фейд-пайплайн, что и у обычного
        // выхода в меню (RETURN_TO_MENU) — просто БЕЗ scene.saveActiveSlot()
        // здесь: смерть не должна перезаписывать последний сейв игрока
        // состоянием "умер с нулём здоровья".
        // Раньше проверялось только AppState::PLAYING — если игрок
        // успевал поставить игру на паузу ПРЯМО во время самой
        // последовательности смерти (до того как враг вообще был
        // заморожен, см. большой комментарий у render() в
        // DungeonScene.h), tickDeathFade() всё равно доигрывал
        // последовательность до конца (он не зависит от AppState вообще),
        // но взведённый триггер тут просто никогда не считывался —
        // игра зависала в паузе навсегда. PAUSED теперь тоже учитывается.
        const bool canProcessDeathFade =
            m_appState == AppState::PLAYING || m_appState == AppState::PAUSED;
        if (canProcessDeathFade && scene.consumeDeathFadeTrigger()) {
            // БАГФИКС: раньше здесь стоял RETURN_TO_MENU — тот же
            // PendingAction, что и у "выйти в меню из паузы", который
            // жёстко считает, что поверх фейда уже открыто меню паузы
            // (см. OverlayMode::PAUSE_MENU) — так это меню паузы
            // выскакивало НИОТКУДА посреди самой смерти, хотя игрок его
            // не открывал. DIED — отдельный, "чистый" фейд без
            // всплывающего меню (см. большой комментарий у DIED в
            // Application.h), плюс генерирует новую карту вместо того,
            // чтобы держать в фоне меню ту самую, где игрок погиб (см.
            // обработку DIED в блоке FADE_TO_BLACK ниже).
            m_pendingAction = PendingAction::DIED;
            ++m_menuOpenCount;
            m_menuVariant = MainMenu::PickAtmosphereVariant(m_menuOpenCount, m_menuSeed);
            m_menuLayoutDirty = true;
            m_appState = AppState::FADE_TO_BLACK;
        }

        // ---- Анимация разрушения заголовка CELL ----
        // Заголовок живёт только на стартовом экране. Пока он виден,
        // обновляем физику осколков и наклона каждый кадр, поэтому
        // скорость движения действительно меняется со временем, а не
        // задаётся линейной интерполяцией.
        const bool mainMenuVisible =
            (m_appState == AppState::MENU) ||
            (m_appState == AppState::FADE_TO_MENU) ||
            (m_appState == AppState::FADE_TO_BLACK &&
             m_pendingAction != PendingAction::RETURN_TO_MENU &&
             m_pendingAction != PendingAction::DIED);

        if (mainMenuVisible) {
            MainMenu::UpdateTitleBreakup(m_titleBreakup, deltaTime);

            // Оверлей нужно пересобирать каждый кадр, пока есть движение
            // CELL, иначе текстура UI останется на старом положении.
            if (MainMenu::HasActiveTitleAnimation(m_titleBreakup) ||
                m_titleBreakup.clickCount > 0) {
                m_menuLayoutDirty = true;
            }
        }

        // ---- Наведение и подтверждение в меню (стартовый экран ИЛИ пауза) ----
        // ВАЖНО: подсветка и клик работают ИСКЛЮЧИТЕЛЬНО через наведение
        // курсора мыши — m_hoveredButton пересчитывается заново каждый
        // кадр из текущей позиции курсора (не хранит "последний выбор"),
        // поэтому без наведения ни одна кнопка не подсвечена, а клик/
        // Enter подтверждают только ту кнопку, над которой курсор
        // находится ПРЯМО СЕЙЧАС. Общая для MENU и PAUSED, потому что
        // это одна и та же механика над разными кнопками/действиями.
        if (m_appState == AppState::MENU || m_appState == AppState::PAUSED ||
            m_appState == AppState::SETTINGS || m_appState == AppState::CONTINUE_SELECT ||
            m_appState == AppState::SAVE_SELECT || m_appState == AppState::SAVE_CONFIRM ||
            m_appState == AppState::SAVE_NAME_ENTRY) {
            // Переводим позицию курсора (реальные пиксели окна, как у
            // glfwGetCursorPos: (0,0) вверху слева) в ТУ ЖЕ систему
            // символьных клеток, что использует шейдер для UI-оверлея —
            // см. большой комментарий у AsciiEffect::kMenuReferenceCellSize:
            // меню всегда в СВОЕЙ фиксированной сетке, независимой от
            // живого cellSize (которым теперь управляет SHARPNESS) —
            // иначе клики "уезжали" бы вместе с раскладкой при её смене.
            double mx = 0.0, my = 0.0;
            glfwGetCursorPos(window, &mx, &my);

            int winW = 0, winH = 0;
            glfwGetFramebufferSize(window, &winW, &winH);

            const int cellSize = AsciiEffect::kMenuReferenceCellSize;
            const int uiRows = ascii.getMenuGridRowsForWindow(winH);

            int hoverCol = -1, hoverRow = -1;
            if (cellSize > 0 && uiRows > 0) {
                hoverCol = (int)std::floor(mx / (double)cellSize);
                // Шейдер считает клетки экрана снизу вверх (cellIndex.y=0
                // у нижнего края), а наша раскладка в MainMenu.h — сверху
                // вниз (row=0 у заголовка) — тот же переворот, что и в
                // самом шейдере (см. AsciiEffect.cpp: "uiTexel.y = 1.0 - ...").
                const int cellIndexY = (int)std::floor((winH - my) / (double)cellSize);
                hoverRow = uiRows - 1 - cellIndexY;
            }

            auto insideRect = [&](const MainMenu::ButtonRect& r) {
                return hoverCol >= r.x0 && hoverCol <= r.x1 &&
                       hoverRow >= r.y0 && hoverRow <= r.y1;
            };

            // Подтверждение — ЛКМ или Enter/Space. Для MENU/PAUSED, как и
            // раньше, засчитывается только когда курсор реально наведён на
            // кнопку; для SETTINGS отдельная обработка ниже (ЛКМ там же
            // запускает перетаскивание бегунка, а не только клик).
            const bool confirmDown =
                (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS ||
                 glfwGetKey(window, GLFW_KEY_ENTER)    == GLFW_PRESS ||
                 glfwGetKey(window, GLFW_KEY_KP_ENTER) == GLFW_PRESS ||
                 glfwGetKey(window, GLFW_KEY_SPACE)    == GLFW_PRESS);

            if (m_appState == AppState::SETTINGS) {
                const bool newBackHovered = insideRect(m_settingsLayout.backButton);
                const bool newSliderHovered = insideRect(m_settingsLayout.sliderPanel);
                const bool newSharpnessSliderHovered = insideRect(m_settingsLayout.sharpnessSliderPanel);
                const bool newColorCheckboxHovered = insideRect(m_settingsLayout.colorCheckbox);
                const bool newLensCheckboxHovered = insideRect(m_settingsLayout.lensCheckbox);
                if (newBackHovered != m_backHovered || newSliderHovered != m_sliderHovered ||
                    newSharpnessSliderHovered != m_sharpnessSliderHovered ||
                    newColorCheckboxHovered != m_colorCheckboxHovered ||
                    newLensCheckboxHovered != m_lensCheckboxHovered) {
                    m_backHovered = newBackHovered;
                    m_sliderHovered = newSliderHovered;
                    m_sharpnessSliderHovered = newSharpnessSliderHovered;
                    m_colorCheckboxHovered = newColorCheckboxHovered;
                    m_lensCheckboxHovered = newLensCheckboxHovered;
                    m_settingsLayoutDirty = true;
                }

                const bool lmbDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

                // Перетаскивание начинается ТОЛЬКО кликом по самой панели
                // бегунка (не по всему экрану) — но раз начавшись, держится,
                // пока зажата ЛКМ, даже если курсор уйдёт за пределы линии
                // по вертикали (обычное поведение слайдера в большинстве UI).
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

                // Слайдер SHARPNESS — тот же паттерн перетаскивания, что и
                // у SENSITIVITY выше, только пишет в ascii.setUserCellSize()
                // вместо scene.setMouseSensitivity(). Перегенерация
                // шрифтовых атласов (см. AsciiEffect::setUserCellSize())
                // дороговата на каждый пиксель мыши — но т.к. значение
                // округляется до целого (slider01ToCellSize()), реальных
                // вызовов на весь диапазон в разы меньше, чем кадров, и
                // setUserCellSize() сама не делает лишней работы, если
                // итоговое целое не изменилось.
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

                // Чекбокс COLOR — обычный edge-triggered клик/Enter/Space,
                // просто переключает bool (как обычная кнопка, а не
                // перетаскивание, поэтому не участвует в m_sliderDragging).
                if (confirmDown && !m_confirmKeyWasDown && !m_sliderDragging && !m_sharpnessSliderDragging && m_colorCheckboxHovered) {
                    m_colorEnabled = !m_colorEnabled;
                    m_settingsLayoutDirty = true;
                }

                // Чекбокс LENS — тот же edge-triggered паттерн, что и COLOR.
                if (confirmDown && !m_confirmKeyWasDown && !m_sliderDragging && !m_sharpnessSliderDragging && m_lensCheckboxHovered) {
                    m_lensEnabled = !m_lensEnabled;
                    m_settingsLayoutDirty = true;
                    std::printf("[lens] toggled -> %s\n", m_lensEnabled ? "ON" : "OFF");
                }

                // BACK — обычный edge-triggered клик/Enter/Space, но только
                // если это НЕ тот же клик, что только что начал
                // перетаскивание бегунка (у них разные прямоугольники, так
                // что реального пересечения нет — проверка чисто для ясности).
                if (confirmDown && !m_confirmKeyWasDown && !m_sliderDragging && !m_sharpnessSliderDragging && m_backHovered) {
                    m_appState = m_settingsReturnState;
                    m_backHovered = false;
                    m_sliderHovered = false;
                    m_sharpnessSliderHovered = false;
                    m_colorCheckboxHovered = false;
                }
            } else if (m_appState == AppState::CONTINUE_SELECT) {
                // Слоты подсвечиваются/кликаются ТОЛЬКО если реально заняты
                // (m_continueLayout.slotFilled[i]) — пустой слот "EMPTY" не
                // получает newHoveredButton вообще (остаётся -1, либо
                // hover переходит на BACK, если курсор там), поэтому и клик
                // по нему ничего не запускает: сам факт "выбрать пустой
                // файл нельзя" обеспечивается тем, что hoveredButton для
                // него никогда не устанавливается.
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
                        // BACK — мгновенно назад в главное меню, без
                        // затемнения (тот же стиль, что и BACK в SETTINGS).
                        m_appState = AppState::MENU;
                        m_hoveredButton = -1;
                    } else {
                        // Слот гарантированно занят (см. комментарий выше) —
                        // грузим его через обычный чёрный фейд, ту же схему,
                        // что и NEW GAME/EXIT (тяжёлая перегенерация карты
                        // происходит, когда экран уже полностью чёрный, см.
                        // FADE_TO_BLACK ниже).
                        m_pendingLoadSlot = m_hoveredButton;
                        m_pendingAction = PendingAction::LOAD_GAME;
                        m_appState = AppState::FADE_TO_BLACK;
                        m_hoveredButton = -1;
                    }
                }
            } else if (m_appState == AppState::SAVE_SELECT) {
                // В отличие от CONTINUE_SELECT, здесь кликабельны ВСЕ 3
                // слота, включая пустые (пустой -> сразу переходим к вводу
                // имени, см. AppState::SAVE_NAME_ENTRY ниже; занятый ->
                // сперва подтверждение перезаписи, см. AppState::
                // SAVE_CONFIRM ниже).
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
                        // BACK — мгновенно назад в меню паузы.
                        m_appState = AppState::PAUSED;
                        m_hoveredButton = -1;
                    } else if (m_saveLayout.slotFilled[m_hoveredButton]) {
                        // Слот занят — прежде чем перезаписать, спрашиваем
                        // подтверждение (см. ТЗ: "прежде чем перезаписать
                        // выходит предупреждение").
                        m_pendingSaveSlot = m_hoveredButton;
                        m_appState = AppState::SAVE_CONFIRM;
                        m_hoveredButton = -1;
                        m_saveConfirmLayoutDirty = true;
                    } else {
                        // Слот пуст — сразу к вводу имени (игрок сам
                        // называет своё сохранение, см. ТЗ), пустой буфер.
                        m_pendingSaveSlot = m_hoveredButton;
                        m_saveNameBuffer.clear();
                        m_appState = AppState::SAVE_NAME_ENTRY;
                        m_hoveredButton = -1;
                        m_backHoveredNameEntry = false;
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
                        // YES — переходим к вводу имени (не сохраняем
                        // сразу): буфер предзаполняем ИМЕНЕМ, которое уже
                        // лежит в этом слоте, чтобы можно было просто
                        // нажать ENTER и оставить как было, либо стереть
                        // и ввести новое.
                        m_saveNameBuffer = SaveSystem::LoadSlot(m_pendingSaveSlot).name;
                        m_appState = AppState::SAVE_NAME_ENTRY;
                        m_backHoveredNameEntry = false;
                        m_nameEntryLayoutDirty = true;
                    } else {
                        // NO — назад к выбору слота (не сразу в паузу: дать
                        // возможность выбрать другой слот).
                        m_pendingSaveSlot = -1;
                        m_appState = AppState::SAVE_SELECT;
                        m_saveLayoutDirty = true;
                    }
                    m_hoveredButton = -1;
                }
            } else if (m_appState == AppState::SAVE_NAME_ENTRY) {
                // Здесь у мыши есть работа только с BACK (отмена) — сам
                // набор текста обрабатывается отдельно, ниже по клавишам
                // (см. блок "Ввод имени сохранения" дальше в tick()).
                const bool newBackHovered = insideRect(m_nameEntryLayout.backButton);
                if (newBackHovered != m_backHoveredNameEntry) {
                    m_backHoveredNameEntry = newBackHovered;
                    m_nameEntryLayoutDirty = true;
                }
                if (confirmDown && !m_confirmKeyWasDown && m_backHoveredNameEntry) {
                    m_appState = AppState::SAVE_SELECT;
                    m_saveNameBuffer.clear();
                    m_saveLayoutDirty = true;
                }
            } else {
                int newHoveredButton = -1;
                bool newTitleHovered = false;
                if (m_appState == AppState::MENU) {
                    // CELL проверяем ПЕРВЫМ: рамка заголовка расположена выше
                    // кнопок и не пересекается с ними по вертикали (см. проверку
                    // в MainMenu.h/DrawDarkFantasyAtmosphere), так что порядок
                    // здесь для ясности, а не для приоритета одного над другим.
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
                } else { // AppState::PAUSED
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
                        // CELL — отдельная интерактивная область, но не кнопка
                        // меню: клик по названию ничего не запускает и не
                        // закрывает игру, а только ломает ASCII-логотип.
                        // Хитбокс клика — titleBoxRect (та же щедрая рамка,
                        // что и hover-подсветка), а вот origin для физики
                        // частиц ниже — ТОЧНЫЕ titleRect.x0/y0 (голый текст),
                        // это разные прямоугольники нарочно (см. Layout в
                        // MainMenu.h): сдвинь их местами — и разлёт "CELL"
                        // рассинхронится с реально нарисованными буквами.
                        const int titleScaleBase = std::max(1, uiRows / 60);
                        const float titleScale = (float)titleScaleBase * 1.5f;

                        MainMenu::ApplyTitleClick(
                            m_titleBreakup,
                            std::max(0, ascii.getMenuGridColsForWindow(winW)),
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
                            // NEW GAME (0) -> новый случайный лабиринт;
                            // CONTINUE (1) -> экран выбора слота (см.
                            // AppState::CONTINUE_SELECT); SETTINGS (2) ->
                            // мгновенный переход, без затемнения — фон
                            // остаётся тем же вращающимся меню (см.
                            // settingsFromMenu выше); EXIT (3) -> единственный
                            // путь реально закрыть приложение (см. PendingAction).
                            if (m_hoveredButton == 1) {
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
                                m_pendingAction = (m_hoveredButton == 3) ? PendingAction::QUIT_APP
                                                                      : PendingAction::NEW_GAME;
                                m_appState = AppState::FADE_TO_BLACK;
                            }
                        } else {
                            // PAUSED: RESUME (0) -> мгновенно назад в игру, без
                            // затемнения. SAVE (1) -> экран выбора слота для
                            // записи (см. AppState::SAVE_SELECT), тоже
                            // мгновенно. SETTINGS (2) -> тоже мгновенно, фон
                            // остаётся замороженным кадром паузы (см.
                            // settingsFromPause выше). MENU (3) -> обычное
                            // затемнение и возврат на стартовый экран;
                            // приложение НЕ закрывается.
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
                                // MENU из паузы: новый дизайн главного меню
                                // выбираем В МОМЕНТ КЛИКА, а не когда фейд уже
                                // закончился. Поэтому FADE_TO_MENU с первого же
                                // кадра использует новый вариант.
                                //
                                // Автосейв ТЕКУЩЕЙ игры прямо здесь, до
                                // затемнения — "Продолжить" должно отражать
                                // прогресс на момент выхода в меню, а не
                                // только периодический автосейв во время
                                // самого геймплея (см. m_autosaveTimer ниже).
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

        // ---- Ввод имени сохранения (AppState::SAVE_NAME_ENTRY) ----
        // Отдельный блок, не часть общего меню-хендлинга выше: там работа
        // с МЫШЬЮ (наведение/клик по кнопкам), здесь — с КЛАВИАТУРОЙ
        // (буквы/цифры/Backspace/Enter). Опрашиваем состояние клавиш
        // руками (glfwGetKey), а не через колбэк — так же, как и весь
        // остальной инпут в этом движке (см. gameplayActive выше) — и
        // сравниваем с m_textEntryKeyWasDown для edge-triggering (иначе
        // зажатая клавиша печатала бы одну и ту же букву каждый кадр).
        if (m_appState == AppState::SAVE_NAME_ENTRY) {
            auto keyPressed = [&](int glfwKey) {
                const bool down = glfwGetKey(window, glfwKey) == GLFW_PRESS;
                const bool wasDown = m_textEntryKeyWasDown[glfwKey];
                m_textEntryKeyWasDown[glfwKey] = down;
                return down && !wasDown;
            };

            bool bufferChanged = false;

            // Буквы A-Z и цифры 0-9 — весь набор, который поддерживает
            // BigFont (см. GetBigGlyph() в BigFont.cpp: полный алфавит A-Z
            // плюс цифры добавлены именно ради этого экрана).
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

            // ENTER подтверждает — только если имя не пустое (сохранение
            // без имени не имеет смысла: экран выбора слота потом нечего
            // было бы показать вместо generic "SLOT"). Пишем в файл СРАЗУ
            // (никакого фейда — см. PendingAction выше, запись мгновенная)
            // и возвращаемся в паузу.
            if (keyPressed(GLFW_KEY_ENTER) && !m_saveNameBuffer.empty()) {
                scene.saveToSlot(m_pendingSaveSlot, m_saveNameBuffer);
                m_pendingSaveSlot = -1;
                m_saveNameBuffer.clear();
                m_appState = AppState::PAUSED;
                m_hoveredButton = -1;
            }
        }

        // ---- Фейд-переходы ----
        if (m_appState == AppState::FADE_TO_BLACK) {
            // Смерть — отдельная, более медленная скорость (см.
            // kDeathFadeOutSpeed в Application.h) — обычный выход в меню
            // (RETURN_TO_MENU и т.д.) не трогаем, он не был частью жалобы.
            const float fadeOutSpeed =
                (m_pendingAction == PendingAction::DIED) ? kDeathFadeOutSpeed : kFadeOutSpeed;
            m_fadeAlpha += fadeOutSpeed * deltaTime;
            if (m_fadeAlpha >= 1.0f) {
                m_fadeAlpha = 1.0f;

                // Экран уже полностью чёрный — решаем, что дальше, по
                // PendingAction (см. enum выше): это ЕДИНСТВЕННОЕ место,
                // где приложение может реально закрыться (QUIT_APP), и
                // выставить его может только кнопка EXIT на стартовом
                // экране — ESC/пауза сюда попасть не могут.
                if (m_pendingAction == PendingAction::QUIT_APP) {
                    glfwSetWindowShouldClose(window, true);
                } else if (m_pendingAction == PendingAction::RETURN_TO_MENU) {
                    m_appState = AppState::FADE_TO_MENU;
                } else if (m_pendingAction == PendingAction::DIED) {
                    // Смерть игрока (см. большой комментарий у DIED в
                    // Application.h) — генерируем СВЕЖУЮ карту вместо
                    // того, чтобы держать в фоне меню ту самую, где
                    // игрок только что погиб (см. запрос: "новая карта в
                    // главном меню"). Заодно newGame() сбрасывает и
                    // состояние игрока (здоровье/m_gameOver/
                    // m_deathSequenceActive/позицию камеры) — то самое,
                    // от чего "запуск [новой игры] невозможен" был бы
                    // риском, останься эти поля в состоянии "только что
                    // умер".
                    scene.newGame();
                    m_appState = AppState::FADE_TO_MENU;
                } else if (m_pendingAction == PendingAction::NEW_GAME) {
                    // Экран уже полностью чёрный — перегенерация карты и
                    // GL-геометрии (см. DungeonScene::newGame()) невидима
                    // игроку, поэтому вызывается именно здесь, а не в
                    // момент клика по кнопке.
                    scene.newGame();
                    m_autosaveTimer = 0.0f;
                    m_appState = AppState::FADE_TO_GAME;
                } else if (m_pendingAction == PendingAction::LOAD_GAME) {
                    if (!scene.loadSlot(m_pendingLoadSlot)) {
                        // Не должно случаться — UI не позволяет выбрать
                        // пустой слот (см. CONTINUE_SELECT выше), но на
                        // случай гонки (файл удалили руками прямо во время
                        // фейда) безопасный откат к новой игре лучше, чем
                        // зависание на чёрном экране без сцены.
                        scene.newGame();
                    }
                    m_pendingLoadSlot = -1;
                    m_autosaveTimer = 0.0f;
                    m_appState = AppState::FADE_TO_GAME;
                } else { // NONE, не должно случаться
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
                m_hoveredButton = -1;     // на стартовом экране наведение считается заново
                m_titleHovered = false;
                m_menuLayoutDirty = true; // на случай, если окно изменилось, пока шла игра
                // m_menuVariant уже был выбран в момент клика по MENU
                // в паузе. Здесь не выбираем второй раз, иначе один возврат
                // мог бы перескочить через композицию.
            }
        }

        // Кинематографичный буст доступен только в реальном геймплее —
        // дальность обзора тянется отдельно через DungeonScene::render()
        // (uniform renderDistance) и клавиши +/- — сюда её прокидывать не нужно.
        const bool noclipEnabled = gameplayActive && scene.isNoclipEnabled();
        const bool wantCinematicBoost =
            gameplayActive && noclipEnabled && scene.isCinematicResolutionEnabled();

        if (wantCinematicBoost != m_appliedCinematicBoost) {
            m_currentSceneW = wantCinematicBoost ? scene.getCinematicSceneWidth()  : m_normalSceneW;
            m_currentSceneH = wantCinematicBoost ? scene.getCinematicSceneHeight() : m_normalSceneH;
            ascii.resize(m_currentSceneW, m_currentSceneH);
            ascii.setCinematicMode(wantCinematicBoost, scene.getCinematicCellSize());
            m_appliedCinematicBoost = wantCinematicBoost;
            m_menuLayoutDirty = true; // сетка символов могла измениться (на случай возврата в меню позже)
        }

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);

        ascii.begin();
        scene.render(m_currentSceneW, m_currentSceneH, gameplayActive);

        // [comment corrupted in source file - original text lost/unrecoverable]
        // [comment corrupted in source file - original text lost/unrecoverable]
        // [comment corrupted in source file - original text lost/unrecoverable]
        // [comment corrupted in source file - original text lost/unrecoverable]
        // [comment corrupted in source file - original text lost/unrecoverable]
        // [comment corrupted in source file - original text lost/unrecoverable]

        // Полоса энергии/стамины: только в реальном геймплее — в меню и
        // во время фейда её не должно быть видно (это HUD выживания, а
        // не элемент меню). Заполнение полосы = стамина, "капающая"
        // рамка = здоровье. В noclip полоса плавно (не мгновенно, см.
        // AsciiEffect::end()) прячется — это debug-режим для трейлерных
        // кадров, HUD выживания там только мешает.
        ascii.setStamina(scene.getStaminaFraction(), /*enabled=*/gameplayActive && !noclipEnabled);
        ascii.setHealth(scene.getHealthFraction());

        // ---- UI-оверлей меню: стартовый экран (CELL/NEW GAME/CONTINUE/
        // SETTINGS/EXIT), экран выбора слота для загрузки (LOAD/SLOT 1-3/
        // BACK), меню паузы (PAUSED/RESUME/SAVE/SETTINGS/MENU), экран
        // выбора слота для записи (SAVE/SLOT 1-3/BACK), экран
        // подтверждения перезаписи (сообщение маленьким шрифтом/YES/NO),
        // экран ввода имени сохранения (NAME/буквы/BACK), экран настроек
        // (SETTINGS/SENSITIVITY/BACK), либо вообще ничего ----
        // Стартовый экран показывается в MENU, в FADE_TO_MENU (проявляется
        // обратно после паузы) и в FADE_TO_BLACK, если этот фейд ведёт НЕ
        // из паузы и НЕ из экрана CONTINUE (NEW GAME/EXIT со стартового
        // экрана). Экран выбора слота для загрузки — в CONTINUE_SELECT, и
        // в том же FADE_TO_BLACK, если фейд идёт ИЗ него
        // (PendingAction::LOAD_GAME) — экран темнеет поверх списка слотов,
        // а не поверх стартового экрана. Меню паузы — в PAUSED, и в том же
        // FADE_TO_BLACK, если он идёт ИЗ паузы (PendingAction::
        // RETURN_TO_MENU) — экран темнеет поверх меню паузы. Экран выбора
        // слота для записи — в SAVE_SELECT; подтверждение перезаписи — в
        // SAVE_CONFIRM; ввод имени — в SAVE_NAME_ENTRY (все три НЕ уходят
        // в FADE_TO_BLACK — запись файла мгновенная, никакого фейда не
        // требуется, см. PendingAction). Экран настроек — в SETTINGS,
        // независимо от того, откуда его открыли (см. settingsFromMenu/
        // settingsFromPause выше) — фон под ним берётся из freezeScene/
        // tickMenuCameraSpin, а не отсюда.
        enum class OverlayMode { NONE, MAIN_MENU, CONTINUE_MENU, SAVE_MENU, SAVE_CONFIRM_MENU, SAVE_NAME_MENU, PAUSE_MENU, SETTINGS_MENU, DIARY_READING, DIARY_HINT, WIN_BLOCKED_MSG };
        OverlayMode overlay = OverlayMode::NONE;
        if (m_appState == AppState::MENU || m_appState == AppState::FADE_TO_MENU) {
            overlay = OverlayMode::MAIN_MENU;
        } else if (m_appState == AppState::CONTINUE_SELECT) {
            overlay = OverlayMode::CONTINUE_MENU;
        } else if (m_appState == AppState::SAVE_SELECT) {
            overlay = OverlayMode::SAVE_MENU;
        } else if (m_appState == AppState::SAVE_CONFIRM) {
            overlay = OverlayMode::SAVE_CONFIRM_MENU;
        } else if (m_appState == AppState::SAVE_NAME_ENTRY) {
            overlay = OverlayMode::SAVE_NAME_MENU;
        } else if (m_appState == AppState::FADE_TO_BLACK) {
            if (m_pendingAction == PendingAction::RETURN_TO_MENU) {
                overlay = OverlayMode::PAUSE_MENU;
            } else if (m_pendingAction == PendingAction::LOAD_GAME) {
                overlay = OverlayMode::CONTINUE_MENU;
            } else if (m_pendingAction == PendingAction::DIED) {
                // Смерть может случиться посреди обычного геймплея, без
                // открытой паузы — экран должен просто темнеть сам по
                // себе (см. запрос: "экран темнеет медленно"), без
                // всплывающего меню паузы/стартового экрана поверх.
                overlay = OverlayMode::NONE;
            } else {
                overlay = OverlayMode::MAIN_MENU;
            }
        } else if (m_appState == AppState::PAUSED) {
            overlay = OverlayMode::PAUSE_MENU;
        } else if (m_appState == AppState::SETTINGS) {
            overlay = OverlayMode::SETTINGS_MENU;
        }
        // AppState::FADE_TO_GAME / PLAYING -> OverlayMode::NONE (обычный HUD)

        // Экран дневника/журнала (см. Diaries.h, DungeonScene::
        // isReadingOverlayOpen()) — НЕ отдельный AppState (см. большой
        // комментарий там же: это надстройка над PLAYING, не полноценный
        // экран со своим переходом/фейдом), поэтому проверяется отдельно
        // от if/else-цепочки выше, а не как ещё одна ветка m_appState.
        if (m_appState == AppState::PLAYING && scene.isReadingOverlayOpen()) {
            overlay = OverlayMode::DIARY_READING;
        } else if (m_appState == AppState::PLAYING && scene.showWinBlockedMessage()) {
            // Приоритет выше подсказки "[E] READ DIARY" ниже — сообщение
            // короткоживущее (2.5 сек, см. DungeonScene::
            // m_winBlockedMessageTimer), а игрок физически не может стоять
            // одновременно у кнопки победы и у дневника, так что реального
            // конфликта тут не бывает, порядок веток чисто формальный.
            overlay = OverlayMode::WIN_BLOCKED_MSG;
        } else if (m_appState == AppState::PLAYING && scene.nearbyDiaryIndex() != -1) {
            // Подсказка "[E] READ DIARY" — пока сам экран ещё не открыт,
            // только игрок стоит рядом с дневником в кармане.
            overlay = OverlayMode::DIARY_HINT;
        }

        if (overlay != OverlayMode::NONE) {
            // ВАЖНО: сетка строится от РЕАЛЬНОГО размера окна (w,h — тех
            // же, что передаются в ascii.end() ниже), а не от внутренней
            // FBO-сцены (getGridCols()/getGridRows()) — именно этими
            // числами оперирует шейдер (screenResolution = окно), финальный
            // кадр ведь просто растягивается на весь экран. Раньше туть
            // использовались FBO-размеры (всегда 1280x720), из-за чего при
            // fullscreen/ресайзе окна текст меню "уезжал" — сетка на CPU
            // не совпадала с сеткой, которую действительно использовал шейдер.
            // БАГФИКС/ИЗМЕНЕНИЕ ("sharpness ломает меню") — меню ВСЕГДА
            // строится в своей фиксированной, независимой от SHARPNESS
            // сетке (см. большой комментарий у AsciiEffect::
            // kMenuReferenceCellSize) — не getGridCols/RowsForWindow()
            // (те — живой cellSize сцены), а getMenuGridCols/
            // RowsForWindow(). Раньше здесь стояли живые размеры, из-за
            // чего при не-дефолтном cellSize раскладка меню могла
            // потребовать больше клеток, чем вообще есть на экране —
            // экран сходил с ума (см. историю правок).
            const int cols = ascii.getMenuGridColsForWindow(w);
            const int rows = ascii.getMenuGridRowsForWindow(h);
            const size_t expectedGridSize = (size_t)std::max(0, cols) * std::max(0, rows);

            // Подпись слота на экранах CONTINUE/SAVE — имя, которое игрок
            // сам ввёл при сохранении (см. save/SaveSystem.h: SaveData::
            // name, AppState::SAVE_NAME_ENTRY выше), а не generic "SLOT N".
            // Пустой слот -> "EMPTY". Занятый, но БЕЗ имени (единственный
            // случай — автосейв сразу со старта NEW GAME, до первого
            // ручного SAVE, см. DungeonScene::newGame()) -> просто "SLOT",
            // чтобы кнопка не осталась пустой надписью.
            auto slotLabel = [](bool filled, const std::string& name) -> std::string {
                if (!filled) return "EMPTY";
                return name.empty() ? "SLOT" : name;
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
                    // Полная загрузка каждого слота (не только SlotExists())
                    // — нужно реальное имя для подписи кнопки, не только
                    // факт занятости. Файлы крошечные, разбор трёх штук раз
                    // за вход на экран (см. m_continueLayoutDirty) ничего не
                    // стоит по времени.
                    const bool slotFilled[3] = {
                        SaveSystem::SlotExists(0),
                        SaveSystem::SlotExists(1),
                        SaveSystem::SlotExists(2)
                    };
                    const std::string slotLabels[3] = {
                        slotLabel(slotFilled[0], SaveSystem::LoadSlot(0).name),
                        slotLabel(slotFilled[1], SaveSystem::LoadSlot(1).name),
                        slotLabel(slotFilled[2], SaveSystem::LoadSlot(2).name)
                    };
                    m_continueLayout = MainMenu::BuildContinueMenu(
                        cols, rows, m_hoveredButton, m_menuSeed, slotLabels, slotFilled, m_menuVariant);
                    m_continueLayoutDirty = false;
                }
                ascii.setUIOverlay(true, m_continueLayout.grid, cols, rows);
            } else if (overlay == OverlayMode::SAVE_MENU) {
                if (m_saveLayoutDirty || m_saveLayout.grid.size() != expectedGridSize) {
                    // Occupancy/имя запрашиваются заново при каждом входе
                    // на экран (см. клик по SAVE выше и YES/NO SAVE_CONFIRM
                    // ниже), чтобы отражать актуальное состояние диска.
                    const bool slotFilled[3] = {
                        SaveSystem::SlotExists(0),
                        SaveSystem::SlotExists(1),
                        SaveSystem::SlotExists(2)
                    };
                    const std::string slotLabels[3] = {
                        slotLabel(slotFilled[0], SaveSystem::LoadSlot(0).name),
                        slotLabel(slotFilled[1], SaveSystem::LoadSlot(1).name),
                        slotLabel(slotFilled[2], SaveSystem::LoadSlot(2).name)
                    };
                    m_saveLayout = MainMenu::BuildSaveMenu(
                        cols, rows, m_hoveredButton, m_menuSeed, slotLabels, slotFilled, m_pauseVariant);
                    m_saveLayoutDirty = false;
                }
                ascii.setUIOverlay(true, m_saveLayout.grid, cols, rows);
            } else if (overlay == OverlayMode::SAVE_CONFIRM_MENU) {
                if (m_saveConfirmLayoutDirty || m_saveConfirmLayout.grid.size() != expectedGridSize) {
                    // Компактное многострочное сообщение маленьким шрифтом
                    // (см. BuildConfirmMenu()) вместо гигантского заголовка
                    // "OVERWRITE" — целая фраза-предупреждение так не
                    // влезла бы ни по ширине, ни стилистически.
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
            } else if (overlay == OverlayMode::SAVE_NAME_MENU) {
                if (m_nameEntryLayoutDirty || m_nameEntryLayout.grid.size() != expectedGridSize) {
                    m_nameEntryLayout = MainMenu::BuildNameEntryMenu(
                        cols, rows, m_menuSeed, m_saveNameBuffer, SaveSystem::kNameMaxLen,
                        m_backHoveredNameEntry, m_pauseVariant);
                    m_nameEntryLayoutDirty = false;
                }
                ascii.setUIOverlay(true, m_nameEntryLayout.grid, cols, rows);
            } else if (overlay == OverlayMode::DIARY_READING) {
                // Не кэшируется (без *_LayoutDirty флага, в отличие от
                // остальных экранов выше) — сетка маленькая и дешёвая
                // (текст + рамка + горсть потёков), пересборка каждый
                // кадр не заметна на перфомансе, а логика "когда именно
                // инвалидировать кэш" (E/Tab/стрелки меняют содержимое)
                // сложнее, чем просто не кэшировать вовсе.
                std::vector<unsigned char> diaryGrid;
                scene.buildReadingOverlayGrid(diaryGrid, cols, rows);
                ascii.setUIOverlay(true, diaryGrid, cols, rows);
            } else if (overlay == OverlayMode::DIARY_HINT) {
                // БАГФИКС ("буква E слишком большая") — BigFont не умеет
                // меньше блока 5x7 клеток (см. ComputeFinalRes() в
                // BigFont.cpp: finalRes = max(1, ...) — пол уже на
                // scale=0.5), то есть автоматически даёт скачок в ~7 раз
                // относительно обычного текста. Вместо размера — рамка-
                // "клавиша" вокруг обычной E того же размера, что и
                // остальной текст: как кнопка на клавиатуре, а не гигант.
                std::vector<unsigned char> hintGrid((size_t)std::max(0, cols) * std::max(0, rows), 0);

                const std::string text = "E READ DIARY";
                const int startCol = cols / 2 - (int)text.size() / 2;
                const int textRow = rows / 2;

                MainMenu::PutText(hintGrid, cols, rows, startCol, textRow, text);
                // Рамка вплотную вокруг одной клетки "E" (первый символ) —
                // визуально читается как клавиша-кнопка, не как декор.
                MainMenu::DrawBox(hintGrid, cols, rows,
                                   startCol - 1, textRow - 1, startCol + 1, textRow + 1,
                                   1, /*filled=*/false, /*seed=*/555);

                ascii.setUIOverlay(true, hintGrid, cols, rows);
            } else if (overlay == OverlayMode::WIN_BLOCKED_MSG) {
                // Тоже в центре экрана (у пьедестала кнопки победы игрок
                // и так смотрит прямо на неё) — сколько дневников не
                // хватает, считаем прямо здесь по diariesReadCount().
                const int have = scene.diariesReadCount();
                const int need = std::max(0, PlayerController::kMinDiariesToWin - have);
                const std::string msg = "NEED " + std::to_string(need) + " MORE DIARIES";
                std::vector<unsigned char> blockedGrid((size_t)std::max(0, cols) * std::max(0, rows), 0);
                MainMenu::PutText(blockedGrid, cols, rows,
                                   cols / 2 - (int)msg.size() / 2, rows / 2 + 2, msg);
                ascii.setUIOverlay(true, blockedGrid, cols, rows);
            } else { // SETTINGS_MENU
                // Пересобираем на каждый кадр, где значение реально могло
                // поменяться (перетаскивание бегунка меняет его непрерывно,
                // не только по границам hover/клика, как у обычных кнопок).
                if (m_settingsLayoutDirty || m_sliderDragging || m_sharpnessSliderDragging ||
                    m_settingsLayout.grid.size() != expectedGridSize) {
                    // Атмосфера настроек использует ту же композицию, что и
                    // текущий "родительский" экран (меню либо пауза), чтобы
                    // не мигать другим узором при входе/выходе.
                    const int settingsVariant =
                        (m_settingsReturnState == AppState::PAUSED) ? m_pauseVariant : m_menuVariant;
                    m_settingsLayout = MainMenu::BuildSettingsMenu(
                        cols, rows, m_menuSeed,
                        sensitivityToSlider01(scene.getMouseSensitivity()),
                        cellSizeToSlider01(ascii.getUserCellSize()),
                        ascii.getUserCellSize(),
                        m_backHovered, m_sliderHovered, m_sharpnessSliderHovered,
                        m_colorEnabled, m_colorCheckboxHovered,
                        m_lensEnabled, m_lensCheckboxHovered,
                        settingsVariant);
                    m_settingsLayoutDirty = false;
                }
                ascii.setUIOverlay(true, m_settingsLayout.grid, cols, rows);
            }
        } else {
            ascii.setUIOverlay(false, m_menuLayout.grid, 0, 0);
        }

        ascii.setFadeAlpha(m_fadeAlpha);
        ascii.setColorEnabled(m_colorEnabled);
        ascii.setLensEffectEnabled(m_lensEnabled);
        ascii.setTime((float)glfwGetTime());
        // Компас/мини-карта рисуются отдельным шейдером ПОСЛЕ ascii.end()
        // (см. renderCompassOverlay() ниже), поэтому цветной режим нужно
        // передать и сюда — тот же флаг, что ушёл в ascii.setColorEnabled().
        scene.setColorEnabled(m_colorEnabled);

        ascii.end(w, h);

        // Компас/дебаг-карта — только реальный геймплей, в меню их
        // просто нечему показывать (миникарта скрыта, noclip недоступен).
        if (gameplayActive) {
            scene.renderCompassOverlay(w, h);

            // Debug: полная карта лабиринта по кнопке M (только для
            // бета-тестирования). Рисуется поверх всего, вне ASCII-эффекта,
            // как и компас — см. DungeonScene::renderDebugMap().
            scene.renderDebugMap(w, h);

            // Текст дневников/журнала (см. ui/TextRenderer.h — TTF,
            // stb_truetype) — тем же принципом, что и компас выше: свой
            // экранный слой, рисуется ПОСЛЕ ascii.end(), не через
            // основной ASCII-постпроцесс сцены (иначе буквы бы дробились
            // на ASCII-символы вместо чтения обычным текстом). Рамка/
            // капли крови для этого же экрана уже нарисованы старым
            // сеточным UI-шрифтом чуть выше (см. OverlayMode::
            // DIARY_READING) — здесь только сама проза/заголовок/список,
            // позиционированные ВНУТРИ той же рамки через
            // DungeonScene::getReadingBoxBounds() (те же числа, что
            // рисовали саму рамку — не рассинхронизированы).
            if (scene.isReadingOverlayOpen() && m_textRenderer.isReady()) {
                const int cols = ascii.getMenuGridColsForWindow(w);
                const int rows = ascii.getMenuGridRowsForWindow(h);
                int boxX0, boxY0, boxX1, boxY1;
                scene.getReadingBoxBounds(cols, rows, boxX0, boxY0, boxX1, boxY1);

                const float cellPx = (float)AsciiEffect::kMenuReferenceCellSize;
                const float boxLeftPx   = boxX0 * cellPx;
                const float boxRightPx  = boxX1 * cellPx;
                const float boxTopPx    = boxY0 * cellPx;
                const float boxBottomPx = boxY1 * cellPx;
                const float boxWidthPx  = boxRightPx - boxLeftPx;
                const float boxCenterX  = (boxLeftPx + boxRightPx) * 0.5f;

                // Тот же тёплый тон, что и у остального UI-текста игры
                // (см. s_minimapGlyphs-рендер в AsciiEffect.cpp) — новый
                // TTF-слой не должен визуально выбиваться палитрой.
                const glm::vec3 textColor(0.86f, 0.80f, 0.62f);

                // БАГФИКС ("текст слишком мелкий") — раньше один и тот же
                // bodyScale=0.55 использовался и для прозы дневника, и
                // для списка журнала. Разделены: сама проза (нужно
                // читать ЕЁ, отсюда и был весь переход на TTF) — заметно
                // крупнее; список журнала — компактнее, ему нужно
                // вместить все 12 строк в ту же рамку.
                const float diaryReadScale = 0.85f;
                const float journalRowScale = 0.68f;

                m_textRenderer.beginFrame(w, h);

                if (const Diaries::PlacedDiary* d = scene.openDiary()) {
                    const float textAreaWidth = boxWidthPx - 60.0f; // отступы слева/справа внутри рамки
                    const std::vector<std::string> lines =
                        m_textRenderer.wrapText(d->text, textAreaWidth, diaryReadScale);

                    float lineY = boxTopPx + 80.0f;
                    const float lineH = m_textRenderer.lineHeight(diaryReadScale);
                    for (const std::string& line : lines) {
                        if (lineY > boxBottomPx - 40.0f) break; // не вылезаем за нижний край рамки
                        const float lineW = m_textRenderer.textWidth(line, diaryReadScale);
                        m_textRenderer.drawLine(line, boxCenterX - lineW * 0.5f, lineY, diaryReadScale, textColor);
                        lineY += lineH;
                    }
                } else if (scene.isJournalListMode()) {
                    // БАГФИКС ("в LOG нет ещё 2 книг, видно только 10 из
                    // 12") — заголовок "LOG" убран целиком (по просьбе),
                    // это освобождает место сверху под список, и сам
                    // список теперь начинается почти от верхнего края
                    // рамки с более компактным шагом (journalRowScale),
                    // так что все 12 строк реально помещаются в рамку, а
                    // не обрезаются по нижнему краю.
                    const int total = scene.diariesTotalCount();
                    const float rowH = m_textRenderer.lineHeight(journalRowScale);
                    float rowY = boxTopPx + 40.0f;
                    for (int i = 0; i < total; ++i) {
                        if (rowY > boxBottomPx - 30.0f) break;
                        const bool read = scene.diaryReadAt(i);
                        const std::string label = "ENTRY " + std::to_string(i + 1) + (read ? "" : " ---");

                        // Подсветка выбранной строки — сплошной прямоугольник
                        // ТЕМ ЖЕ слоем и в ТЕХ ЖЕ координатах, что и текст
                        // строки ниже (см. TextRenderer::drawRect()), поэтому
                        // гарантированно совпадает с текстом, а не рассчитана
                        // отдельно где-то ещё. Добавлена в батч ДО drawLine()
                        // этой же строки, чтобы текст оказался поверх заливки.
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
}
