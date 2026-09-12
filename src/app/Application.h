#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <string>
#include "render/AsciiEffect.h"
#include "scene/DungeonScene.h"
#include "ui/MainMenu.h"
#include "ui/TextRenderer.h"
#include "WindowManager.h"

// ============================================================================
// Application — конечный автомат приложения (меню/пауза/настройки/фейды) и
// композиция кадра. Вынесено из main.cpp при разбиении на модули (Этап 5).
// main.cpp теперь только bootstrap + один вызов app.tick(...) за кадр.
// ============================================================================
class Application {
public:
    // Вызывается один раз после создания окна/сцены/AsciiEffect.
    void init(GLFWwindow* window, DungeonScene& scene, AsciiEffect& ascii);

    // Один кадр: обрабатывает ввод, обновляет стейт-машину, рисует сцену
    // и UI-оверлей. window/scene/ascii передаются те же, что и в init()
    // (см. main.cpp — единственный вызывающий).
    void tick(GLFWwindow* window, DungeonScene& scene, AsciiEffect& ascii,
              WindowManager& windowManager, float deltaTime);

    // true, пока идёт реальный геймплей (FADE_TO_GAME/PLAYING) — нужно
    // main.cpp, чтобы включать/выключать mouse-look колбэк (см. mouseCallback
    // там же) и переключать GLFW_CURSOR режим.
    bool isMouseLookEnabled() const { return m_mouseLookEnabled; }

private:
    // Настоящий TTF-рендер (stb_truetype, шрифт VT323) для текста
    // дневников/журнала — см. большой комментарий в ui/TextRenderer.h за
    // тем, почему это отдельный слой, а не расширение старого сеточного
    // UI-шрифта.
    TextRenderer m_textRenderer;

    // ---- Экраны и переходы (см. подробные комментарии в исходном main.cpp,
    // сохранены при переносе тела tick()) ----
    enum class AppState {
        MENU, CONTINUE_SELECT, SETTINGS, SAVE_SELECT, SAVE_CONFIRM, SAVE_NAME_ENTRY,
        FADE_TO_BLACK, FADE_TO_GAME, FADE_TO_MENU, PLAYING, PAUSED
    };
    enum class PendingAction {
        // NEW_GAME — генерирует новый лабиринт (см. DungeonScene::newGame()).
        // LOAD_GAME — грузит m_pendingLoadSlot (см. DungeonScene::loadSlot()).
        // Оба выполняются в момент, когда экран уже полностью чёрный (см.
        // FADE_TO_BLACK ниже) — тяжёлая работа (перегенерация карты и
        // GL-геометрии) невидима игроку. SAVE (кнопка SAVE меню паузы,
        // AppState::SAVE_SELECT/SAVE_CONFIRM/SAVE_NAME_ENTRY) через
        // FADE_TO_BLACK НЕ идёт — запись файла мгновенная, отдельного
        // PendingAction не требует (см. DungeonScene::saveToSlot(),
        // вызывается сразу по подтверждению имени клавишей ENTER).
        // DIED — смерть игрока (здоровье дошло до нуля, см.
        // PlayerController::applyDamage()/consumeDeathFadeTrigger()).
        // Отдельный от RETURN_TO_MENU: та реюзается для паузы -> меню и
        // жёстко считает, что поверх фейда уже видно меню паузы (см.
        // OverlayMode::PAUSE_MENU ниже в Application.cpp) — смерть же
        // может случиться посреди обычного геймплея, когда пауза вообще
        // не была открыта, так что overlay здесь должен остаться NONE
        // (просто тёмный экран, без всплывающего меню паузы). Плюс, в
        // отличие от RETURN_TO_MENU, для DIED генерируется НОВАЯ карта
        // (см. FADE_TO_BLACK ниже) — не оставляем в фоне меню ту самую
        // карту, где игрок только что погиб.
        NONE, NEW_GAME, LOAD_GAME, QUIT_APP, RETURN_TO_MENU, DIED
    };
    enum class OverlayMode { NONE, MAIN_MENU, CONTINUE_MENU, SAVE_MENU, SAVE_CONFIRM_MENU, SAVE_NAME_MENU, PAUSE_MENU, SETTINGS_MENU };

    static float sensitivityToSlider01(float sensitivity);
    static float slider01ToSensitivity(float t);

    // Тот же принцип, что и у sensitivityToSlider01/slider01ToSensitivity
    // выше, для нового слайдера SHARPNESS (см. MenuLayouts::
    // BuildSettingsMenu()/AsciiEffect::setUserCellSize()) — размер
    // ASCII-ячейки в пикселях, а не физическая величина, но сама
    // механика перевода "значение <-> положение бегунка 0..1" одна и та
    // же, отдельная пара функций просто чтобы не путать единицы измерения.
    static float cellSizeToSlider01(int cellSize);
    static int slider01ToCellSize(float t);

    bool m_mouseLookEnabled = false;
    bool m_cursorCaptured = false;

    bool m_fKeyWasDown = false;
    bool m_f1KeyWasDown = false; // см. Application::tick() — F1 = Performance/Stability mode

    bool m_appliedCinematicBoost = false;
    int m_currentSceneW = 0, m_currentSceneH = 0;
    // Возвращено на 1280x720 по просьбе — было временно урезано до
    // 640x360 (см. историю правок) как рычаг производительности,
    // независимый от количества факелов; после фикса реального бага в
    // PlaceTorches (см. MapGenerator.cpp::MIN_MAZE_TORCHES) в этом рычаге
    // больше не было необходимости в такой степени.
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

    // ---- CONTINUE (выбор слота для загрузки, см. save/SaveSystem.h) ----
    MainMenu::SlotMenuLayout m_continueLayout;
    bool m_continueLayoutDirty = true;
    int m_pendingLoadSlot = -1; // какой слот грузить, когда FADE_TO_BLACK дойдёт до конца (см. PendingAction::LOAD_GAME)

    // ---- SAVE (выбор слота для записи + подтверждение перезаписи +
    // ввод имени, кнопка SAVE меню паузы) ----
    MainMenu::SlotMenuLayout m_saveLayout;
    bool m_saveLayoutDirty = true;
    MainMenu::ConfirmLayout m_saveConfirmLayout;
    bool m_saveConfirmLayoutDirty = true;
    int m_pendingSaveSlot = -1; // какой слот подтверждаем перезаписать (AppState::SAVE_CONFIRM) / в какой пишем имя (SAVE_NAME_ENTRY)

    // ---- SAVE_NAME_ENTRY (игрок сам вводит имя сохранения, максимум
    // save/SaveSystem.h::kNameMaxLen символов) ----
    MainMenu::NameEntryLayout m_nameEntryLayout;
    bool m_nameEntryLayoutDirty = true;
    std::string m_saveNameBuffer;
    bool m_backHoveredNameEntry = false;
    // Отдельная edge-trigger таблица для алфавитно-цифровых клавиш + Enter/
    // Backspace (см. tick() — опрашиваются только в AppState::
    // SAVE_NAME_ENTRY). Размер — GLFW_KEY_LAST+1 (348+1): GLFW_KEY_ENTER
    // (257) и GLFW_KEY_BACKSPACE (259) лежат ЗА пределами диапазона A-Z/
    // 0-9 (65-90, 48-57), маленького массива на них не хватило бы.
    bool m_textEntryKeyWasDown[GLFW_KEY_LAST + 1] = {};

    // Автосохранение прогресса ТЕКУЩЕЙ игры (см. DungeonScene::
    // saveActiveSlot()) — периодически во время игры, чтобы "Продолжить"
    // отражало реальный прогресс, а не только момент старта/загрузки.
    float m_autosaveTimer = 0.0f;
    static constexpr float kAutosaveIntervalSeconds = 20.0f;

    AppState m_settingsReturnState = AppState::MENU;
    MainMenu::SettingsLayout m_settingsLayout;
    bool m_settingsLayoutDirty = true;
    bool m_backHovered = false;
    bool m_sliderHovered = false;
    bool m_sliderDragging = false;
    // ---- Новый слайдер SHARPNESS (см. AsciiEffect::setUserCellSize()) ----
    bool m_sharpnessSliderHovered = false;
    bool m_sharpnessSliderDragging = false;

    // УЛУЧШЕНИЕ ("чтобы при запуске уже были включены COLOR и LENS") —
    // были false по умолчанию.
    bool m_colorEnabled = true;
    bool m_lensEnabled = true;
    bool m_lensCheckboxHovered = false;
    bool m_colorCheckboxHovered = false;

    bool m_confirmKeyWasDown = false;

    float m_fadeAlpha = 0.0f;
    static constexpr float kFadeOutSpeed = 1.2f;
    // БАГФИКС ("затемнение экрана слишком быстрое при смерти") — раньше
    // общий kFadeOutSpeed использовался И для смерти, И для обычного
    // выхода в меню (RETURN_TO_MENU) — общий фейд-пайплайн (см.
    // PendingAction). Замедлять kFadeOutSpeed целиком не стали —
    // затронуло бы и обычный выход в меню, который никто не просил
    // менять. Отдельная, вдвое медленнее, скорость — используется ТОЛЬКО
    // при m_pendingAction==DIED (см. её применение в update()).
    static constexpr float kDeathFadeOutSpeed = 0.6f;
    static constexpr float kFadeInSpeed = 0.5f;
};
