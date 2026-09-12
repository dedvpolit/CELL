#pragma once
#include <GL/glew.h>
#include <vector>
#include <chrono>

// [comment corrupted in source file - original text lost/unrecoverable]
// [comment corrupted in source file - original text lost/unrecoverable]
//   AsciiEffect fx;
// [comment corrupted in source file - original text lost/unrecoverable]
// [comment corrupted in source file - original text lost/unrecoverable]
//   fx.begin();
//     renderYourDungeonScene();
//   fx.end(windowWidth, windowHeight);
class AsciiEffect {
public:
    bool init(int sceneWidth, int sceneHeight, int cellSize = 8);
    void shutdown();

    void begin();  // [comment corrupted in source file - original text lost/unrecoverable]
    void end(int windowWidth, int windowHeight);  // [comment corrupted in source file - original text lost/unrecoverable]

    void resize(int sceneWidth, int sceneHeight);  // [comment corrupted in source file - original text lost/unrecoverable]

    // Для DungeonScene noclip-инструментов/дебага: тот же кадр сцены (до
    // ASCII-постпроцесса) и его реальный размер.
    GLuint sceneColorTexture() const { return m_sceneTex; }
    int sceneWidth() const { return m_fboW; }
    int sceneHeight() const { return m_fboH; }

    // Ближняя/дальняя плоскости проекции сцены — были нужны только
    // TorchAsciiEffect'у (линеаризация depth-текстуры для детекта краёв
    // факела по кривизне глубины); эффект убран (см. историю правок,
    // жалоба "игра жрёт слишком много ОЗУ"), вместе с ним ушли и эти
    // сеттеры/геттеры — больше никто их не читал.

    // [comment corrupted in source file - original text lost/unrecoverable]
    // [comment corrupted in source file - original text lost/unrecoverable]
    // [comment corrupted in source file - original text lost/unrecoverable]
    // [comment corrupted in source file - original text lost/unrecoverable]
    // [comment corrupted in source file - original text lost/unrecoverable]
    // [comment corrupted in source file - original text lost/unrecoverable]
    // [comment corrupted in source file - original text lost/unrecoverable]
    void setMinimap(GLuint dataTex, int gridSize, float playerYawDegrees);
    GLuint getMinimapFontTexture() const
    {
        return m_minimapFontTex;
    }

    int getMinimapGlyphCount() const
    {
        return m_minimapGlyphCount;
    }

    // Полоса энергии/стамины внизу экрана, из "#" символов в рамке.
    // fraction01 — доля заполнения (0..1, зажимается в шейдере).
    // enabled=false плавно (не мгновенно) прячет полосу — см. m_staminaAlpha,
    // затухание считается по реальному времени внутри end().
    // Вызывать каждый кадр между begin()/end() (значение сохраняется
    // до следующего вызова, как и у setMinimap()).
    void setStamina(float fraction01, bool enabled = true);

    // Доля здоровья игрока (0..1), см. DungeonScene::getHealthFraction().
    // Управляет тем, насколько сильно "каплет" рамка полосы стамины —
    // 1.0 = ровная рамка, 0.0 = каплет по всему периметру.
    // Вызывать каждый кадр вместе с setStamina().
    void setHealth(float fraction01);

    // "Кинематографичный" режим для трейлерных кадров — на практике
    // вызывается вместе с noclip (см. DungeonScene::isNoclipEnabled()).
    // cinematicCellSize — размер ASCII-ячейки в этом режиме (обычно
    // меньше обычного, см. DevTools.h: kCinematicCellSize — константа
    // специально вынесена туда, чтобы весь дев-тюнинг жил в одном месте).
    // Перегенерирует оба атласа шрифтов под новый размер — дорогая
    // операция (пересоздание текстур), вызывать только на смене режима
    // (true/false), а не каждый кадр; повторный вызов с тем же
    // enabled/cinematicCellSize ничего не делает.
    void setCinematicMode(bool enabled, int cinematicCellSize = 6);

    // ---- Настройка "SHARPNESS" в SETTINGS (клавиши мыши/слайдер, см.
    // MenuLayouts::BuildSettingsMenu()/Application.cpp) — размер ASCII-
    // ячейки в обычном (не кинематографичном) режиме. Меньше значение =
    // ячейка мельче = символов на экране больше = картинка "чётче"
    // (больше деталей, мельче текст); больше значение = крупнее символы,
    // сильнее ASCII-эффект.
    //
    // Диапазон подобран вручную: kMinCellSize — заметно мельче дефолта
    // (11), но ещё разборчиво; kMaxCellSize — крупно, но ASCII-сетка
    // всё ещё остаётся играбельной (не превращается в 3-4 гигантских
    // символа на весь экран).
    static constexpr int kMinCellSize = 6;
    static constexpr int kMaxCellSize = 20;

    // Если сейчас идёт кинематографичный режим (см. setCinematicMode()),
    // применяется НЕ сразу — только запоминается и вступает в силу, как
    // только кинематографичный режим выключится (тот же принцип, что и
    // у m_normalCellSize там: "куда вернуться"). Перегенерирует атласы
    // шрифтов при фактическом изменении — как и setCinematicMode(),
    // дорогая операция, вызывать на отпускание слайдера/явное действие
    // пользователя, а не каждый кадр во время перетаскивания (см.
    // Application.cpp — там дискретизация с шагом в 1, а не сплошной
    // поток вызовов на каждый пиксель мыши).
    void setUserCellSize(int cellSize);
    int getUserCellSize() const { return m_normalCellSize; }

    // ---- UI-текст поверх всего (заголовок меню, кнопки и т.п.) ----
    // grid — glyph-индексы по клеткам экрана (0 = пусто/прозрачно, иначе
    // glyphIndex+1, см. s_minimapGlyphs[] в .cpp), размер cols*rows.
    // cols/rows ДОЛЖНЫ совпадать с getGridCols()/getGridRows() на момент
    // вызова — иначе текст съедет/обрежется. enabled=false отключает
    // прорисовку независимо от содержимого grid (сцена рисуется как
    // обычно, без надписей).
    void setUIOverlay(bool enabled, const std::vector<unsigned char>& grid, int cols, int rows);

    // Текущий размер ASCII-сетки (символьных колонок/строк) для текущего
    // разрешения сцены (m_fboW/m_fboH) и cellSize — используется, чтобы
    // построить grid для setUIOverlay() с координатами, которые совпадут
    // с шейдером. Меняется при resize()/setCinematicMode().
    //
    // ВАЖНО: это сетка для ВНУТРЕННЕЙ FBO-сцены, а НЕ для окна — шейдер
    // же считает totalCols/totalRows от screenResolution (реального
    // окна, см. AsciiEffect::end()), потому что финальный кадр просто
    // растягивается на весь экран. При полноэкранном режиме/ресайзе окна
    // эти два числа расходятся, и текст меню (использующий эти геттеры)
    // "уезжает" — для UI-оверлея нужно считать сетку от РЕАЛЬНОГО окна,
    // см. getGridColsForWindow()/getGridRowsForWindow() ниже.
    int getGridCols() const { return m_cellSize > 0 ? m_fboW / m_cellSize : 0; }
    int getGridRows() const { return m_cellSize > 0 ? m_fboH / m_cellSize : 0; }

    // То же самое, но от РЕАЛЬНОГО размера окна (windowWidth/Height,
    // как передаются в end()) — именно это использует шейдер для
    // totalCols/totalRows, поэтому setUIOverlay() должен строить свою
    // сетку именно под эти числа, а не под getGridCols()/getGridRows().
    int getGridColsForWindow(int windowWidth) const { return m_cellSize > 0 ? windowWidth / m_cellSize : 0; }
    int getGridRowsForWindow(int windowHeight) const { return m_cellSize > 0 ? windowHeight / m_cellSize : 0; }

    // БАГФИКС ("sharpness ломает пауза/главное меню — иконки уезжают за
    // экран") — раньше ВСЕ экраны меню (главное/пауза/настройки/слоты
    // сохранений) строились через getGridColsForWindow()/
    // getGridRowsForWindow() выше — то есть на ЖИВОМ m_cellSize, том же,
    // которым теперь управляет слайдер SHARPNESS. При крупной клетке
    // (например 20px) итоговых колонок на экране может оказаться МЕНЬШЕ,
    // чем нужно всего одному слову "SENSITIVITY" на минимально возможном
    // размере глифа (5 клеток на букву — дальше мельчить эта шрифтовая
    // система физически не умеет, см. BigFont.cpp::ComputeFinalRes()) —
    // в таких случаях меню математически не может поместиться ни при
    // какой раскладке, при любых доработках формул отступов.
    //
    // Единственное общее решение — полностью РАЗВЯЗАТЬ раскладку меню от
    // живого cellSize: меню всегда строится и кликается в СВОЕЙ,
    // ФИКСИРОВАННОЙ сетке (kMenuReferenceCellSize — то самое историческое
    // значение по умолчанию, 11, на котором вся раскладка изначально
    // подбиралась и проверялась), а на экран накладывается через UV
    // (см. ascii_post.frag — там uiCols/uiRows теперь не привязаны к
    // totalCols/totalRows живой сцены). Из-за этого сам текст меню
    // (SETTINGS/PAUSE/MAIN) не меняет "зернистость" вместе с SHARPNESS —
    // это осознанный компромисс: экраны меню гарантированно НИКОГДА не
    // ломаются, независимо от выбранного размера клетки, а сам SHARPNESS
    // остаётся тем, чем и задумывался — настройкой ЧЁТКОСТИ ИГРОВОЙ
    // СЦЕНЫ (подземелье/факелы/мини-карта), а не двух настроек в одной.
    static constexpr int kMenuReferenceCellSize = 11;
    int getMenuGridColsForWindow(int windowWidth) const { return windowWidth / kMenuReferenceCellSize; }
    int getMenuGridRowsForWindow(int windowHeight) const { return windowHeight / kMenuReferenceCellSize; }

    // Текущий размер ASCII-ячейки в пикселях (см. m_cellSize) — нужен,
    // чтобы перевести координаты курсора мыши (реальные пиксели окна) в
    // те же символьные клетки, которыми оперирует MainMenu.h/шейдер.
    int getCellSize() const { return m_cellSize; }

    // Плавное затемнение всего экрана (0 = не затемнено, 1 = полностью
    // чёрный) — переход между стартовым меню и игрой (см. main.cpp).
    // Применяется поверх абсолютно всего (сцена, UI-текст, мини-карта,
    // полоса стамины) в самом шейдере, не отдельным draw call'ом.
    // Значение не сглаживается здесь само по себе — ожидается, что
    // вызывающий код (main.cpp) уже передаёт плавно интерполированное
    // значение, обновляемое каждый кадр по deltaTime.
    void setFadeAlpha(float alpha01);

    // Включает/выключает цветной режим ASCII-рендера (см. Settings ->
    // COLOR в MainMenu.h / main.cpp). Влияет ТОЛЬКО на цвет символов —
    // сама сцена по-прежнему рисуется исключительно ASCII-глифами,
    // никаких растровых элементов не добавляется. Выключено по
    // умолчанию (классический чёрно-белый режим, как было раньше).
    void setColorEnabled(bool enabled) { m_colorEnabled = enabled; }
    bool isColorEnabled() const { return m_colorEnabled; }

    // Едва заметная линзовая дисторсия + круглая виньетка по краям
    // экрана (см. assets/shaders/ascii_post.frag). Настройки -> LENS.
    void setLensEffectEnabled(bool enabled) { m_lensEffectEnabled = enabled; }
    bool isLensEffectEnabled() const { return m_lensEffectEnabled; }

    // УЛУЧШЕНИЕ ("капли крови по экрану, снова и снова") — этому шейдеру
    // раньше вообще не был нужен уиTime: единственный анимационный эффект
    // до этого (капли на рамке полосы стамины) намеренно СТАТИЧЕН —
    // "не завися от кадра/времени, иначе капли бы дёргались" (см.
    // ascii_post.frag). Полноэкранные капли по запросу должны реально
    // ДВИГАТЬСЯ (стекать вниз), поэтому здесь впервые понадобилось само
    // время — секунды с момента старта приложения (Application передаёт
    // glfwGetTime(), тот же источник, что и везде в игре).
    void setTime(float seconds) { m_time = seconds; }

private:
    GLuint m_fbo = 0;
    GLuint m_sceneTex = 0;
    // Настоящая depth-ТЕКСТУРА (не renderbuffer) — оставлена текстурой
    // (не renderbuffer) по историческим причинам (раньше её сэмплировал
    // TorchAsciiEffect для детекта краёв факела по кривизне глубины,
    // теперь этот эффект убран) — но сама по себе всё ещё нужна как
    // обычный depth-attachment для 3D-прохода отрисовки сцены.
    GLuint m_depthTex = 0;
    GLuint m_fontTex = 0;
    GLuint m_program = 0;
    GLuint m_quadVAO = 0, m_quadVBO = 0;

    int m_fboW = 0, m_fboH = 0;
    int m_cellSize = 8;
    int m_rampLength = 0;  // [comment corrupted in source file - original text lost/unrecoverable]

    // [comment corrupted in source file - original text lost/unrecoverable]
    // [comment corrupted in source file - original text lost/unrecoverable]
    // [comment corrupted in source file - original text lost/unrecoverable]
    GLuint m_minimapFontTex = 0;
    int m_minimapGlyphCount = 0;

    GLuint m_minimapDataTex = 0;  // [comment corrupted in source file - original text lost/unrecoverable]
    int    m_minimapGridSize = 0;  // [comment corrupted in source file - original text lost/unrecoverable]
    float  m_minimapYawDeg = 0.0f;  // [comment corrupted in source file - original text lost/unrecoverable]

    // [comment corrupted in source file - original text lost/unrecoverable]
    // [comment corrupted in source file - original text lost/unrecoverable]
    float m_staminaFrac         = 1.0f;
    bool  m_staminaEnabledTarget = false;  // [comment corrupted in source file - original text lost/unrecoverable]
    float m_staminaAlpha        = 0.0f;  // [comment corrupted in source file - original text lost/unrecoverable]
    bool  m_staminaFadeTimeValid = false;
    std::chrono::steady_clock::time_point m_staminaFadeLastTime;
    float m_healthFrac    = 1.0f;

    // ---- Кинематографичный режим (см. setCinematicMode()) ----
    bool m_cinematicMode  = false;
    int  m_normalCellSize = 8; // запоминается в init(), чтобы было куда вернуться

    // ---- UI-текст поверх всего (см. setUIOverlay()) ----
    GLuint m_uiOverlayTex = 0;
    bool   m_uiOverlayEnabled = false;
    int    m_uiOverlayCols = 0, m_uiOverlayRows = 0;

    // ---- Затемнение экрана (см. setFadeAlpha()) ----
    float m_fadeAlpha = 0.0f;

    // ---- Цветной режим (см. setColorEnabled()) ----
    bool m_colorEnabled = false;

    // ---- Линза (см. setLensEffectEnabled()) ----
    bool m_lensEffectEnabled = false;
    float m_time = 0.0f;

    void createFBO(int w, int h);
    void destroyFBO();
    void generateFontAtlas();
    void generateMinimapFontAtlas();
    void createQuad();
    // Компиляция/линковка шейдеров вынесена в общий render/ShaderProgram
    // (см. init()) — раньше здесь были собственные compileShader()/linkProgram(),
    // дублирующие идентичный код из DungeonScene и компаса.
    // ---- Cached uniform locations for m_program (see end()) ----
    // Same rationale as DungeonScene: glGetUniformLocation() was being
    // called ~15 times every frame for names that never change after the
    // program is linked. Resolved once in init() (cacheUniformLocations()).
    GLint m_uniSceneTex = -1;
    GLint m_uniFontTex = -1;
    GLint m_uniScreenResolution = -1;
    GLint m_uniCellSize = -1;
    GLint m_uniRampLength = -1;
    GLint m_uniMinimapFontTex = -1;
    GLint m_uniMinimapGlyphCount = -1;
    GLint m_uniMinimapTex = -1;
    GLint m_uniMinimapGridSize = -1;
    GLint m_uniMinimapYawDeg = -1;
    GLint m_uniStaminaFrac = -1;
    GLint m_uniStaminaAlpha = -1;
    GLint m_uniHealthFrac = -1;
    GLint m_uniUiTex = -1;
    GLint m_uniUiCols = -1;
    GLint m_uniUiRows = -1;
    GLint m_uniUiEnabled = -1;
    GLint m_uniFadeAlpha = -1;
    GLint m_uniColorEnabled = -1;
    GLint m_uniLensEffectEnabled = -1;
    GLint m_uniTime = -1;
    void cacheUniformLocations();

    // ---- Item 5: avoid realloc'ing the UI overlay texture every call ----
    // setUIOverlay() is called every frame while a menu/pause screen is
    // showing (the CELL title animation rebuilds its glyph grid every
    // frame). The texture's cols/rows don't change between those calls
    // (only its content does), so once it's allocated at a given size we
    // just overwrite it in place instead of reallocating GPU storage.
    int m_uiOverlayTexW = 0;
    int m_uiOverlayTexH = 0;

};
