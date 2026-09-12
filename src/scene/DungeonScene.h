#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <array>
#include <cstdint>
#include "audio/FootstepAudio.h"
#include "WallTexture.h"
#include "MinimapFog.h"
#include "DebugMapOverlay.h"
#include "Compass.h"
#include "Culling.h"
#include "SceneGeometry.h"
#include "PlayerTorchViewmodel.h"
#include "EnemyCharacter.h"
#include "EnemyAI.h"
#include "WallShapes.h"
#include "Columns.h"
#include "Zoning.h"
#include "Diaries.h"
#include "CorridorWidth.h"
#include "DiagonalCorridors.h"
#include "Lighting.h"
#include "PlayerController.h"

// ---- Необязательные дев-инструменты (N/M/+/-/H/J), см. DevTools.h ----
// Подключается через __has_include, поэтому удаление или отсутствие
// DevTools.h НЕ ЛОМАЕТ сборку — просто HAS_DEV_TOOLS не определится, и
// весь блок дев-клавиш в processInput() окажется в неактивной ветке
// #ifdef, а обычный игрок не получит дев-возможностей.
#if __has_include("dev/DevTools.h")
#include "dev/DevTools.h"
#endif
#ifdef DEV_TOOLS_ACTIVE
#define HAS_DEV_TOOLS 1
#endif

class DungeonScene {
public:
    bool init();
    void shutdown();

    void processInput(GLFWwindow* window, float deltaTime);

    // ---- Экран чтения дневника / журнал (см. Diaries.h) ----
    // Application.cpp опрашивает это, чтобы решить, звать ли
    // processInput() (обычное движение) или tickReadingOverlayInput()
    // (E/Tab/стрелки внутри открытого экрана) — тот же принцип, что и
    // AppState::PAUSED, только решение принимается на уровне сцены, а не
    // отдельным состоянием Application (экран чтения — надстройка над
    // PLAYING, не отдельный AppState).
    bool isReadingOverlayOpen() const { return m_openDiaryIndex != -1 || m_journalOpen; }

    // Сколько дневников уже прочитано — нужно и PlayerController (гейт
    // кнопки победы, см. PlayerController::kMinDiariesToWin), и
    // Application.cpp (текст сообщения "нужно ещё N").
    int diariesReadCount() const {
        int n = 0;
        for (bool read : m_diariesRead) if (read) ++n;
        return n;
    }

    // true короткое время после неудачной попытки нажать кнопку победы
    // с недостаточным числом прочитанных дневников (см.
    // PlayerController::consumeWinBlockedRequest()) — Application.cpp
    // показывает по этому сообщение в центре экрана.
    bool showWinBlockedMessage() const { return m_winBlockedMessageTimer > 0.0f; }

    // Индекс дневника в радиусе E прямо сейчас (-1 = ни один) — публичный
    // геттер над m_nearbyDiaryIndex (обновляется в processInput()), нужен
    // Application.cpp только для подсказки "[E] READ DIARY" на HUD, пока
    // сам экран чтения ещё не открыт.
    int nearbyDiaryIndex() const { return m_nearbyDiaryIndex; }

    // Закрывает то, что сейчас открыто (сам дневник — приоритетнее
    // журнала, если открыты оба, что на практике не бывает: журнал
    // сворачивается, когда открываешь конкретную запись). Вызывается из
    // Application.cpp по Escape — той же клавишей, что закрывает любой
    // другой экран в игре, вместо того чтобы учить эту клавишу новому
    // смыслу только здесь.
    void closeDiaryOrJournal() {
        if (m_openDiaryIndex != -1) m_openDiaryIndex = -1;
        else m_journalOpen = false;
    }

    // Опрашивает E (закрыть текущий дневник / открыть выделенную запись
    // из журнала), Tab (закрыть журнал) и стрелки вверх/вниз (навигация
    // по списку журнала) — вызывается ВМЕСТО processInput(), пока
    // isReadingOverlayOpen() истинно.
    void tickReadingOverlayInput(GLFWwindow* window);

    // Строит grid для ascii.setUIOverlay() — сам текст дневника (с
    // испорченными ~словами~ и потёками) либо список журнала, в
    // зависимости от того, что сейчас открыто. Возвращает false (и
    // оставляет grid пустым), если ничего открыто не было — вызывающий
    // код просто не показывает оверлей в этом случае.
    bool buildReadingOverlayGrid(std::vector<unsigned char>& grid, int cols, int rows) const;

    // Те же границы рамки, что рисует buildReadingOverlayGrid() (в
    // клетках сетки меню) — Application.cpp пересчитывает их в пиксели
    // (см. AsciiEffect::kMenuReferenceCellSize), чтобы разместить
    // TextRenderer-текст (см. ui/TextRenderer.h) ровно внутри этой же
    // рамки, а не рассинхронизировать два независимых вычисления.
    void getReadingBoxBounds(int cols, int rows, int& x0, int& y0, int& x1, int& y1) const;

    // Открытый сейчас дневник (для рендера прозы TextRenderer'ом в
    // Application.cpp) — nullptr, если ничего не открыто.
    const Diaries::PlacedDiary* openDiary() const {
        return (m_openDiaryIndex != -1 && m_openDiaryIndex < (int)m_diaries.size())
            ? &m_diaries[(size_t)m_openDiaryIndex] : nullptr;
    }

    // Журнал открыт И ничего конкретного не читается (т.е. сейчас должен
    // рисоваться именно список, а не текст одной записи) — см.
    // buildReadingOverlayGrid() выше, та же проверка.
    bool isJournalListMode() const { return m_journalOpen && m_openDiaryIndex == -1; }

    int diariesTotalCount() const { return (int)m_diaries.size(); }
    bool diaryReadAt(int i) const { return i >= 0 && i < (int)m_diariesRead.size() && m_diariesRead[(size_t)i]; }
    int journalSelectedIndex() const { return m_journalSelectedIndex; }

    void processMouse(double xpos, double ypos);

    // Сбрасывает только базовую точку mouse-look. Yaw/pitch и позиция камеры
    // не изменяются. Нужен при повторном захвате GLFW_CURSOR_DISABLED
    // после паузы, чтобы первое событие курсора не считалось огромным dx/dy.
    void resetMouseLook() { m_player.resetMouseLook(); }

    // Статичный вид камеры для фона стартового меню (см. main.cpp) — НЕ
    // вращается, просто фиксирует небольшой наклон вниз (pitch), чтобы
    // факелы на стенах стартовой safe-zone попадали в кадр. yaw не
    // трогается — камера смотрит в том же направлении, что и обычный
    // спавн игрока. Вызывается ТОЛЬКО пока processInput()/processMouse()
    // ещё не активны (игрок не управляет персонажем). Название метода
    // сохранено ради минимальных изменений в main.cpp; deltaTime сейчас
    // не используется (раньше был нужен для скорости вращения).
    void tickMenuCameraSpin(float deltaTime);
    void tickPauseCameraIdle(float deltaTime);

    // gameplayActive — то же самое понятие, что и в Application.cpp
    // (PLAYING/FADE_TO_GAME) — пока false (пауза/меню), враг (см.
    // m_enemyAI.update() внутри) не двигается и не атакует, хотя
    // по-прежнему рисуется в своей последней позе/позиции (сцена не
    // "исчезает" под меню — просто застывает, как и всё остальное).
    // БАГФИКС: раньше m_enemyAI.update() вызывался БЕЗУСЛОВНО каждый
    // кадр вне зависимости от паузы/меню — враг продолжал преследовать
    // и атаковать игрока, даже когда игра казалась "на паузе", что при
    // поимке во время паузы приводило к вечному зависанию (см. историю:
    // последовательность смерти запускалась, но переход в меню был
    // gated по AppState::PLAYING и никогда не срабатывал).
    void render(int viewportWidth, int viewportHeight, bool gameplayActive);

    // Draws the compass directly to the currently bound framebuffer.
    // Must be called AFTER AsciiEffect::end(), not inside begin()/end(),
    // so the compass isn't re-processed by the ASCII post-effect.
    void renderCompassOverlay(int viewportWidth, int viewportHeight);

    // Debug/beta-test full map overlay, toggled with M. Draws the whole
    // maze (unfogged) as a small panel on the left half of the screen,
    // with a marker for the player's position/orientation. Must be called
    // AFTER AsciiEffect::end(), same as renderCompassOverlay(). No-ops
    // when the overlay is hidden.
    void renderDebugMap(int viewportWidth, int viewportHeight);

    // Цветной ASCII-режим (Settings -> COLOR, см. AsciiEffect::setColorEnabled()).
    // Компас/мини-карта (renderCompassOverlay -> renderCompass) рисуются
    // СВОИМ, отдельным от AsciiEffect шейдером (m_compassProgram) — он не
    // проходит через основной ASCII-постпроцесс (см. комментарий у
    // renderCompassOverlay() выше), поэтому тонировку компаса нужно
    // включать здесь отдельно, тем же флагом, что main.cpp передаёт в
    // ascii.setColorEnabled(). Ничего не делает с самой геометрией/
    // символами компаса — как и в AsciiEffect, влияет только на цвет
    // уже выбранного глифа.
    void setColorEnabled(bool enabled) { m_colorEnabled = enabled; }
    bool isColorEnabled() const { return m_colorEnabled; }

    // ---- Perf diagnostics (see Application::tick()'s [perf] fps log) ----
    // Snapshot of what render() actually submitted to the GPU LAST frame
    // — lets us tell whether an fps dip lines up with more geometry, more
    // particles, or more active torches (the expensive per-fragment
    // shadow raymarch scales with this last one), instead of guessing.
    int getLastVisibleTriangles() const { return m_lastVisibleTriangles; }
    int getLastVisibleParticles() const { return m_lastVisibleParticles; }

    // Ближняя/дальняя плоскость последнего кадра (см. render(),
    // glm::perspective()) — раньше читались Application'ом для
    // TorchAsciiEffect'а (линеаризация depth), который убран (см.
    // историю правок, жалоба "игра жрёт слишком много ОЗУ"). Сейчас
    // никем не используются — оставлены как есть (два float, ничего не
    // стоят), вдруг понадобятся другому дебаг-инструменту позже.
    float getNearPlane() const { return m_lastNearPlane; }
    float getFarPlane() const { return m_lastFarPlane; }
    int getLastActiveTorchCount() const { return m_lastActiveTorchCount; }
    int getLastVisibleChunks() const { return m_lastVisibleChunkCount; }

    static const int MAX_TORCHES = 1024; // увеличено в 2 раза (было 512) — см. также PARTICLE_ID_SCALE в шейдере
    // Perf: lowered back down from 32. Each active torch costs a full
    // per-fragment DDA shadow raymarch in scene.frag (shadowedByWall()) —
    // on weak hardware (this engine's actual target) that loop is the
    // single biggest fragment-shader cost, and it scales linearly with
    // this number for every lit pixel on screen, every frame. Left at the
    // original 20 (not lowered further) — the actual fix for "FPS drops
    // in torch-dense rooms" this round is cutting the PIXEL count instead
    // (see Application.h::m_normalSceneW/H), so torch quality doesn't
    // need to be sacrificed. If that alone isn't enough, THIS constant is
    // still the next lever to pull — it directly multiplies
    // shadowedByWall()'s cost (confirmed via GPU timing to be the single
    // most expensive part of the frame) with zero change to the lighting
    // algorithm itself.
    static const int MAX_ACTIVE_TORCHES = 20;

    // Размер мини-карты в клетках (нечётное число — центр всегда клетка
    // игрока), см. MinimapFog.h.

    // Нужно для AsciiEffect: текстура мини-карты для отрисовки
    // на компасе (0=неразведано, wall/floor/torch — см. MinimapFog::updateMinimap()).
    GLuint getMinimapTexture() const { return m_minimapFog.minimapTexture(); }
    int getMinimapSize() const { return MinimapFog::kMinimapSize; }
    float getYaw() const { return m_player.yaw(); }

    // 0..1, для полосы энергии/стамины (см. AsciiEffect::setStamina()).
    float getStaminaFraction() const { return m_player.staminaFraction(); }

    // 0..1, здоровье игрока (см. AsciiEffect::setHealth()). Пока нет
    // источников урона — источник правды на будущее для боевой системы;
    // на данный момент меняется только временными debug-клавишами
    // H/J в PlayerController::processInput() (см. комментарий там).
    float getHealthFraction() const { return m_player.healthFraction(); }

    // true, если активен debug-noclip (клавиша N, см. DevTools.h). Нужен
    // снаружи (main.cpp), чтобы на время noclip прятать полосу стамины
    // и переключать AsciiEffect в кинематографичный режим для трейлера.
    // Без DevTools.h (или при DEV_TOOLS_ENABLED=0) всегда false.
    bool isNoclipEnabled() const { return m_player.noclipEnabled(); }

    // Множитель дальности обзора (1.0 у обычного игрока), см. DevTools.h.
    // Используется в render() для RENDER_DISTANCE-uniform'а шейдера, для
    // дальней плоскости проекции и для радиуса отбора активных факелов.
    float getViewDistanceMultiplier() const {
#ifdef HAS_DEV_TOOLS
        return DevTools::GetViewDistanceMultiplier();
#else
        return 1.0f;
#endif
    }

    // Кинематографичный буст разрешения/детализации (клавиша C, см.
    // DevTools.h) — включаем ли его вообще, когда активен noclip. Без
    // DevTools.h (или если C выключил его) — всегда false, буста нет.
    bool isCinematicResolutionEnabled() const {
#ifdef HAS_DEV_TOOLS
        return DevTools::IsCinematicResolutionEnabled();
#else
        return false;
#endif
    }

    // Параметры кинематографичного режима — константы вынесены в
    // DevTools.h, чтобы весь дев-тюнинг настраивался в одном месте.
    // Без DevTools.h возвращают обычные (не увеличенные) значения —
    // не то чтобы это должно когда-либо использоваться, раз
    // isCinematicResolutionEnabled() в этом случае всегда false, но так
    // геттеры остаются осмысленными сами по себе.
    int getCinematicSceneWidth() const {
#ifdef HAS_DEV_TOOLS
        return DevTools::kCinematicSceneW;
#else
        return 1280;
#endif
    }
    int getCinematicSceneHeight() const {
#ifdef HAS_DEV_TOOLS
        return DevTools::kCinematicSceneH;
#else
        return 720;
#endif
    }
    int getCinematicCellSize() const {
#ifdef HAS_DEV_TOOLS
        return DevTools::kCinematicCellSize;
#else
        return 11;
#endif
    }

    void setCompassMinimapFont(
        GLuint texture,
        int glyphCount
    );

    // true, если игрок уже нажал кнопку победы в финишной safe-zone.
    // Само закрытие окна происходит внутри processInput() в момент нажатия,
    // геттер оставлен для возможного использования снаружи (HUD и т.п.).
    bool hasWon() const { return m_player.hasWon(); }
    // См. PlayerController::isDying()/consumeDeathFadeTrigger() — для
    // Application.cpp (последовательность смерти -> переход в меню).
    bool isDying() const { return m_player.isDying(); }
    bool consumeDeathFadeTrigger() { return m_player.consumeDeathFadeTrigger(); }
    void tickDeathFade(float deltaTime) { m_player.tickDeathFade(deltaTime); }

    // ---- Чувствительность камеры (экран SETTINGS, см. MainMenu.h/main.cpp) ----
    // Диапазон подобран вручную: kMinMouseSensitivity — заметно медленнее
    // дефолта, но ещё управляемо, kMaxMouseSensitivity — быстро, но камеру
    // ещё можно контролировать. Бегунок настроек хранит только позицию
    // 0..1 и сам пересчитывает её в это значение (см. main.cpp).
    static constexpr float kMinMouseSensitivity = PlayerController::kMinMouseSensitivity;
    static constexpr float kMaxMouseSensitivity = PlayerController::kMaxMouseSensitivity;

    float getMouseSensitivity() const { return m_player.mouseSensitivity(); }
    void setMouseSensitivity(float sensitivity) { m_player.setMouseSensitivity(sensitivity); }

    // ---- NEW GAME / CONTINUE (см. save/SaveSystem.h, ui/MenuLayouts.h) ----

    // Новый случайный лабиринт: полная перегенерация карты/факелов/GL-
    // геометрии, полный сброс игрока (позиция/здоровье/стамина/туман
    // войны) и НОВЫЙ активный слот сохранения (см. SaveSystem::
    // PickSlotForNewGame()) — сохраняется в него сразу же, чтобы экран
    // CONTINUE увидел эту игру, даже если игрок ещё не сделал ни шага.
    // Вызывается и из init() (самая первая карта сессии), и по клику
    // NEW GAME в уже запущенном приложении — во втором случае безопасно
    // пересобирает уже существующую GL-геометрию (см. .cpp).
    void newGame();

    // Загружает сохранённый слот (0..SaveSystem::kSlotCount-1). Тот же
    // seed, что был сохранён, детерминированно восстанавливает ТОЧНО тот
    // же лабиринт и расстановку факелов (см. MapGenerator.h) — поверх
    // них накатывается сохранённое состояние игрока и туман войны.
    // Возвращает false, если слот пуст/повреждён — вызывающий код
    // (Application.cpp) не должен был предлагать такой слот в UI (пустые
    // слоты не кликабельны), но проверка здесь не лишняя.
    bool loadSlot(int slotIndex);

    // Перезаписывает ТЕКУЩИЙ активный слот (см. activeSlot()) актуальным
    // состоянием — используется для автосохранения прогресса (см.
    // Application.cpp: периодически во время игры и перед выходом в
    // меню). Ничего не делает, если активного слота нет.
    void saveActiveSlot();

    // Сохраняет ТЕКУЩЕЕ состояние игры В УКАЗАННЫЙ слот под ЗАДАННЫМ
    // именем (см. AppState::SAVE_NAME_ENTRY в Application.cpp — игрок сам
    // вводит имя, до save/SaveSystem.h::kNameMaxLen символов) и делает
    // этот слот новым активным (см. activeSlot() ниже) — так что
    // дальнейшее автосохранение (периодическое и при выходе в меню)
    // продолжит писать именно в этот, только что вручную выбранный,
    // слот под тем же именем, а не в тот, с которого началась/была
    // загружена сессия.
    void saveToSlot(int slotIndex, const std::string& name) {
        m_activeSlot = slotIndex;
        m_saveName = name;
        saveActiveSlot();
    }

    // Слот, к которому привязана текущая игровая сессия — -1, пока не
    // было ни newGame(), ни loadSlot() (не должно случаться после init(),
    // см. комментарий у newGame() выше).
    int activeSlot() const { return m_activeSlot; }

private:
    // Vertex/GeoChunk перенесены в SceneGeometry.h (нужны и там, и в
    // render() ниже для чтения m_geometry.chunks()).

    // [comment corrupted in source file - original text lost/unrecoverable]
    int m_mapW = 0, m_mapH = 0;
    std::vector<int> m_map;

    // ---- Срезанные углы стен (WallShapes.h, Шаг 1 "неровные стены") ----
    // Строится один раз в generateMap() детерминированно по тому же seed,
    // что и сам лабиринт (см. WallShapes::BuildCornerCuts) — размер
    // m_mapW*m_mapH, тот же индекс z*m_mapW+x, что и m_map. Читается и
    // геометрией (m_geometry.build(), для формы меша), и коллизией
    // (wallCornerCut() ниже, передаётся в PlayerController) — одна и та
    // же таблица для обоих, чтобы силуэт и коллизия не могли разойтись.
    std::vector<WallShapes::CornerCut> m_wallCornerCuts;

    // ---- Размер среза угла поклеточно (WallShapes.h) ----
    // Обычно WallShapes::kChamferSize (мелкий скос), но клетки
    // диагональных "лестниц" (см. m_diagonalChainMask ниже) получают
    // WallShapes::kChamferSizeChain (почти половина клетки) — без этого
    // цепочка среза выглядит как ряд едва заметных царапин на прямых
    // углах, а не как связная диагональная стена (см. историю правок —
    // реальный скриншот в игре показал именно эту проблему). Читается и
    // геометрией (m_geometry.build()), и коллизией (wallChamferSize()
    // ниже) — те же индексы, что и m_wallCornerCuts.
    std::vector<float> m_chamferSizes;

    // ---- Свободностоящие колонны (Columns.h, Шаг 2 "неровные стены") ----
    // Мировые XZ-центры — строится в generateMap() (см. Columns::BuildColumns)
    // ДО m_wallCornerCuts (см. комментарий в Columns.h про порядок вызовов:
    // мутация map под колонны должна случиться раньше, чтобы соседние стены
    // могли получить органичные срезы у новых открытых граней).
    std::vector<glm::vec2> m_columnCentersXZ;

    // ---- Зонирование (Zoning.h) ----
    // В отличие от m_wallCornerCuts/m_columnCentersXZ (нужны каждый кадр
    // геометрии/коллизии), сама сетка секторов больше нигде не читается
    // после generateMap() — НО хранится здесь же (не как локальная
    // переменная), чтобы debug-карта (клавиша M, renderDebugMap()) могла
    // нарисовать границы/профиль секторов; без этого поля зонирование
    // было бы вообще не проверить на глаз, см. историю правок.
    Zoning::ZoneGrid m_zoneGrid;

    // ---- Ширина коридоров (CorridorWidth.h) ----
    // Битовая маска mapW*mapH: 0/1 — стала ли эта клетка полом ИМЕННО от
    // расширения коридора (а не изначально от MapGenerator). Как и
    // m_zoneGrid, нужна только debug-карте (клавиша M) для визуализации —
    // сама геометрия/коллизия читают уже финальный m_map и не различают,
    // откуда взялась конкретная floor-клетка.
    std::vector<unsigned char> m_corridorWidened;

    // ---- Палитра по зоне (Zoning.h, GetZonePalette в SceneGeometry.cpp) ----
    // ГОТОВЫЙ цвет стены/пола поклеточно (mapW*mapH), а не индекс палитры —
    // уже смешанный между двумя ближайшими регионами вблизи Voronoi-границы
    // (см. BlendedZoneColor() в .cpp) — баг: раньше цвет резко скакал между
    // палитрами двух соседних регионов без перехода. Строится вместе с
    // остальными поклеточными массивами в generateMap().
    std::vector<glm::vec3> m_paletteWallColors;
    std::vector<glm::vec3> m_paletteFloorColors;

    // ---- Диагональные "лестницы" (DiagonalCorridors.h) ----
    // Битовая маска mapW*mapH: 1, если эта клетка входит в достаточно
    // длинную цепочку eligible-срезов одного типа (см. DiagonalCorridors::
    // DetectChains) — их срез форсируется через wallCornerCutProbabilityAt
    // в generateMap(), а не отдаётся на волю обычной вероятности региона.
    // Как и m_zoneGrid/m_corridorWidened, хранится только ради
    // debug-карты (клавиша M) — сама генерация читает её один раз внутри
    // generateMap() и дальше не нуждается.
    std::vector<unsigned char> m_diagonalChainMask;

    // ---- Финишная safe-zone (противоположный угол карты) ----
    // Границы прямоугольника, заполненные в generateMap(); используются
    // также в placeTorches(), чтобы расставить в этой комнате свои
    // факелы и не дублировать их из общего прохода по лабиринту.
    int m_endSafeX0 = 0, m_endSafeZ0 = 0, m_endSafeX1 = 0, m_endSafeZ1 = 0;

    // ---- Маленькие safe-зоны (карманы) внутри лабиринта ----
    // Центры и радиус заполняются в generateMap(); используются в
    // placeTorches(), чтобы гарантированно поставить по 3 факела в
    // каждом кармане (а не полагаться на случайный проход по лабиринту).
    std::vector<glm::ivec2> m_smallSafeZoneCenters;
    int m_smallSafeZoneRadius = 0;

    // ---- Дневники (см. Diaries.h) ----
    // Один на карман, тот же индекс i в обоих векторах: m_diaries[i]
    // лежит физически в m_smallSafeZoneCenters[i]. m_diariesRead[i] —
    // прочитан ли (влияет только на UI/сейв, геометрия/меш не меняются —
    // дневник остаётся лежать в мире и после прочтения).
    std::vector<Diaries::PlacedDiary> m_diaries;
    std::vector<bool> m_diariesRead;
    // Индекс дневника, к которому игрок сейчас достаточно близко, чтобы
    // читать по E (-1 = ни один не в радиусе) — считается в processInput(),
    // используется и подсказкой "[E] READ DIARY", и самим экраном чтения.
    int m_nearbyDiaryIndex = -1;
    // -1 = экран чтения закрыт; иначе — индекс открытого дневника (см.
    // m_diaries) в m_diaries, читается прямо сейчас (E на месте или из
    // журнала по Tab — см. m_journalOpen).
    int m_openDiaryIndex = -1;
    // Журнал (Tab) — отдельный от m_openDiaryIndex экран со списком уже
    // прочитанных дневников для перечитывания в любой момент, не только
    // стоя рядом с карманом.
    bool m_journalOpen = false;
    int m_journalSelectedIndex = 0; // какая строка списка подсвечена
    // Edge-tracking для tickReadingOverlayInput() — отдельные от
    // PlayerController-овских m_eKeyWasDown/m_tabKeyWasDown, т.к. эта
    // функция вызывается ВМЕСТО processInput(), не вместе с ним (см.
    // Application.cpp: gameplayActive-ветка).
    bool m_overlayEKeyWasDown = false;
    bool m_overlayTabKeyWasDown = false;
    bool m_overlayUpKeyWasDown = false;
    bool m_overlayDownKeyWasDown = false;
    // Таймер показа сообщения "нужно ещё N дневников" (секунды, считает
    // вниз до 0 в processInput()) — см. showWinBlockedMessage() выше.
    float m_winBlockedMessageTimer = 0.0f;

    // Позиция кнопки победы в центре финишной safe-zone (см. generateMap()
    // и addWinButtonMesh()). Игрок нажимает E рядом с ней (см. PlayerController).
    glm::vec3 m_winButtonPos{ 0.0f, 0.0f, 0.0f };

    // ---- Debug: полная карта лабиринта по кнопке M ----
    // Только для бета-тестирования: показывает весь m_map целиком, без
    // fog of war, поверх всего экрана (вызывается после AsciiEffect::end(),
    // как и renderCompassOverlay()). Само рисование вынесено в
    // DebugMapOverlay (см. DebugMapOverlay.h) — видимость/переключение
    // клавишей M теперь состояние PlayerController (см. m_player ниже).
    DebugMapOverlay m_debugMapOverlay;

    // ---- Fog of war / мини-карта (см. MinimapFog.h) ----
    // m_map остаётся здесь (будущий MapGenerator), а сама логика тумана
    // войны и связанные GL-текстуры (полная карта + мини-карта) вынесены
    // в MinimapFog — она принимает нужные данные параметрами, а не
    // владеет ими сама (см. комментарий в MinimapFog.h).
    MinimapFog m_minimapFog;

    // [comment corrupted in source file - original text lost/unrecoverable]
    std::vector<glm::vec3> m_torchWallBase;
    std::vector<glm::vec3> m_torchNormal;
    std::vector<glm::vec3> m_torchFlamePos;
    std::vector<glm::vec3> m_torchColor;
    std::vector<float>     m_torchIntensity;

    // O(1) "is there a torch on this map cell" lookup used by
    // MinimapFog::updateMinimap(). Built once in placeTorches() (see
    // buildTorchCellLookup()) instead of the old approach, which
    // linearly rescanned the ENTIRE torch list (up to MAX_TORCHES) for
    // every single one of the kMinimapSize*kMinimapSize cells, every
    // single frame.
    std::vector<unsigned char> m_torchCellLookup; // size m_mapW*m_mapH, 0/1
    // Строится внутри placeTorches() (см. MapGenerator::PlaceTorches()).

    // ---- Геометрия (см. SceneGeometry.h) ----
    SceneGeometry m_geometry;

    // ---- Текстура стен (см. WallTexture.h) ----
    // Загружается в init() через m_wallTex.load(WallTexture::kDefaultName).
    // Чтобы поставить другую стеновую текстуру, ДОСТАТОЧНО положить нужный
    // файл в assets/textures/walls/ и поменять WallTexture::kDefaultName —
    // сам шейдер/пайплайн трогать не нужно.
    WallTexture m_wallTex;

    GLuint m_program = 0;

    // ---- Cached uniform locations for m_program ----
    // glGetUniformLocation() does a name lookup in the driver; doing this
    // ~15 times every single frame (as render() used to) is wasted CPU
    // work since the locations never change after linking. Resolved once
    // in init() (see cacheUniformLocations()) and reused every frame.
    GLint m_uniView = -1;
    GLint m_uniProjection = -1;
    GLint m_uniCamPos = -1;

    // ---- Факел в руке игрока (см. scene.frag) ----
    GLint m_uniPlayerLightPos = -1;
    GLint m_uniPlayerLightDir = -1;
    GLint m_uniPlayerLightColor = -1;
    GLint m_uniPlayerLightIntensity = -1;
    GLint m_uniIsViewmodelDraw = -1;

    // ---- Dev-tools: статичные направленные фонарики (клавиша L) ----
    GLint m_uniDevLightPos = -1;
    GLint m_uniDevLightDir = -1;
    GLint m_uniDevLightCount = -1;

    // ---- Dev-tools: перебор цветовых гамм (клавиша G) ----
    GLint m_uniDevPaletteOverride = -1;
    GLint m_uniInvView = -1;
    GLint m_uniViewmodelSway = -1;
    GLint m_uniTime = -1;
    GLint m_uniRenderDistance = -1;
    GLint m_uniMapTex = -1;
    GLint m_uniWallTex = -1;
    GLint m_uniWallTexEnabled = -1;
    GLint m_uniWallTexContrast = -1;
    GLint m_uniTorchPos = -1;
    GLint m_uniTorchColor = -1;
    GLint m_uniTorchIntensity = -1;
    GLint m_uniTorchCount = -1;
    // Колонны (Шаг 2) — нужны шейдеру для ray-vs-circle теста в тенях
    // (см. assets/shaders/scene.frag: shadowedByWall()), т.к. клетка
    // колонны теперь пол в mapTex и сама по себе тени не давала бы (см.
    // историю правок про "тени как от квадратов"/отсутствие тени у колонн).
    GLint m_uniColumnPos = -1;
    GLint m_uniColumnCount = -1;
    GLint m_uniColumnRadius = -1;

    // ---- Тени от врагов (см. большой комментарий в shadowedByWall(),
    // assets/shaders/scene.frag) — точный ray-vs-cylinder тест, тем же
    // приёмом, что и колонны выше (columnPos/columnCount/columnRadius),
    // просто с дополнительной проверкой по высоте (враг не бесконечно
    // высокий, как колонна). ПЕРВАЯ версия этой фичи использовала
    // отдельную растеризованную карту глубины (общий "shadow map",
    // 1024x1024 FBO, ортокамера над игроком) — откачена: сэмплирование
    // луча в нескольких точках регулярно "проскакивало" мимо тонкого
    // (радиус ~0.3) силуэта врага между сэмплами, что на практике давало
    // едва заметные, беспорядочные пятна вместо тени, плюс не нужный
    // здесь лишний FBO/текстура/шейдер/меш. Аналитический тест точен по
    // построению (как и у колонн) и не требует GPU-ресурсов вообще.
    GLint m_uniEnemyOccluderPosXZ = -1;
    GLint m_uniEnemyOccluderCount = -1;
    GLint m_uniEnemyOccluderRadius = -1;
    GLint m_uniEnemyOccluderHeight = -1;
    void cacheUniformLocations();

    // ---- Освещение (кэш активных факелов, см. Lighting.h) ----
    Lighting m_lighting;

    // ---- Chunked geometry for frustum/distance culling ----
    // The maze is 128x128 cells; without culling the ENTIRE mesh (walls,
    // floor, torches, win-button pedestal) is transformed and rasterized
    // every frame regardless of what the player can actually see, which
    // wastes GPU vertex-processing time and fill-rate. Geometry is instead
    // bucketed at build time into chunks (см. SceneGeometry.h — построение
    // геометрии, PVS и владение GL-буферами вынесены туда, т.к. render()
    // ниже читает их каждый кадр); at render time each chunk's AABB is
    // tested against the camera frustum and render distance, and only
    // chunks that can actually be seen are drawn.

    // ---- Item 1 (review): zero-allocation render loop ----
    // These used to be local std::vectors freshly heap-allocated and freed
    // every single call to render() (per-frame malloc/free — exactly what
    // Quake's Zone/Hunk allocators exist to avoid). They're now persistent
    // fields, sized once via reserveRenderScratchBuffers() (called from
    // init(), right after m_geometry.build()), and reused every frame via
    // .clear() + push_back(), which does not reallocate as long as
    // capacity (reserved up front) is sufficient.
    std::vector<char>          m_chunkVisible;
    std::vector<GLsizei>       m_mainCounts;
    std::vector<const GLvoid*> m_mainOffsets;
    std::vector<GLint>         m_particleFirsts;
    std::vector<GLsizei>       m_particleCounts;
    void reserveRenderScratchBuffers();

    // Переиспользуемый буфер XZ-позиций врагов на этот кадр, для
    // enemyOccluderPosXZ (см. scene.frag::shadowedByWall()) — собирается
    // заново в render() каждый кадр, но без переаллокации (см. общий
    // комментарий "zero-allocation render loop" выше).
    std::vector<glm::vec2> m_enemyOccluderScratch;

    // ---- Perf diagnostics (see getLastVisibleTriangles() etc. above) ----
    int m_lastVisibleTriangles = 0;
    int m_lastVisibleParticles = 0;
    int m_lastActiveTorchCount = 0;
    int m_lastVisibleChunkCount = 0;

    // ---- Игрок (камера/движение/коллизии/здоровье/стамина/шаги, см.
    // PlayerController.h) ----
    PlayerController m_player;

    // ---- Цветной ASCII-режим компаса (см. setColorEnabled() выше) ----
    bool m_colorEnabled = false;

    // Текущая дальность прорисовки/тумана (обычно 16.0, см. render()).
    // Пересчитывается каждый кадр в render() и используется там же для
    // uniform'а шейдера renderDistance, дальней плоскости проекции и
    // радиуса отбора активных факелов (см. render()).
    float m_currentRenderDistance = 16.0f;
    // Последние near/far, использованные в glm::perspective() (см.
    // render()) — раньше нужны были только TorchAsciiEffect'у (убран, см.
    // getNearPlane()/getFarPlane() выше), сейчас не потреблены никем.
    float m_lastNearPlane = 0.05f;
    float m_lastFarPlane = 50.0f;

    // ---- 3D compass / right arm (см. Compass.h) ----
    Compass m_compass;
    PlayerTorchViewmodel m_playerTorch;

    // ---- Враг (см. EnemyCharacter.h/SkinnedModel.h) ----
    // ОПТИМИЗАЦИЯ ПАМЯТИ (жалоба "160-190 МБ ОЗУ") — модель ("THE
    // WRAPPED") грузится СЮДА, РОВНО ОДИН РАЗ на весь процесс: раньше
    // m_testEnemy и каждый из kEnemyCount m_enemies[] грузили СВОЙ
    // собственный экземпляр SkinnedModel из одного и того же файла — 8
    // независимых копий меша/анимаций/текстуры. Теперь единственный
    // владелец GPU/CPU-ресурсов модели — этот member; m_testEnemy и
    // m_enemies[] ниже лишь ссылаются на него (см.
    // EnemyCharacter::attachSharedModel()).
    SkinnedModel m_enemySharedModel;
    GLuint m_enemyProgram = 0;
    GLint m_uEnemyView = -1, m_uEnemyProjection = -1, m_uEnemyModel = -1, m_uEnemyBoneMatrices = -1;
    GLint m_uEnemyCamPos = -1, m_uEnemyTime = -1;
    GLint m_uEnemyPlayerLightPos = -1, m_uEnemyPlayerLightDir = -1, m_uEnemyPlayerLightColor = -1, m_uEnemyPlayerLightIntensity = -1;
    GLint m_uEnemyDevLightPos = -1, m_uEnemyDevLightDir = -1, m_uEnemyDevLightCount = -1;
    GLint m_uEnemyDevPaletteOverride = -1;
    GLint m_uEnemyTorchPos = -1, m_uEnemyTorchColor = -1, m_uEnemyTorchIntensity = -1, m_uEnemyTorchCount = -1;
    GLint m_uEnemyMapTex = -1, m_uEnemyColumnPos = -1, m_uEnemyColumnCount = -1, m_uEnemyColumnRadius = -1;
    GLint m_uEnemyRenderDistance = -1;
    GLint m_uEnemyDiffuseTex = -1, m_uEnemyHasDiffuseTex = -1;
    void cacheEnemyUniformLocations();
    EnemyCharacter m_testEnemy; // dev-tools манекен (клавиша K) — см. spawnDevDummyEnemyAtPlayerView()
    EnemyAI m_enemyAI;          // AI-состояние манекена (реально не используется, только позиция/поворот/Idle)

    // ============================================================================
    // Настоящие враги (Шаг 3, см. запрос "добавить 4 врага, которые
    // появляются по всей карте в отдельных местах") — независимые
    // экземпляры, каждый со своим ИИ (m_enemyAIs[i]) и своей стартовой
    // позицией (см. spawnEnemiesAcrossMap() — по одному в каждой
    // четверти карты, подальше друг от друга и от старта игрока), но
    // ссылающиеся на ОДНУ ОБЩУЮ модель (m_enemySharedModel выше, см.
    // EnemyCharacter::attachSharedModel()) — GL-ресурсы модели ТЕПЕРЬ
    // общие (раньше каждый инстанс грузил и владел своими собственными,
    // см. историю правок и большой комментарий в EnemyCharacter.h про
    // оптимизацию памяти).
    //
    // m_testEnemy/m_enemyAI выше — ОТДЕЛЬНЫЙ, самостоятельный манекен
    // dev-tools, не входит в этот массив и не путается с ним: реальные 4
    // врага активно патрулируют/преследуют, манекен — просто стоит.
    // ============================================================================
    static constexpr int kEnemyCount = 7; // было 4 — "очень долго гулял и особо никого не встречал"
    std::array<EnemyCharacter, kEnemyCount> m_enemies;
    std::array<EnemyAI, kEnemyCount> m_enemyAIs;

    // УЛУЧШЕНИЕ ("на мини-карте — если ИГРОК увидел врага, а не
    // наоборот") — FOV-конус (примерно соответствующий реальному FOV
    // камеры рендера, см. glm::perspective(63°,...) в render()) + прямая
    // видимость (переиспользует LightBaking::HasLineOfSight, ту же
    // функцию, что и восприятие самого ИИ, просто с камерой игрока в
    // роли наблюдателя) + разумная дальность. Считается отдельно для
    // КАЖДОГО врага (см. render() — вызывается по одному разу на
    // enemyAI перед update()).
    //
    // БАГФИКС/ОПТИМИЗАЦИЯ ("заверни под тот же таймер, дёшево сделать
    // сейчас") — сама функция ниже осталась как была (честный рейкаст
    // каждый вызов), но ВЫЗЫВАЕТСЯ теперь не каждый кадр, а по тому же
    // таймеру, что и восприятие самого ИИ (kPerceptionInterval=0.2с в
    // EnemyAI.cpp) — см. m_playerVisibilityCheckTimers/
    // m_cachedPlayerCanSeeEnemy ниже и место вызова в render(). При 7
    // врагах и 60 FPS разница — 420 рейкастов/сек против 35: мини-карте
    // мгновенная реакция не нужна вообще (маркер и так гаснет плавно
    // несколько секунд), а вот раскладке при бОльшем числе врагов на
    // менее мощном железе — уже может быть заметна.
    bool isEnemyVisibleToPlayer(const glm::vec3& enemyPos) const;

    // Свой независимый таймер и закэшированный результат НА КАЖДОГО
    // врага (см. isEnemyVisibleToPlayer() выше) — кэш обязан быть
    // отдельным для каждого (у каждого свой результат), а раз кэш и так
    // отдельный, удобнее держать и таймер рядом с ним, а не городить
    // общий таймер + массив кэшей раздельно.
    std::array<float, kEnemyCount> m_playerVisibilityCheckTimers{};
    std::array<bool, kEnemyCount> m_cachedPlayerCanSeeEnemy{};

    // Раскладывает kEnemyCount врагов по отдельным непересекающимся
    // секторам сетки (не рядом друг с другом) — см. .cpp, размер сетки
    // считается от kEnemyCount автоматически (не захардкожено на "2x2
    // четверти" — это ломалось бы при kEnemyCount, для которого 4
    // ячеек мало). Вызывается один раз на каждую новую карту/новую
    // игру, после генерации геометрии (там же, где раньше стояла
    // одна-единственная точка спавна m_enemyAI).
    void spawnEnemiesAcrossMap(uint32_t seed);

    // ============================================================================
    // Dev-tools (см. src/dev/DevTools.h) — три новых инструмента по
    // отдельному запросу:
    //
    //   L — заспавнить статичный направленный "фонарик" там, где стоит
    //       игрок, светящий туда, куда он СЕЙЧАС смотрит. В отличие от
    //       обычного факела в руке, дальше не двигается и не
    //       поворачивается вместе с игроком — застывает на месте (см.
    //       devLightPos/Dir[8] в scene.frag/enemy.frag).
    //   K — заспавнить (или переставить, если уже стоит) манекена врага
    //       перед игроком, лицом к нему — стоит на месте, никакого ИИ,
    //       только Idle-анимация (см. m_devDummyEnemyActive ниже — та
    //       же модель/EnemyCharacter, что и раньше был "настоящий"
    //       враг, просто без EnemyAI::update() вообще).
    //   U — отменить последнее из двух действий выше (история — см.
    //       m_devActionHistory).
    //
    // Реализовано через уже загруженные m_testEnemy/m_enemyAI, а не
    // отдельный список независимых манекенов — не нужно парсить .glb
    // ещё раз, и рендер-цикл (m_testEnemy.draw(...) в render()) не
    // пришлось переделывать в цикл по массиву. Ограничение по дизайну:
    // манекен ОДИН — повторное нажатие K переставляет его на новое
    // место, а не добавляет второго.
    // ============================================================================

    struct DevSpotlight
    {
        glm::vec3 position;
        glm::vec3 direction; // нормализованное
    };
    // Предел 8 — совпадает с devLightPos[8]/devLightDir[8] в шейдерах;
    // spawnDevLightAtPlayerView() молча не добавляет сверх лимита.
    std::vector<DevSpotlight> m_devSpotlights;

    bool m_devDummyEnemyActive = false;

    enum class DevActionType { SpawnLight, SpawnDummyEnemy };
    struct DevAction
    {
        DevActionType type;
        // Только для SpawnDummyEnemy — было ли активно ДО этого
        // действия; undo просто возвращает как было (see
        // undoLastDevAction()) — не нужно помнить старую позицию
        // манекена отдельно, т.к. "было" всегда означает либо "его не
        // было вовсе" (false), либо "стоял, но не важно где именно"
        // (undo одного re-spawn'а не обязано восстанавливать точную
        // предыдущую позицию — это dev-инструмент, а не полноценная
        // система отмены с точной историей трансформаций).
        bool dummyWasActiveBefore = false;
    };
    std::vector<DevAction> m_devActionHistory;

    bool m_devLightKeyWasDown = false;
    bool m_devEnemyKeyWasDown = false;
    bool m_devUndoKeyWasDown = false;

    void spawnDevLightAtPlayerView();
    void spawnDevDummyEnemyAtPlayerView();
    void undoLastDevAction();

    // Перебор цветовых гамм окружения (клавиша G) — см. большой
    // комментарий у uniform devPaletteOverride в scene.frag. -1 —
    // выключено (обычная запечённая по вершинам палитра зон), 0..4 —
    // конкретная гамма из Zoning::kPaletteCount принудительно для ВСЕЙ
    // видимой геометрии сразу (глобально, без плавных переходов между
    // зонами, которые есть у обычного зонирования — это инструмент для
    // "посмотреть на всю сцену в этой гамме", а не часть игрового
    // зонирования).
    int m_devPaletteOverride = -1;
    bool m_devPaletteKeyWasDown = false;
    void cycleDevPalette();

    double m_lastEnemyUpdateTime = 0.0;
    bool m_enemyUpdateInit = false;

    // ---- Покачивание факела в руке (см. scene.vert: uViewmodelSway) ----
    // Простое сглаживание yaw ("факел отстаёт при резком повороте и
    // плавно догоняет") — чистая скалярная арифметика, без базисов/
    // cross(), см. render(). Инициализируется текущим yaw при первом
    // кадре (m_playerTorchLagInit), а не нулём, чтобы не дёргало при
    // самой первой отрисовке уровня.
    float m_playerTorchLagYaw = 0.0f;
    bool m_playerTorchLagInit = false;
    double m_playerTorchLastSwayTime = 0.0;
    // Второй этап сглаживания — см. render(): без него САМ выходной сдвиг
    // мог измениться скачком за один кадр при очень резком движении мыши
    // ("телепортируется"), даже с ограничением амплитуды (kMaxSway) —
    // клампинг ограничивает МАКСИМУМ, но не гарантирует ПЛАВНОСТЬ пути к
    // нему. Эта переменная — фактическое, гарантированно непрерывное
    // визуальное смещение; "сырая" цель (из разницы yaw) используется
    // только как ориентир, к которому она плавно стремится.
    float m_playerTorchSwayVisual = 0.0f;

    // ---- Покачивание при ходьбе/беге (см. render()) ----
    // Отдельный накопитель фазы (не тот же, что у камеры,
    // PlayerController::m_bobPhase) — так частота бега/ходьбы для
    // факела настраивается независимо, без риска дёрнуть чем-то ещё,
    // что уже завязано на камерный m_bobPhase.
    float m_playerTorchBobPhase = 0.0f;
    float m_playerTorchMoveBlend = 0.0f;

    glm::vec3 getFront() const;

    bool isWall(int x, int z) const;
    bool isFloor(int x, int z) const;
    // Срезан ли угол этой клетки стены (WallShapes::CornerCut::None, если
    // клетка обычная/не стена/вне грида) — см. m_wallCornerCuts выше.
    WallShapes::CornerCut wallCornerCut(int x, int z) const;
    // Размер среза для этой клетки (см. m_chamferSizes выше) — 0.0 для
    // клеток без данных (не должно использоваться, т.к. вызывающий код
    // сначала проверяет wallCornerCut()!=None).
    float wallChamferSize(int x, int z) const;

    // seed — см. NEW GAME / CONTINUE выше: тот же seed, переданный сюда и
    // в placeTorches() ниже, детерминированно даёт тот же самый лабиринт/
    // расстановку факелов (см. MapGenerator.h).
    void generateMap(unsigned int seed);
    void placeTorches(unsigned int seed);

    // Общая часть newGame()/loadSlot()/init(): генерирует карту заданным
    // seed'ом, грузит текстуру карты на GPU, расставляет факелы,
    // (пере)строит GL-геометрию и PVS. НЕ трогает игрока/активный слот/
    // туман войны — это по-разному решают вызывающие методы (newGame()
    // сбрасывает игрока и туман с нуля, loadSlot() восстанавливает их из
    // файла). Безопасно вызывать повторно (не только при первом init()) —
    // m_geometry.destroy() перед build() корректно освобождает уже
    // существующие GL-буферы.
    void loadMapAndGeometry(unsigned int seed);

    // Слот, к которому привязана текущая сессия (см. activeSlot() выше),
    // и seed, которым была построена текущая карта (нужен saveActiveSlot()
    // — читать его из MapGenerator::GenerateResult на каждый автосейв
    // избыточно, он не меняется, пока не вызваны newGame()/loadSlot()).
    int m_activeSlot = -1;
    unsigned int m_currentSeed = 0;

    // Имя текущего сохранения (см. saveToSlot() выше) — введённое игроком
    // вручную при явном SAVE, либо пустая строка, если сессия началась
    // через NEW GAME и ещё ни разу не была сохранена вручную (тогда
    // автосейв в newGame() тоже пишет пустое имя — экран выбора слота
    // в этом случае показывает generic "SLOT", см. Application.cpp).
    // Загрузка (loadSlot()) переносит сюда имя ИЗ файла — чтобы
    // последующие автосейвы не затирали его пустой строкой.
    std::string m_saveName;

    // Построение геометрии (addQuad/addCylinder/addSphere/addTorchMesh/
    // addWinButtonMesh/buildFloorAndWallsGreedy/build) полностью вынесено
    // в SceneGeometry — см. m_geometry.build(...) в init().

    GLuint compileShader(GLenum type, const char* src);
    GLuint linkProgram(GLuint vs, GLuint fs);

};
