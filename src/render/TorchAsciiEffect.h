#pragma once
#include <GL/glew.h>

// ============================================================================
// TorchAsciiEffect — накладывает направленные ASCII-штрихи ('|','-','/','\')
// поверх факелов, сверху уже готового ASCII-кадра игры.
//
// ИСТОРИЯ (важно для понимания, почему сделано именно так): первая версия
// была прямой адаптацией AcerolaFX_ASCII.fx — Sobel ПО ЯРКОСТИ (первая
// производная) + PNG-атласы глифов. На практике это почти не давало
// штрихов на факеле вообще: пламя само по себе гладкое (плавное
// свечение), а единственный настоящий перепад яркости — на границе
// силуэта факела с чёрным фоном — я же сам его гасил, боясь "ложных"
// краёв. Позже выяснилось (см. отдельно найденный рабочий референс), что
// более ранняя версия ЭТОГО ЖЕ движка уже решала СХОЖУЮ задачу для всей
// сцены и специально отказалась от Sobel по яркости как от ОСНОВНОГО
// сигнала именно потому, что он даёт ложные "кружки" вокруг ярких
// точечных источников света (тот же факел!) — Sobel вокруг компактного
// яркого пятна на тёмном фоне обнаруживает "край" сразу со всех сторон.
//
// Вместо этого используется КРИВИЗНА ГЛУБИНЫ (вторая производная,
// дискретный Лаплас — см. torch_ascii_edges.frag): для плоской грани,
// даже видимой под углом (линейно меняющаяся глубина), кривизна около
// нуля везде, кроме настоящих переломов геометрии — а сферическое пламя
// факела, наоборот, имеет непрерывную кривизну по всей своей видимой
// поверхности, что и даёт желаемый эффект "штриховки" всего факела, а не
// только его силуэта. Это ТОТ ЖЕ метод и даже переиспользованные формулы
// геометрии (Лаплас по 4 осям), что и в найденном рабочем референсе —
// просто примененный только к области факела (см. maskTex), а не ко всей
// сцене (там, по тем же комментариям в референсе, кривизна по плоским, но
// повёрнутым стенам всё ещё давала "лесенку" повторяющихся штрихов —
// именно поэтому в этой версии эффект ограничен ТОЛЬКО факелами).
//
// Атлас глифов — не PNG (как раньше), а тот же процедурный 8x8-битмап-
// шрифт, что генерирует и основной ASCII-рендер движка (см.
// AsciiEffect::generateFontAtlas()) — 4 фиксированных глифа, без внешних
// файлов вообще.
//
// Всего 2 прохода (было 3 — Sobel + голосование по ячейке + финальный):
// кривизна теперь считается СРАЗУ на уровне ASCII-ячейки (сам render
// target этого прохода — это разрешение окно/cellSize, один вызов
// шейдера = одна ячейка целиком), поэтому отдельного прохода-голосования
// по пикселям внутри ячейки, как раньше, больше не нужно.
// ============================================================================
class TorchAsciiEffect {
public:
    bool create();
    void destroy();

    // sceneColorTex/sceneMaskTex/sceneDepthTex — из AsciiEffect (см.
    // sceneColorTexture()/sceneTorchMaskTexture()/sceneDepthTexture()).
    // camNear/camFar — AsciiEffect::cameraNear()/cameraFar() (для
    // линеаризации depth). cellSize — тот же размер ASCII-ячейки, что и у
    // основного эффекта. colorEnabled — тот же переключатель, что и у
    // основного ASCII-рендера (Settings -> COLOR) — штрихи факела иначе
    // оставались бы цветными даже в чёрно-белом режиме.
    //
    // fadeAlpha — БАГФИКС ("штрихи факела видны поверх чёрного экрана во
    // время фейда") — этот проход раньше ничего не знал про общий
    // AsciiEffect::setFadeAlpha() основного рендера: композитится ОТДЕЛЬНО,
    // ПОСЛЕ ascii.end(), поверх уже готового (возможно, уже затемнённого)
    // кадра — так что штрихи "прорезали" даже полностью чёрный экран во
    // время смерти/фейда в меню. Теперь та же величина 0..1 передаётся и
    // сюда — см. torch_ascii_final.frag, там домножает итоговую alpha.
    void render(GLuint sceneColorTex, GLuint sceneMaskTex, GLuint sceneDepthTex,
                float camNear, float camFar,
                int windowWidth, int windowHeight, int cellSize,
                bool colorEnabled, float fadeAlpha);

private:
    void ensureSize(int windowWidth, int windowHeight, int cellSize);
    void generateEdgeFontAtlas(int cellSize);

    GLuint m_quadVAO = 0, m_quadVBO = 0;

    GLuint m_cellProgram = 0;  // torch_ascii_edges.frag — теперь сразу считает по ячейкам
    GLuint m_finalProgram = 0; // torch_ascii_final.frag

    // Разрешение (окно/cellSize) — результат torch_ascii_edges.frag.
    GLuint m_cellInfoFBO = 0;
    GLuint m_cellInfoTex = 0;

    int m_texW = 0, m_texH = 0, m_cellSize = 0;

    GLuint m_edgeFontTex = 0; // процедурный атлас 4 глифов, см. generateEdgeFontAtlas()
    static const int kEdgeGlyphCount = 4;

    // ---- Uniform locations, кэшируются один раз в create() ----
    GLint m_uCellSceneTex = -1, m_uCellMaskTex = -1, m_uCellDepthTex = -1;
    GLint m_uCellScreenRes = -1, m_uCellCellSize = -1;
    GLint m_uCellNear = -1, m_uCellFar = -1, m_uCellThreshold = -1;

    GLint m_uFinalMaskTex = -1, m_uFinalCellInfoTex = -1, m_uFinalEdgeFontTex = -1;
    GLint m_uFinalScreenRes = -1, m_uFinalCellSize = -1, m_uFinalGlyphCount = -1;
    GLint m_uFinalAsciiColor = -1, m_uFinalColorEnabled = -1;
    GLint m_uFinalFadeAlpha = -1;
};
