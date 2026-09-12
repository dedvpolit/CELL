#include "MenuLayouts.h"
#include "TextGrid.h"
#include "BigFont.h"
#include "Atmosphere.h"
#include "UiGlyphs.h"
#include <algorithm>
#include <cmath>

namespace MainMenu {

// БАГФИКС ("sharpness сильно меняет размер меню, иконки уходят за
// экран") — раньше здесь стояло `(float)std::max(1, rows / 60)` — ЦЕЛОЕ
// деление (rows/60 обрубается до int ДО max()), да ещё и с полом в 1.0,
// который НЕЛЬЗЯ пробить вниз. rows = высота окна в АСКИ-клетках, то
// есть windowHeight/cellSize — она растёт, когда клетка становится
// МЕЛЬЧЕ (см. AsciiEffect::setUserCellSize(), настройка SHARPNESS), и
// падает, когда клетка КРУПНЕЕ. Само по себе умножение потом обратно на
// cellSize при рендере глифа (finalRes * cellSize пикселей на клетку)
// должно сокращать эту зависимость и держать РЕАЛЬНЫЙ, пиксельный
// размер текста примерно постоянным — но только если baseScale может
// СВОБОДНО уменьшаться пропорционально при рОСТЕ cellSize. С полом в
// 1.0 это работало только в одну сторону (клетка мельче — baseScale
// растёт как надо), а при УВЕЛИЧЕНИИ cellSize сверх дефолтных 11
// (rows падает ниже 60) baseScale упереться в пол и оставался 1.0,
// хотя обязан был продолжать падать, скажем, до 0.6 — из-за этого весь
// текст/раскладка меню начинали занимать РЕАЛЬНО БОЛЬШЕ пикселей экрана
// при увеличении cellSize, и на достаточно крупных клетках вылезали за
// границы окна.
//
// Исправлено: обычное float-деление (не int/int) БЕЗ пола в 1.0 — та же
// формула, что и раньше по сути ("сколько раз 60 строк умещается"),
// просто теперь честно продолжается и ниже 1.0. Пол 0.15 — чисто
// защитный (не даёт итоговому масштабу схлопнуться в 0/отрицательное
// при совсем experimental крупных cellSize за пределами официального
// диапазона AsciiEffect::kMinCellSize/kMaxCellSize), а не рабочий
// нижний предел для обычного использования.
// БАГФИКС #2 ("всё стало слишком большим после развязки меню от
// cellSize") — прошлый фикс убрал ПОЛ в 1.0, но не поставил ПОТОЛОК.
// Меню теперь всегда строится в фиксированной сетке
// (windowWidth/windowHeight делённые на kMenuReferenceCellSize=11, см.
// AsciiEffect.h) — если РЕАЛЬНОЕ окно/фреймбуфер оказывается крупнее
// эталонных 1280x720 (больше пикселей на том же мониторе, другой DPI,
// full screen на крупном экране и т.п.), rows пропорционально растёт, и
// baseScale вместе с ним — раньше это тихо маскировалось целочисленным
// делением (round down), теперь честно считается и уходит выше 1.0.
// Верхний потолок 1.0 — тот же максимум, что и был исторически
// "стандартным" видом меню (при cellSize=11 на 1280x720 rows=65,
// 65/60=1.08 — почти ровно потолок); текст меню теперь никогда не
// крупнее этого, независимо от того, насколько большой реальный экран.
// Ползунки (trackW и т.п.) этот потолок не трогает — они считаются от
// cols напрямую, не через baseScale, и уже были в порядке.
static float ComputeMenuBaseScale(int rows) {
    return std::clamp((float)rows / 60.0f, 0.15f, 1.0f);
}

// Вспомогательная функция для buttonScale (BuildButtonMenu ниже) и для
// сообщения экрана подтверждения (BuildConfirmMenu ниже) — пытается
// уменьшить шрифт на целую ступень finalRes (см. ComputeFinalRes() в
// BigFont.cpp); если шрифт уже на полу (finalRes==1, дальше некуда) —
// честно возвращает ИСХОДНЫЙ scale без изменений, а не подменяет разницу
// истончённой рамкой/отступом где-то ещё (так уже было и оказалось
// нежелательным, см. правку buttonScale в BuildButtonMenu).
static float ShrinkTextScale(float baseTextScale, float scaleMultiplier) {
    if (scaleMultiplier >= 1.0f) return baseTextScale;
    const int normalRes = ComputeFinalRes(baseTextScale);
    int reducedRes = std::max(1, (int)std::lround((double)normalRes * scaleMultiplier));
    if (reducedRes >= normalRes && normalRes > 1) {
        reducedRes = normalRes - 1;
    }
    return (float)reducedRes / (float)kMaskUpsample;
}

// Общий множитель "компактных" экранов (слоты сохранения/загрузки,
// подтверждение перезаписи, ввод имени) — применяется и к заголовку, и
// к кнопкам этих экранов через ShrinkTextScale() выше, ~20% меньше
// главного меню/паузы.
static constexpr float kCompactScale = 0.8f;


// Общая реализация "заголовок + N кнопок друг под другом" — раньше жила
// только внутри Build() (главное меню, "CELL"/START/EXIT) в виде
// BuildTwoButtonMenu() с ровно двумя кнопками; теперь обобщена до
// произвольного их числа (главное меню — START/SETTINGS/EXIT, пауза —
// RESUME/SETTINGS/MENU), чтобы вёрстка, отступы и dark-fantasy рамки не
// разъезжались между экранами. Раскладка со ВСЕМИ кнопками одной ширины
// (по самому длинному тексту среди них) не изменилась — просто теперь
// это цикл по buttonTexts.size(), а не два скопированных блока кода.
//
// selection: индекс кнопки, подсвеченной курсором (0..buttonTexts.size()-1),
// -1 (или любое другое значение) = ни одна не подсвечена — обычная рваная
// рамка у всех. Подсветка управляется ИСКЛЮЧИТЕЛЬНО наведением курсора
// (см. main.cpp: hoveredButton, пересчитывается заново каждый кадр, не
// залипает); "подсвечена" больше не значит "залита сплошным #" — см.
// новую версию DrawBox() выше.
// seed — фиксируется один раз за сессию (см. main.cpp), чтобы рваные
// рамки не "перестраивались" визуально каждый раз при смене наведения.
// titleHovered — курсор сейчас над рамкой заголовка (только для CELL в
// главном меню, у паузы заголовка нет) — включает тот же hover-акцент,
// что и у кнопок (см. DrawBox). Пересчитывается в main.cpp каждый кадр
// той же логикой, что и hoveredButton.
// outButtons — заполняется РОВНО buttonTexts.size() прямоугольниками, в
// том же порядке, что и buttonTexts (вызывающий код раскладывает их по
// именованным полям Layout/PauseLayout).
// skipTitleDraw (item 4, review): when true, every step is performed
// EXCEPT the final title-glyph draw (DrawBrokenTitle/DrawBigText) — the
// grid comes back with atmosphere + button frames + button text baked in,
// but the title area left blank. This lets a caller cache that (expensive,
// otherwise-static) result and draw only the cheap, per-frame-changing
// title glyphs on top of a copy of it, instead of re-running the entire
// layout (atmosphere pattern, box drawing, button text) every single frame
// just because the title is mid-animation. See Build() below for the
// caching wrapper that actually does this. Defaults to false so every
// existing call site (including BuildPauseMenu(), which passes an empty
// title and is unaffected either way) keeps its old one-shot behaviour.
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
                             float buttonScale) {
    grid.assign((size_t)std::max(0, cols) * std::max(0, rows), 0);
    outButtons.assign(buttonTexts.size(), ButtonRect{});
    outTitle = ButtonRect{};
    outTitleBox = ButtonRect{};

    if (cols <= 0 || rows <= 0 || buttonTexts.empty()) return;

    // Атмосфера (рамка/трещины/руна/факелы) рисуется НИЖЕ, ПОСЛЕ расчёта
    // кнопок/outTitle — ей нужен реальный прямоугольник контента, чтобы
    // не гадать координаты и не попадать руной под кнопку.

    // ---- Заголовок ----
    // title может быть пустым (см. BuildPauseMenu() ниже — экран паузы
    // теперь без заголовка, только кнопки) — тогда весь блок заголовка
    // просто не занимает места (letterH/titleWidth/titleToButtonsGap = 0),
    // а внизу центрируются только сами кнопки.
    const bool hasTitle = !title.empty();

    // baseScale — адаптивный "шаг" под размер экрана, от него отдельно
    // считаются масштаб заголовка (крупнее) и масштаб кнопок (заметно
    // мельче) — иначе кнопки визуально были бы почти как заголовок.
    const float baseScale = ComputeMenuBaseScale(rows);

    // buttonScale теперь уменьшает И заголовок ("SAVE"/"LOAD"), И кнопки
    // (не только кнопки, как раньше) — см. BuildSlotMenu()/
    // BuildConfirmMenu() ниже, передают 0.8 (~20% меньше). Главного меню
    // ("CELL") и паузы (без заголовка) это не касается — там buttonScale
    // остаётся 1.0 по умолчанию.
    const float titleScaleBase = baseScale * 1.5f;
    const float titleScale = ShrinkTextScale(titleScaleBase, buttonScale);
    const int letterH = hasTitle ? BigGlyphHeight(titleScale) : 0;
    const int titleWidth = hasTitle ? BigTextWidth(title, titleScale) : 0;

    // ---- Кнопки ----
    // buttonScale уменьшает ТОЛЬКО текст (заголовка и кнопок) — рамка
    // вокруг него ВСЕГДА обычная, как у главного меню/паузы, buttonScale
    // на неё не влияет (см. BuildSlotMenu() ниже — передаёт 0.8, экран
    // выбора слота сохранения заметно компактнее главного меню/паузы).
    //
    // ПРИНЦИПИАЛЬНО: рамка (buttonBorderThickness) и внутренний отступ
    // (buttonInnerPadding) у кнопок ВСЕГДА обычные, как у главного меню/
    // паузы — buttonScale уменьшает ТОЛЬКО сам текст (шрифт), а не рамку
    // вокруг него. Шрифт квантован шагом kMaskUpsample=2 (см.
    // ComputeFinalRes() в BigFont.cpp) и на типичном разрешении уже
    // близок к минимуму (finalRes=1, 5x7 ячеек на букву) — на самом
    // ходовом разрешении (1280x720 по умолчанию, см. main.cpp) шрифт
    // кнопок и так уже на этом полу, дальше ужимать нечего. В такой
    // ситуации кнопка честно остаётся обычного размера — это лучше, чем
    // визуально "врать" истончённой рамкой при неизменном тексте.
    const int buttonBorderThickness = 2;
    const int buttonInnerPadding = 1; // отступ текста от рамки внутри кнопки

    // Рамка вокруг заголовка (см. outTitleBox ниже) обрамляет буквы с
    // отступом titleInnerPadding + толщиной buttonBorderThickness С КАЖДОЙ
    // стороны — т.е. реальная занимаемая высота заголовка на экране
    // БОЛЬШЕ, чем просто letterH (голая высота глифов), на 2*titleOverhang.
    // Раньше это "нависание" рамки НЕ учитывалось в contentHeight/
    // contentTop (там использовался голый letterH) — на плотных
    // раскладках (когда contentTop итак прижат почти к верхнему краю,
    // см. clamp max(1,...) ниже) верхняя часть рамки заголовка уходила
    // за пределы сетки (row < 0) и обрезалась: именно это выглядело как
    // "CELL не влезает" — не сам текст, а его декоративная рамка сверху.
    // Считаем titleOverhang здесь же (а не только внутри if(hasTitle)
    // ниже), чтобы использовать его и в contentHeight, и в положении
    // titleRow.
    const int titleInnerPadding = 2;
    const int titleOverhang = hasTitle ? (titleInnerPadding + buttonBorderThickness) : 0;

    // buttonGap/titleToButtonsGap ниже — НЕ const: если после того, как
    // шрифт кнопок уже ужат до пола (см. цикл автоподгонки ниже), блок
    // всё ещё не помещается, второй проход слегка поджимает промежутки
    // между кнопками и отступ под заголовком — это тоже часть "уменьшить
    // кнопки" (их взаимное расположение, не только сам текст), и стоит
    // почти ничего визуально по сравнению с уже урезанным шрифтом.
    int buttonGap = pauseMenu ? 4 : 3;
    int titleToButtonsGap = hasTitle ? std::max(4, rows / 10) : 0;
    const int buttonCount = (int)buttonTexts.size();

    // ---- Автоподгонка размера текста кнопок под доступную высоту ----
    // Раньше buttonTextScale считался один раз по формуле (baseScale*0.5,
    // прогнанной через buttonScale/ShrinkTextScale) — на больших окнах
    // (fullscreen на 1080p/1440p/4K мониторах, где baseScale скачком
    // растёт, см. комментарий у baseScale выше) сумма высоты заголовка и
    // всех кнопок регулярно превышала rows: заголовок ("CELL"/"LOAD"/
    // "SAVE") утыкался в край экрана или в первую кнопку. Высоту
    // ЗАГОЛОВКА (letterH) здесь намеренно НЕ трогаем — она уже посчитана
    // выше по обычной формуле, ужимаем ТОЛЬКО кнопки, шаг за шагом на одну
    // ступень finalRes (см. ShrinkTextScale выше) за раз, пока весь блок
    // (заголовок + отступ + все кнопки + зазоры между ними) не уместится
    // по высоте — с небольшим запасом (targetHeight чуть меньше rows),
    // чтобы декоративная рамка атмосферы не оказывалась впритык к краю
    // экрана. Если кнопки уже на полу (finalRes==1) и всё равно не
    // влезает — дальше сжимать нечего, ShrinkTextScale сама остановит
    // цикл (см. floor-защиту внутри неё).
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
        // letterH + 2*titleOverhang — полная высота ЗАГОЛОВКА ВМЕСТЕ С его
        // рамкой (см. комментарий у titleOverhang выше), не только буквы.
        contentHeight = (letterH + 2 * titleOverhang) + titleToButtonsGap +
            buttonCount * buttonH + std::max(0, buttonCount - 1) * buttonGap;
    };
    recomputeButtonMetrics();

    const int targetHeight = std::max(1, rows - std::max(2, rows / 30));
    for (int guard = 0; guard < 6 && contentHeight > targetHeight; ++guard) {
        const float smaller = ShrinkTextScale(buttonTextScale, 0.99f);
        if (smaller >= buttonTextScale) break; // уже на полу — дальше некуда
        buttonTextScale = smaller;
        recomputeButtonMetrics();
    }

    // Второй проход: шрифт кнопок уже на полу, а блок всё ещё не влезает
    // (маленькое окно/много кнопок сразу, см. targetHeight выше) — донажимаем
    // зазоры между кнопками (buttonGap, floor 1) и под заголовком
    // (titleToButtonsGap, floor 3) по одной клетке за раз, поочерёдно, пока
    // не влезет или оба не упрутся в свой пол. Рамку/отступ самих кнопок
    // (buttonBorderThickness/buttonInnerPadding) не трогаем — см. комментарий
    // выше, это единственное, что должно визуально совпадать со START/EXIT
    // на любом экране.
    for (int guard = 0; guard < 12 && contentHeight > targetHeight; ++guard) {
        const int minButtonGap = 1;
        const int minTitleGap = hasTitle ? 3 : 0;
        bool shrunk = false;
        if (buttonGap > minButtonGap) { --buttonGap; shrunk = true; }
        else if (titleToButtonsGap > minTitleGap) { --titleToButtonsGap; shrunk = true; }
        if (!shrunk) break; // оба зазора уже на полу
        recomputeButtonMetrics();
    }
    const int contentShiftY = pauseMenu ? -1 : 0;
    const int contentTop = std::max(1, (rows - contentHeight) / 2 + contentShiftY);

    const int titleCol = std::max(0, (cols - titleWidth) / 2);
    // titleRow сдвинут от contentTop на titleOverhang — оставляет место
    // СВЕРХУ под рамку заголовка (outTitleBox ниже), которая иначе
    // начиналась бы выше contentTop и обрезалась (см. комментарий у
    // titleOverhang выше).
    const int titleRow = contentTop + titleOverhang;

    if (hasTitle) {
        outTitle = ButtonRect{
            titleCol,
            titleRow,
            titleCol + std::max(0, titleWidth - 1),
            titleRow + std::max(0, letterH - 1)
        };

        // Рамка вокруг заголовка — с отступом от букв побольше, чем у
        // кнопок (titleInnerPadding=2 против buttonInnerPadding=1): текст
        // заметно крупнее, вплотную рамка выглядела бы тесной. Толщина
        // рамки та же (buttonBorderThickness — теперь ВСЕГДА обычная, см.
        // комментарий выше), чтобы визуально совпадать со START/EXIT —
        // единая школа на весь экран, не два разных стиля.
        outTitleBox = ButtonRect{
            outTitle.x0 - titleInnerPadding - buttonBorderThickness,
            outTitle.y0 - titleInnerPadding - buttonBorderThickness,
            outTitle.x1 + titleInnerPadding + buttonBorderThickness,
            outTitle.y1 + titleInnerPadding + buttonBorderThickness
        };
    }

    const int buttonCol = std::max(0, (cols - buttonW) / 2);

    std::vector<ButtonRect> buttons(buttonCount);
    // titleRow + letterH + titleOverhang == contentTop + (letterH + 2*titleOverhang)
    // — низ ПОЛНОГО блока заголовка (буквы + нижняя половина рамки), см.
    // contentHeight выше; при hasTitle=false titleOverhang=letterH=0, и
    // это просто contentTop, как и раньше.
    int nextY0 = titleRow + letterH + titleOverhang + titleToButtonsGap;
    for (int i = 0; i < buttonCount; ++i) {
        buttons[i] = ButtonRect{ buttonCol, nextY0, buttonCol + buttonW - 1, nextY0 + buttonH - 1 };
        nextY0 = buttons[i].y1 + 1 + buttonGap;
    }
    outButtons = buttons;

    // Теперь у нас есть реальные координаты — считаем bbox контента
    // (рамка заголовка, если есть, объединённая со всеми кнопками) и
    // рисуем атмосферу вокруг него. bbox использует именно outTitleBox
    // (с отступом+рамкой), а не голый текст outTitle — иначе атмосфера
    // подумает, что контент уже кончился там, где на самом деле ещё
    // стоит новая рамка вокруг CELL, и залезет на неё пеплом/трещиной.
    // Вызывается ДО DrawBox() кнопок специально: если декор всё же на
    // пару клеток заденет их зону (не должен благодаря отступам внутри
    // DrawDarkFantasyAtmosphere, но на экстремально малых разрешениях
    // подстраховка не помешает), кнопки перекроют его сверху.
    AtmosphereBounds contentBounds;
    contentBounds.x0 = hasTitle ? std::min(outTitleBox.x0, buttons.front().x0) : buttons.front().x0;
    contentBounds.y0 = hasTitle ? outTitleBox.y0 : buttons.front().y0;
    contentBounds.x1 = hasTitle ? std::max(outTitleBox.x1, buttons.front().x1) : buttons.front().x1;
    contentBounds.y1 = buttons.back().y1;
    DrawDarkFantasyAtmosphere(grid, cols, rows, seed, pauseMenu, contentBounds, atmosphereVariant);

    // Рамка заголовка — та же DrawBox, что и у кнопок (одинаковая рваная
    // dark-fantasy линия, тот же hover-акцент), рисуется ДО текста CELL
    // по тому же принципу, что и у кнопок (рамка сначала, буквы поверх).
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

    // CELL рисуется ПОСЛЕ кнопок. Поэтому при третьем клике свисающая
    // надпись может спокойно лежать поверх START, а после пятого клика
    // падающее название остаётся поверх всех элементов меню.
    if (hasTitle && !skipTitleDraw) {
        if (titleState) {
            DrawBrokenTitle(grid, cols, rows, title, titleCol, titleRow, titleScale, *titleState);
        } else {
            DrawBigText(grid, cols, rows, title, titleCol, titleRow, titleScale, seed);
        }
    }
}

// Стартовый экран: заголовок "CELL" + кнопки NEW GAME/CONTINUE/SETTINGS/
// EXIT. EXIT здесь — единственное место, где допустимо реально закрыть
// приложение (см. main.cpp: PendingAction::QUIT_APP запускается только
// отсюда). NEW GAME сразу генерирует новый случайный лабиринт и
// начинает игру; CONTINUE открывает AppState::CONTINUE_SELECT — экран
// выбора одного из 3 слотов сохранения (см. SlotMenuLayout/
// BuildContinueMenu() ниже). SETTINGS открывает AppState::SETTINGS (см.
// main.cpp) — экран с одной настройкой (чувствительность камеры), см.
// BuildSettingsMenu() ниже.
// titleHovered — курсор сейчас над рамкой CELL (см. main.cpp) — включает
// тот же hover-акцент рамки, что и у кнопок.
// variantIndex — какая из kAtmosphereVariantCount композиций атмосферы
// используется (см. AtmosphereVariant/GetAtmosphereVariant/
// PickAtmosphereVariant выше). Выбирается ОДИН РАЗ в main.cpp в момент
// входа в AppState::MENU (не каждый кадр!) и передаётся сюда неизменным
// на всё время, пока меню открыто — иначе композиция "плавала" бы при
// каждом пересчёте раскладки (hover, ресайз окна).
//
// Item 4 (review): cache the static part of the layout, only redraw the
// title.
//
// While the CELL title is mid-animation, main.cpp marks the layout dirty
// and calls this EVERY frame (see menuLayoutDirty in main.cpp) — but only
// the title glyphs actually change frame to frame; the dark-fantasy
// atmosphere pattern, the button frames, and the button text are 100%
// determined by (cols, rows, selection, seed, titleHovered, variantIndex)
// and don't need to be recomputed at all as long as none of those changed.
// Previously BuildButtonMenu() re-ran ALL of that (procedural atmosphere
// generation, several DrawBox() calls, DrawBigText() for every button)
// every single frame just to get a differently-postured "CELL" on top.
//
// So: build the "base" grid ONCE per distinct (cols, rows, selection,
// seed, titleHovered, variantIndex) signature, with the title glyphs left
// out entirely (BuildButtonMenu(..., skipTitleDraw=true)), and cache it in
// static storage. Every call then just copies that cached base into *out
// (a plain buffer copy — cheap, and allocation-free once out.grid's
// capacity has stabilized, since vector::operator= reuses existing
// capacity when it's sufficient) and draws only the title glyphs on top,
// which is the one part that's actually supposed to change every frame.
//
// out is taken by reference (not returned by value) specifically so its
// std::vector<unsigned char> grid buffer persists and keeps its capacity
// across calls from main.cpp's persistently-stored menuLayout — the same
// zero-allocation-in-steady-state reasoning as Item 1's render() buffers.
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
                         variantIndex, /*skipTitleDraw=*/true);
        s_baseNewGame  = buttons[0];
        s_baseContinue = buttons[1];
        s_baseSettings = buttons[2];
        s_baseExit     = buttons[3];

        s_cCols = cols; s_cRows = rows; s_cSelection = selection; s_cSeed = seed;
        s_cVariant = variantIndex; s_cTitleHovered = titleHovered;
        s_cacheValid = true;
    }

    // Reuses out.grid's existing capacity when it's already the right size
    // (steady-state: no allocation here at all).
    out.grid = s_baseGrid;
    out.newGameButton  = s_baseNewGame;
    out.continueButton = s_baseContinue;
    out.settingsButton = s_baseSettings;
    out.exitButton      = s_baseExit;
    out.titleRect       = s_baseTitleRect;
    out.titleBoxRect    = s_baseTitleBoxRect;

    if (cols <= 0 || rows <= 0) return;

    // Only the title glyphs are drawn fresh every call — everything else
    // above came from the cache.
    const float baseScale = ComputeMenuBaseScale(rows);
    const float titleScale = baseScale * 1.5f;
    if (titleState) {
        DrawBrokenTitle(out.grid, cols, rows, "CELL", out.titleRect.x0, out.titleRect.y0, titleScale, *titleState);
    } else {
        DrawBigText(out.grid, cols, rows, "CELL", out.titleRect.x0, out.titleRect.y0, titleScale, seed);
    }
}

// Меню паузы (по ESC во время игры, см. main.cpp): БЕЗ заголовка (ни
// текстом, ни крупным dot-matrix шрифтом) — только кнопки RESUME/SETTINGS/
// MENU и декоративная dark-fantasy рамка (DrawDarkFantasyAtmosphere),
// которая сама заполняет верх композиции руной-сигилой вместо текста.
// RESUME продолжает игру мгновенно (без затемнения), SAVE открывает
// AppState::SAVE_SELECT (выбор слота для записи, см. SlotMenuLayout/
// BuildSaveMenu() ниже), SETTINGS открывает AppState::SETTINGS (см.
// main.cpp), MENU уводит на стартовый экран через обычное затемнение
// (PendingAction::RETURN_TO_MENU) — само приложение при этом НЕ
// закрывается, в отличие от EXIT в Build() выше.
// variantIndex — см. комментарий у Build() выше; выбирается один раз в
// main.cpp в момент входа в AppState::PAUSED, отдельно от menuVariant
// стартового меню (свой счётчик входов — pauseOpenCount).
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

// Общая часть BuildContinueMenu()/BuildSaveMenu() ниже — оба экрана
// отличаются только заголовком ("LOAD"/"SAVE"), сама разметка "3 слота +
// BACK" одинакова. Подписи слотов (slotLabels) готовит вызывающий код
// (Application.cpp — имя, которое ввёл игрок, либо "EMPTY"/"SLOT", см.
// комментарий у SlotMenuLayout в MenuLayouts.h) — здесь только вёрстка.
// buttonScale=kCompactScale (~20%) (см. BuildButtonMenu() выше) — слоты заметно
// компактнее кнопок главного меню/паузы.
// (internal linkage — не объявлена в MenuLayouts.h, только вспомогательная
// для BuildContinueMenu()/BuildSaveMenu() ниже в этом же файле)
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

// Экран "CONTINUE" (загрузка) — см. BuildSlotMenu() выше. Пустой слот
// НЕ кликабелен (нечего загружать) — эту часть решает вызывающий код,
// Application.cpp, по SlotMenuLayout::slotFilled, сама раскладка только
// сообщает факт.
SlotMenuLayout BuildContinueMenu(int cols, int rows, int selection, int seed,
                                  const std::string slotLabels[3],
                                  const bool slotFilled[3],
                                  int variantIndex) {
    return BuildSlotMenu(cols, rows, selection, seed, "LOAD", slotLabels, slotFilled, variantIndex);
}

// Экран "SAVE" (запись) — см. BuildSlotMenu() выше. В отличие от
// CONTINUE, здесь кликабельны ВСЕ слоты, включая пустые — запись в
// пустой слот сразу открывает ввод имени (AppState::SAVE_NAME_ENTRY,
// см. BuildNameEntryMenu() ниже), а запись в занятый вызывающий код
// (Application.cpp) должен сначала подтвердить через BuildConfirmMenu()
// ниже (AppState::SAVE_CONFIRM).
SlotMenuLayout BuildSaveMenu(int cols, int rows, int selection, int seed,
                              const std::string slotLabels[3],
                              const bool slotFilled[3],
                              int variantIndex) {
    return BuildSlotMenu(cols, rows, selection, seed, "SAVE", slotLabels, slotFilled, variantIndex);
}

// Экран подтверждения перезаписи (см. ConfirmLayout в MenuLayouts.h) —
// в отличие от главного меню/паузы/слотов, НЕ использует BuildButtonMenu()
// (тот рисует ОДИН заголовок гигантским шрифтом — целая фраза-
// предупреждение так не влезла бы ни по ширине, ни стилистически).
// Вместо этого сообщение рисуется построчно маленьким шрифтом (тем же
// ShrinkTextScale(0.85), что и у кнопок слотов — единый стиль), а YES/NO
// собраны вручную под ним, в одной общей атмосферной рамке.
ConfirmLayout BuildConfirmMenu(int cols, int rows, int selection, int seed,
                                const std::vector<std::string>& messageLines,
                                int variantIndex) {
    ConfirmLayout out;
    out.grid.assign((size_t)std::max(0, cols) * std::max(0, rows), 0);
    if (cols <= 0 || rows <= 0 || messageLines.empty()) return out;

    const float baseScale = ComputeMenuBaseScale(rows);

    // ---- Блок сообщения ----
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

    // ---- Кнопки YES/NO — та же арифметика, что и в BuildButtonMenu()
    // (тот же buttonScale=kCompactScale (~20%)), но собрана здесь вручную, чтобы блок
    // сообщения и кнопки легли в ОДНУ общую атмосферную рамку ----
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

// Экран ввода имени сохранения (см. NameEntryLayout в MenuLayouts.h) —
// заголовок "NAME" обычного (крупного) размера, под ним — уже набранные
// буквы плюс подчёркивания-плейсхолдеры на оставшиеся maxLen символов
// (см. BigUnderscore() в BigFont.cpp), снизу — BACK (отмена; подтверждение
// самого имени — клавишей ENTER, целиком в Application.cpp, тут только
// картинка).
NameEntryLayout BuildNameEntryMenu(int cols, int rows, int seed,
                                    const std::string& currentName, int maxLen,
                                    bool backHovered,
                                    int variantIndex) {
    NameEntryLayout out;
    out.grid.assign((size_t)std::max(0, cols) * std::max(0, rows), 0);
    if (cols <= 0 || rows <= 0 || maxLen <= 0) return out;

    const float baseScale = ComputeMenuBaseScale(rows);

    // ---- "TITLE" — та же школа заголовка, что SAVE/LOAD (см.
    // BuildButtonMenu()), в такой же рамке вокруг него — раньше заголовок
    // этого экрана рисовался БЕЗ рамки, теперь единый стиль со всеми
    // остальными титулами. Уменьшен на kCompactScale (~20%), как и весь
    // остальной компактный экран (слоты/подтверждение/тут).
    const std::string title = "NAME";
    const float titleScale = ShrinkTextScale(baseScale * 1.5f, kCompactScale);
    const int titleTextW = BigTextWidth(title, titleScale);
    const int titleTextH = BigGlyphHeight(titleScale);

    const int titleBorderThickness = 2;
    const int titleInnerPadding = 2; // тот же отступ, что и у SAVE/LOAD (см. BuildButtonMenu())
    const int titleBoxW = titleTextW + titleInnerPadding * 2 + titleBorderThickness * 2;
    const int titleBoxH = titleTextH + titleInnerPadding * 2 + titleBorderThickness * 2;

    // ---- Набранное имя, с подчёркиваниями на месте ещё не введённых
    // букв ("AB___" из maxLen=5) — крупнее кнопок, но заметно скромнее
    // титула, чтобы взгляд сразу шёл по иерархии TITLE -> ввод -> BACK ----
    std::string displayName = currentName;
    while ((int)displayName.size() < maxLen) displayName += '_';
    const float nameScale = ShrinkTextScale(baseScale * 0.9f, kCompactScale);
    const int nameW = BigTextWidth(displayName, nameScale);
    const int nameH = BigGlyphHeight(nameScale);

    // ---- BACK — тот же стиль/размер, что и везде (buttonScale=kCompactScale,
    // как у слотов/YES-NO) ----
    const int buttonBorderThickness = 2;
    const int buttonInnerPadding = 1;
    const float buttonTextScale = ShrinkTextScale(baseScale * 0.5f, kCompactScale);
    const std::string backText = "BACK";
    const int buttonTextAreaW = BigTextWidth(backText, buttonTextScale);
    const int buttonTextAreaH = BigGlyphHeight(buttonTextScale);
    const int buttonW = buttonTextAreaW + buttonInnerPadding * 2 + buttonBorderThickness * 2;
    const int buttonH = buttonTextAreaH + buttonInnerPadding * 2 + buttonBorderThickness * 2;

    const int titleToNameGap = std::max(3, rows / 20);
    const int nameToButtonGap = std::max(3, rows / 20);
    const int contentHeight = titleBoxH + titleToNameGap + nameH + nameToButtonGap + buttonH;
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
    const int buttonRow = nameRect.y1 + 1 + nameToButtonGap;
    out.backButton = ButtonRect{ buttonCol, buttonRow, buttonCol + buttonW - 1, buttonRow + buttonH - 1 };

    AtmosphereBounds contentBounds;
    contentBounds.x0 = std::min({ titleBox.x0, nameRect.x0, out.backButton.x0 });
    contentBounds.y0 = titleBox.y0;
    contentBounds.x1 = std::max({ titleBox.x1, nameRect.x1, out.backButton.x1 });
    contentBounds.y1 = out.backButton.y1;
    DrawDarkFantasyAtmosphere(out.grid, cols, rows, seed, /*pauseMenu=*/true, contentBounds, variantIndex);

    DrawBox(out.grid, cols, rows, titleBox.x0, titleBox.y0, titleBox.x1, titleBox.y1,
            titleBorderThickness, false, seed + 2000);
    DrawBigText(out.grid, cols, rows, title, titleTextRect.x0, titleTextRect.y0, titleScale, seed + 4000);
    DrawBigText(out.grid, cols, rows, displayName, nameRect.x0, nameRect.y0, nameScale, seed + 5000);

    DrawBox(out.grid, cols, rows, out.backButton.x0, out.backButton.y0, out.backButton.x1, out.backButton.y1,
            buttonBorderThickness, backHovered, seed);
    const int backTextCol = out.backButton.x0 + (buttonW - buttonTextAreaW) / 2;
    const int backTextRow = out.backButton.y0 + (buttonH - buttonTextAreaH) / 2;
    DrawBigText(out.grid, cols, rows, backText, backTextCol, backTextRow, buttonTextScale, seed + 1000);

    return out;
}

// ================================================================
// Экран настроек (SETTINGS) — открывается кнопкой SETTINGS и из
// главного меню, и из паузы (main.cpp запоминает, куда вернуться, в
// settingsReturnState). БЕЗ заголовка "SETTINGS" (сам факт, что мы на
// этом экране, и так понятен по кнопке, которой сюда попали — лишний
// текст только отнимает место) — сразу список настроек. Пока в списке
// одна строка: "SENSITIVITY" слева (обычная подпись, БЕЗ своей рамки —
// рамку получает только сам элемент управления, не название) и рядом
// справа от неё framed-полоса с ASCII-бегунком чувствительности камеры.
// Будущие настройки добавляются той же схемой — ещё одна строка на
// том же leftMargin, ниже.
// Кнопка BACK — единственный элемент, что остаётся отдельно и по
// центру внизу, ПО-ПРЕЖНЕМУ через собственную рамку (DrawBox), как и
// остальные кнопки меню — визуально она "кнопка", а не "поле настройки".
//
// sensitivity01 — текущее значение чувствительности, УЖЕ приведённое к
// диапазону 0..1 (см. main.cpp: (sens - min) / (max - min)); эта функция
// ничего не знает про физические единицы чувствительности камеры, только
// про положение бегунка.
// backHovered/sliderHovered — курсор сейчас над кнопкой BACK / над
// рамкой бегунка (см. main.cpp), тот же hover-акцент, что и у обычных
// кнопок меню.
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
                                          int variantIndex) {
    SettingsLayout out;
    out.grid.assign((size_t)std::max(0, cols) * std::max(0, rows), 0);
    out.backButton = ButtonRect{};
    out.sliderPanel = ButtonRect{};
    out.sharpnessSliderPanel = ButtonRect{};

    if (cols <= 0 || rows <= 0) return out;

    sensitivity01 = std::clamp(sensitivity01, 0.0f, 1.0f);
    sharpness01 = std::clamp(sharpness01, 0.0f, 1.0f);

    const bool pauseMenu = true; // тот же компактный стиль атмосферы, что у паузы (без факела снизу)

    const float baseScale = ComputeMenuBaseScale(rows);
    const float smallScale = baseScale * 0.5f; // тот же масштаб, что и у текста кнопок

    const int borderThickness = 2;
    const int innerPadding = 1;

    // ---- Отступы списка настроек от краёв экрана: не вплотную, но и не
    // по центру — левый верхний угол с небольшим полем. ----
    const int topMargin = std::max(6, rows / 14);
    const int leftMargin = std::max(6, cols / 18);

    // ---- Строка "SENSITIVITY": подпись слева + рамка бегунка справа ----
    const std::string sliderLabel = "SENSITIVITY";
    const int labelW = BigTextWidth(sliderLabel, smallScale);
    const int labelH = BigGlyphHeight(smallScale);

    // УЛУЧШЕНИЕ ("каждый ползунок/чекбокс должен быть на одном уровне
    // с другими — SHARPNESS ровно под SENSITIVITY, LENS ровно под
    // COLOR") — раньше каждая строка сама считала, где у неё начинается
    // рамка/чекбокс (labelCol + СВОЯ ширина подписи + gap), и так как
    // "SENSITIVITY"/"SHARPNESS"/"COLOR"/"LENS" разной длины, элементы
    // управления оказывались на разных X. Теперь все подписи считаются
    // здесь же, ЗАРАНЕЕ, и все четыре элемента управления стартуют с
    // ОДНОЙ и той же колонки — по самой широкой подписи (обычно
    // SENSITIVITY).
    const std::string sharpnessLabel = "SHARPNESS";
    const int sharpnessLabelW = BigTextWidth(sharpnessLabel, smallScale);
    const int sharpnessLabelH = BigGlyphHeight(smallScale);

    const std::string colorLabel = "COLOR";
    const int colorLabelW = BigTextWidth(colorLabel, smallScale);
    const int colorLabelH = BigGlyphHeight(smallScale);

    const std::string lensLabel = "LENS";
    const int lensLabelW = BigTextWidth(lensLabel, smallScale);
    const int lensLabelH = BigGlyphHeight(smallScale);

    // Промежуток между подписью и рамкой бегунка — по ширине экрана, но
    // не меньше нескольких клеток, чтобы они визуально не слипались.
    const int labelToTrackGap = std::max(3, cols / 40);

    // Общая колонка старта ВСЕХ элементов управления — считается от
    // самой широкой из четырёх подписей, а не от подписи конкретно этой
    // строки.
    const int maxLabelW = std::max({ labelW, sharpnessLabelW, colorLabelW, lensLabelW });
    const int controlCol0 = leftMargin + maxLabelW + labelToTrackGap;

    // Ширина самой линии бегунка — заметно шире, чем раньше (было
    // clamp(cols/4, 24, 64)): чем больше клеток, тем плавнее
    // перетаскивание (больше различимых промежуточных положений).
    const int trackW = std::clamp(cols / 3, 40, 100);
    const int trackH = 1;

    const int trackPanelW = trackW + innerPadding * 2 + borderThickness * 2;
    const int trackPanelH = trackH + innerPadding * 2 + borderThickness * 2;

    const int labelRow = topMargin;
    const int labelCol = leftMargin;

    const int trackPanelCol0 = controlCol0;
    // Рамка бегунка по вертикали центрируется относительно строки подписи
    // (подпись обычно ниже, чем framed-рамка, — labelH меньше trackPanelH
    // при мелком масштабе, поэтому центрируем именно так, а не подгоняем
    // высоты друг под друга).
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

    // ---- Строка "SHARPNESS": ТОЧНО тот же размер виджета, что и у
    // SENSITIVITY выше (та же trackW, та же высота панели — по прямому
    // запросу "сделать размером с ползунок чувствительности мыши, как
    // по длине так и по ширине так и по символам") — просто ещё одной
    // строкой ниже, и с той же колонки старта (controlCol0 выше — по
    // прямому запросу "SHARPNESS ровно под SENSITIVITY"). Число текущего
    // значения рисуется ПОВЕРХ полосы трека, посередине, заметно мельче
    // подписей (см. kSharpnessValueScaleDivisor ниже) — не увеличивает
    // габариты самой панели ни на клетку.
    const int sharpnessRowGap = std::max(2, rows / 30); // тот же промежуток, что и между остальными строками ниже
    const int sharpnessLabelRow = labelRow + std::max(labelH, trackPanelH) + sharpnessRowGap;
    const int sharpnessLabelCol = leftMargin;

    // Панель и трек — БУКВАЛЬНО те же trackW/trackPanelW/trackPanelH, что
    // и у SENSITIVITY (см. их вычисление выше), и та же колонка старта
    // (controlCol0) — никакого отдельного "ширже под цифры" и никакого
    // сдвига по X относительно SENSITIVITY больше нет.
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

    // Число текущего значения — заметно (в 2.5 раза) мельче подписи,
    // рисуется ПОСЕРЕДИНЕ полосы трека, поверх нарисованных под ним
    // #/./o (см. отрисовку ниже — рисуется ПОСЛЕ трека, поэтому
    // перекрывает его в тех клетках, которые занимает). Не влияет на
    // размер панели — трек по-прежнему нормально перетаскивается по
    // всей своей длине, включая клетки под числом.
    const float kSharpnessValueScaleDivisor = 2.5f;
    const float sharpnessValueScale = smallScale / kSharpnessValueScaleDivisor;
    const std::string sharpnessValueText = std::to_string(sharpnessValue);
    const int sharpnessValueW = BigTextWidth(sharpnessValueText, sharpnessValueScale);
    const int sharpnessValueH = BigGlyphHeight(sharpnessValueScale);

    const int sharpnessTrackCenterX = (sharpnessTrackX0 + sharpnessTrackX1) / 2;
    const int sharpnessValueCol = sharpnessTrackCenterX - sharpnessValueW / 2;
    const int sharpnessValueRow = sharpnessTrackRow - sharpnessValueH / 2;

    // ---- Строка "COLOR": подпись слева + маленький квадрат-чекбокс
    // справа, с той же колонки старта, что и остальные элементы
    // (controlCol0 выше), одной строкой ниже SHARPNESS. Квадрат —
    // фиксированного небольшого размера (не растягивается на всю
    // ширину, как бегунок), т.к. это просто булевый переключатель, а не
    // непрерывное значение. ----
    const int colorRowGap = std::max(2, rows / 30); // промежуток между строкой SHARPNESS и COLOR
    const int colorLabelRow = sharpnessLabelRow + std::max(sharpnessLabelH, trackPanelH) + colorRowGap;
    const int colorLabelCol = leftMargin;

    // Квадрат чекбокса: внутренняя область ровно 1x1 клетка (сам
    // "флажок"), плюс рамка borderThickness с каждой стороны — тот же
    // визуальный язык framed-панелей, что и у бегунка/кнопок.
    const int checkboxInner = 1;
    const int checkboxSize = checkboxInner + innerPadding * 2 + borderThickness * 2;

    const int checkboxCol0 = controlCol0;
    const int checkboxRow0 = colorLabelRow + (colorLabelH - checkboxSize) / 2;
    const int checkboxCol1 = checkboxCol0 + checkboxSize - 1;
    const int checkboxRow1 = checkboxRow0 + checkboxSize - 1;

    const ButtonRect checkboxRect{ checkboxCol0, checkboxRow0, checkboxCol1, checkboxRow1 };
    out.colorCheckbox = checkboxRect;

    // ---- Строка "LENS": тот же паттерн, что и COLOR выше, ещё одной
    // строкой ниже (не "FISHEYE" по ТЗ — линза уместнее вписывается в
    // общий стиль подписей COLOR/SENSITIVITY), с той же колонки старта
    // (controlCol0) — по прямому запросу "LENS ровно под COLOR". ----
    const int lensRowGap = std::max(2, rows / 30); // тот же промежуток, что между SENSITIVITY и COLOR
    const int lensLabelRow = colorLabelRow + std::max(colorLabelH, checkboxSize) + lensRowGap;
    const int lensLabelCol = leftMargin;

    const int lensCheckboxCol0 = controlCol0;
    const int lensCheckboxRow0 = lensLabelRow + (lensLabelH - checkboxSize) / 2;
    const int lensCheckboxCol1 = lensCheckboxCol0 + checkboxSize - 1;
    const int lensCheckboxRow1 = lensCheckboxRow0 + checkboxSize - 1;

    const ButtonRect lensCheckboxRect{ lensCheckboxCol0, lensCheckboxRow0, lensCheckboxCol1, lensCheckboxRow1 };
    out.lensCheckbox = lensCheckboxRect;

    // ---- Кнопка BACK — по центру внизу, НЕ зависит от раскладки строки
    // выше (свой независимый вертикальный якорь), чтобы при добавлении
    // новых строк настроек в будущем BACK не "прыгала" каждый раз. ----
    const std::string backText = "BACK";
    const int backTextW = BigTextWidth(backText, smallScale);
    const int backTextH = BigGlyphHeight(smallScale);
    const int backW = backTextW + innerPadding * 2 + borderThickness * 2;
    const int backH = backTextH + innerPadding * 2 + borderThickness * 2;

    const int backCol = std::max(0, (cols - backW) / 2);
    const int minBackRow = std::max({ trackPanelRow1, sharpnessTrackPanelRow1, checkboxRow1, lensCheckboxRow1 }) + 1 + std::max(4, rows / 10);
    const int preferredBackRow = (int)std::lround(rows * 0.70);
    const int backRow0 = std::clamp(preferredBackRow, minBackRow, std::max(minBackRow, rows - backH - 4));
    const int backRow1 = backRow0 + backH - 1;

    const ButtonRect backRect{ backCol, backRow0, backCol + backW - 1, backRow1 };
    out.backButton = backRect;

    // ---- Атмосфера ----
    AtmosphereBounds contentBounds;
    contentBounds.x0 = std::min(labelCol, backRect.x0);
    contentBounds.y0 = labelRow;
    contentBounds.x1 = std::max({ trackPanel.x1, sharpnessTrackPanel.x1, checkboxRect.x1, lensCheckboxRect.x1, backRect.x1 });
    contentBounds.y1 = backRect.y1;
    DrawDarkFantasyAtmosphere(out.grid, cols, rows, seed, pauseMenu, contentBounds, variantIndex);

    // ---- Подпись SENSITIVITY — обычный текст, без своей рамки ----
    DrawBigText(out.grid, cols, rows, sliderLabel, labelCol, labelRow, smallScale, seed);

    // ---- Рамка бегунка ----
    DrawBox(out.grid, cols, rows, trackPanel.x0, trackPanel.y0, trackPanel.x1, trackPanel.y1,
            borderThickness, sliderHovered, seed + 3000);

    // Линия бегунка: заполненная часть слева от ручки — GLYPH_HASH,
    // пустая справа — GLYPH_FLOOR (тот же "пол/пыль", что и в остальном
    // UI), сама ручка — GLYPH_CIRCLE, тот же символ, что и навершие
    // факела/рамка круга в остальном атласе — не вводим новых глифов.
    const int handleX = trackX0 + (int)std::lround(sensitivity01 * (float)(trackW - 1));
    for (int x = trackX0; x <= trackX1; ++x) {
        unsigned char g;
        if (x == handleX)      g = GLYPH_CIRCLE;
        else if (x < handleX)  g = GLYPH_HASH;
        else                    g = GLYPH_FLOOR;
        PutGlyph(out.grid, cols, rows, x, trackRow, g);
    }

    // ---- Подпись SHARPNESS + рамка бегунка + число текущего значения ----
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

    // Число текущего значения (размер ASCII-ячейки в пикселях) —
    // поверх середины полосы трека, заметно мельче подписи (см.
    // sharpnessValueScale выше) — "написано, какое число сейчас
    // выставлено" по запросу, но не увеличивает саму панель.
    DrawBigText(out.grid, cols, rows, sharpnessValueText, sharpnessValueCol, sharpnessValueRow, sharpnessValueScale, seed + 11000);

    // ---- Подпись COLOR + квадрат-чекбокс ----
    DrawBigText(out.grid, cols, rows, colorLabel, colorLabelCol, colorLabelRow, smallScale, seed + 6000);

    DrawBox(out.grid, cols, rows, checkboxRect.x0, checkboxRect.y0, checkboxRect.x1, checkboxRect.y1,
            borderThickness, colorCheckboxHovered, seed + 7000);

    // Внутренняя клетка квадрата: галочка (GLYPH_X — единственный
    // "крестовидный" символ в наборе, читается как чёткая пометка
    // внутри маленькой рамки) когда цветной режим включён, иначе пусто.
    const int checkboxCenterX = checkboxRect.x0 + borderThickness + innerPadding;
    const int checkboxCenterY = checkboxRect.y0 + borderThickness + innerPadding;
    PutGlyph(out.grid, cols, rows, checkboxCenterX, checkboxCenterY,
             colorEnabled ? GLYPH_X : GLYPH_SPACE);

    // ---- Подпись LENS + квадрат-чекбокс ----
    DrawBigText(out.grid, cols, rows, lensLabel, lensLabelCol, lensLabelRow, smallScale, seed + 8000);

    DrawBox(out.grid, cols, rows, lensCheckboxRect.x0, lensCheckboxRect.y0, lensCheckboxRect.x1, lensCheckboxRect.y1,
            borderThickness, lensCheckboxHovered, seed + 9000);

    const int lensCheckboxCenterX = lensCheckboxRect.x0 + borderThickness + innerPadding;
    const int lensCheckboxCenterY = lensCheckboxRect.y0 + borderThickness + innerPadding;
    PutGlyph(out.grid, cols, rows, lensCheckboxCenterX, lensCheckboxCenterY,
             lensEnabled ? GLYPH_X : GLYPH_SPACE);

    // ---- Кнопка BACK ----
    DrawBox(out.grid, cols, rows, backRect.x0, backRect.y0, backRect.x1, backRect.y1,
            borderThickness, backHovered, seed + 4000);
    const int backTextCol = backCol + (backW - backTextW) / 2;
    const int backTextRow = backRow0 + (backH - backTextH) / 2;
    DrawBigText(out.grid, cols, rows, backText, backTextCol, backTextRow, smallScale, seed + 4500);

    return out;
}


} // namespace MainMenu
