#pragma once
#include <vector>
#include <string>
#include "TitleBreakup.h"

// ============================================================================
// MenuLayouts — сборка полных экранов меню (главное меню, пауза, настройки):
// объединяет TextGrid (примитивы), BigFont (заголовок/кнопки), Atmosphere
// (фоновые dark-fantasy детали) и TitleBreakup (анимация разрушения CELL)
// в готовую сетку глифов + прямоугольники кнопок для main.cpp.
// Вынесено из MainMenu.h при разбиении монолита на модули — последний и
// самый зависимый (использует все остальные ui-модули).
// ============================================================================

namespace MainMenu {

struct ButtonRect { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };

struct Layout {
    std::vector<unsigned char> grid;
    ButtonRect newGameButton;  // генерирует новый лабиринт и сразу начинает игру
    ButtonRect continueButton; // открывает экран выбора слота (см. SlotMenuLayout ниже)
    ButtonRect settingsButton; // открывает AppState::SETTINGS (см. main.cpp)
    ButtonRect exitButton;
    ButtonRect titleRect;    // точные границы ТЕКСТА "CELL" — используется как
                              // origin для ApplyTitleClick()/DrawBrokenTitle(),
                              // трогать нельзя, иначе разлёт частиц рассинхронится
                              // с реально нарисованными буквами.
    ButtonRect titleBoxRect; // рамка ВОКРУГ текста (с отступом) — по ней
                              // определяется hover/клик в main.cpp, она же
                              // передаётся при отрисовке самой рамки.
};

// То же самое, что Layout, но для меню паузы (см. BuildPauseMenu() ниже) —
// отдельные имена полей, чтобы в main.cpp нельзя было случайно перепутать
// "кнопку выхода из игры" (Layout::exitButton, главное меню) с "кнопкой
// выхода в главное меню" (PauseLayout::menuButton, меню паузы).
struct PauseLayout {
    std::vector<unsigned char> grid;
    ButtonRect resumeButton;
    ButtonRect saveButton;     // открывает AppState::SAVE_SELECT (см. SlotMenuLayout ниже)
    ButtonRect settingsButton; // открывает AppState::SETTINGS (см. main.cpp)
    ButtonRect menuButton;
};

// Экран выбора одного из 3 слотов сохранения (см. save/SaveSystem.h) —
// используется и для "CONTINUE" (загрузка, см. BuildContinueMenu() ниже),
// и для "SAVE" из меню паузы (запись, см. BuildSaveMenu() ниже): у обоих
// экранов одна и та же разметка "заголовок + 3 слота + BACK" (см.
// BuildButtonMenu()), различается только заголовок ("LOAD"/"SAVE") и то,
// как вызывающий код (Application.cpp) реагирует на клик по слоту —
// LOAD игнорирует клик по пустому слоту, SAVE наоборот в первую очередь
// использует пустые слоты (а по занятому спрашивает подтверждение
// перезаписи, см. ConfirmLayout ниже).
struct SlotMenuLayout {
    std::vector<unsigned char> grid;
    ButtonRect slotButtons[3];
    ButtonRect backButton;

    // Слот i занят (файл сохранения реально существует, см.
    // save/SaveSystem.h: SlotExists()) — влияет ТОЛЬКО на то, что дальше
    // делает вызывающий код при клике (см. комментарий выше); подпись
    // кнопки задаётся напрямую вызывающим кодом через slotLabels у
    // BuildContinueMenu()/BuildSaveMenu() ниже (имя, которое игрок сам
    // ввёл при сохранении, см. save/SaveSystem.h: SaveData::name — либо
    // "EMPTY"), сама раскладка кликабельность не решает, только
    // сообщает факт занятости.
    bool slotFilled[3] = { false, false, false };
};

// Экран подтверждения ДА/НЕТ (сейчас — только для "перезаписать занятый
// слот?", см. AppState::SAVE_CONFIRM в Application.cpp) — многострочное
// сообщение (см. BuildConfirmMenu() ниже) вместо гигантского заголовка:
// полная фраза целиком не влезла бы как BigFont-заголовок ни по ширине,
// ни стилистически (заголовок — это разовое слово вроде "SAVE"/"LOAD",
// не предложение).
struct ConfirmLayout {
    std::vector<unsigned char> grid;
    ButtonRect yesButton;
    ButtonRect noButton;
};

// Экран ввода имени сохранения (см. AppState::SAVE_NAME_ENTRY в
// Application.cpp) — игрок печатает на клавиатуре, до
// save/SaveSystem.h::kNameMaxLen символов, здесь только отображаются уже
// набранные буквы (текущий буфер приходит извне, набор символов и ввод —
// целиком в Application.cpp, эта раскладка ничего не знает про
// клавиатуру). backButton — отмена (тот же смысл, что и BACK в
// SlotMenuLayout выше); подтверждение — клавишей ENTER, отдельной кнопки
// у неё нет (см. Application.cpp).
struct NameEntryLayout {
    std::vector<unsigned char> grid;
    ButtonRect backButton;
};

// Раскладка экрана настроек (SETTINGS), см. BuildSettingsMenu() ниже —
// открывается кнопкой SETTINGS и из главного меню, и из паузы (main.cpp
// хранит, куда вернуться, отдельно — settingsReturnState).
struct SettingsLayout {
    std::vector<unsigned char> grid;
    ButtonRect backButton;
    ButtonRect sliderPanel; // весь framed-блок целиком (заголовок + бегунок) — по нему считается hover/начало перетаскивания
    int trackX0 = 0;        // клетка бегунка, соответствующая значению 0.0
    int trackX1 = 0;        // клетка бегунка, соответствующая значению 1.0
    int trackRow = 0;       // строка самой линии бегунка (для отрисовки/хит-теста)

    // ---- Строка "SHARPNESS" — тот же виджет-бегунок, что и SENSITIVITY
    // выше, одной строкой ниже (см. BuildSettingsMenu()). В отличие от
    // SENSITIVITY, значение здесь целое (размер ASCII-ячейки в
    // пикселях, см. AsciiEffect::setUserCellSize()) и его текущее число
    // рисуется ПРЯМО НА панели бегунка, справа от самой полосы — тем же
    // крупным шрифтом, что и подписи (см. DrawBigText()).
    ButtonRect sharpnessSliderPanel;
    int sharpnessTrackX0 = 0;
    int sharpnessTrackX1 = 0;
    int sharpnessTrackRow = 0;

    // ---- Строка "COLOR" — маленький квадрат-чекбокс справа от подписи.
    // Клик по ЛЮБОЙ точке colorCheckbox (сама рамка квадрата, ровно как
    // у sliderPanel выше) переключает флаг цветного режима (см.
    // AsciiEffect::setColorEnabled(), main.cpp). ----
    ButtonRect colorCheckbox; // весь квадрат целиком — по нему hover/клик

    // ---- Строка "LENS" — тот же чекбокс-паттерн, что и COLOR выше,
    // одной строкой ниже. Включает/выключает линзовую дисторсию +
    // виньетку по краям экрана (см. AsciiEffect::setLensEffectEnabled(),
    // Application.cpp). Название сознательно НЕ "FISHEYE" — по ТЗ. ----
    ButtonRect lensCheckbox;
};

// Общая реализация "заголовок + N кнопок друг под другом" — используется и
// для главного меню (START/SETTINGS/EXIT), и для паузы (RESUME/SETTINGS/MENU).
// selection: индекс кнопки, подсвеченной курсором, -1 = ни одна не подсвечена.
// seed фиксируется один раз за сессию (см. main.cpp), чтобы рваные рамки не
// перестраивались визуально при смене наведения. skipTitleDraw=true — всё
// выполняется КРОМЕ финальной отрисовки заголовка (см. Build() — кэширующая
// обёртка, которая переиспользует дорогой результат между кадрами).
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
                      // Множитель размера самих кнопок (рамка+текст), НЕ
                      // трогает заголовок и общую компоновку экрана — 1.0
                      // у главного меню/паузы, 0.85 (см. BuildSlotMenu()/
                      // BuildConfirmMenu() в .cpp) у экранов слотов
                      // сохранения и подтверждения перезаписи — они
                      // визуально заметно компактнее.
                      float buttonScale = 1.0f);

// Главное меню ("CELL" + START/SETTINGS/EXIT). Кэширует дорогую часть
// раскладки (атмосфера + рамки + текст кнопок) между кадрами, пока не
// меняются входные параметры — см. реализацию для деталей кэша.
void Build(Layout& out, int cols, int rows, int selection, int seed,
           const TitleBreakupState* titleState = nullptr,
           bool titleHovered = false,
           int variantIndex = 0);

// Меню паузы (RESUME/SAVE/SETTINGS/MENU, без заголовка).
PauseLayout BuildPauseMenu(int cols, int rows, int selection, int seed,
                            int variantIndex = 0);

// Экран "CONTINUE" (LOAD + 3 слота + BACK) — slotFilled[i] говорит,
// занят ли i-й слот (см. save/SaveSystem.h: SlotExists()); slotLabels[i] —
// готовая подпись кнопки (имя сохранения, которое ввёл игрок, либо
// "EMPTY"/"SLOT" — решает вызывающий код, Application.cpp, см.
// комментарий у SlotMenuLayout::slotFilled выше). selection работает как
// и везде (BuildButtonMenu) — индекс 0..2 это слоты, индекс 3 это BACK;
// -1 — ничего не подсвечено. Вызывающий код обязан сам не выставлять
// selection на пустой слот — раскладка это не проверяет: в LOAD-режиме
// пустой слот нельзя выбрать.
SlotMenuLayout BuildContinueMenu(int cols, int rows, int selection, int seed,
                                  const std::string slotLabels[3],
                                  const bool slotFilled[3],
                                  int variantIndex = 0);

// Экран "SAVE" (та же разметка, что и BuildContinueMenu() выше, другой
// заголовок) — открывается кнопкой SAVE меню паузы. В отличие от LOAD,
// здесь кликабельны ВСЕ 3 слота, включая пустые (запись в пустой слот
// сразу открывает ввод имени, см. AppState::SAVE_NAME_ENTRY, без
// подтверждения) — Application.cpp сам решает, показывать ли экран
// подтверждения перезаписи (см. ConfirmLayout/BuildConfirmMenu() ниже)
// по slotFilled[i].
SlotMenuLayout BuildSaveMenu(int cols, int rows, int selection, int seed,
                              const std::string slotLabels[3],
                              const bool slotFilled[3],
                              int variantIndex = 0);

// Экран подтверждения перезаписи занятого слота — messageLines рисуется
// маленьким (не гигантским заголовочным) шрифтом, по одной строке за
// раз, друг под другом, над кнопками YES/NO (см. ConfirmLayout выше).
// selection: 0=YES, 1=NO, -1=ничего не подсвечено.
ConfirmLayout BuildConfirmMenu(int cols, int rows, int selection, int seed,
                                const std::vector<std::string>& messageLines,
                                int variantIndex = 0);

// Экран ввода имени сохранения (см. NameEntryLayout выше) — currentName
// уже введённый игроком текст (может быть пустым — тогда на месте имени
// рисуются подчёркивания-плейсхолдеры по числу оставшихся символов),
// maxLen — максимальная длина имени (см. save/SaveSystem.h::kNameMaxLen,
// ui-слой намеренно не зависит от save-модуля, поэтому лимит передаёт
// вызывающий код, Application.cpp).
NameEntryLayout BuildNameEntryMenu(int cols, int rows, int seed,
                                    const std::string& currentName, int maxLen,
                                    bool backHovered,
                                    int variantIndex = 0);

// Экран настроек (слайдер чувствительности + слайдер чёткости ASCII +
// чекбоксы COLOR/LENS + кнопка BACK).
// sharpness01 — положение бегунка SHARPNESS (0..1, уже переведено из
// размера ASCII-ячейки в пикселях, см. Application::cellSizeToSlider01()/
// slider01ToCellSize()). sharpnessValue — то же значение, но КАК ЕСТЬ,
// в пикселях (целое число размера ячейки) — используется только чтобы
// напечатать его цифрами на самой панели бегунка, ни на что другое не
// влияет.
SettingsLayout BuildSettingsMenu(int cols, int rows, int seed,
                                  float sensitivity01,
                                  float sharpness01,
                                  int sharpnessValue,
                                  bool backHovered,
                                  bool sliderHovered,
                                  bool sharpnessSliderHovered,
                                  bool colorEnabled,
                                  bool colorCheckboxHovered,
                                  bool lensEnabled,
                                  bool lensCheckboxHovered,
                                  int variantIndex = 0);

} // namespace MainMenu
