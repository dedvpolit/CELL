#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// ============================================================================
// TextRenderer — настоящий TTF-рендер текста (stb_truetype, шрифт VT323 —
// моноширинный "CRT-терминал", OFL-лицензия, см. assets/fonts/OFL.txt) для
// текста дневников/журнала. Экранный 2D-оверлей, рисуется отдельным
// шейдером (assets/shaders/text.{vert,frag}) ПОСЛЕ AsciiEffect::end() — тот
// же принцип, что и у Compass (см. Compass.h), не проходит через основной
// ASCII-постпроцесс сцены, поэтому остаётся чётким/читаемым.
//
// ПОЧЕМУ это отдельный слой, а не расширение старого сеточного UI-шрифта
// (AsciiEffect.cpp::s_minimapGlyphs) — тот шрифт фиксирован по размеру
// (1 символ = 1 клетка сетки, 11 экранных пикселей, см.
// AsciiEffect::kMenuReferenceCellSize) и не имеет понятия "размер шрифта"
// вообще. Он ОСТАЁТСЯ на месте и продолжает рисовать рамки/капли крови/
// подсказки ("E CLOSE" и т.п.) — TextRenderer подключается ТОЛЬКО для
// самого читаемого текста (проза дневника, "LOG", подписи записей
// журнала), где реально важен управляемый размер шрифта.
//
// Раскладка — НАСТОЯЩАЯ пропорциональная (реальные ширины букв из шрифта,
// через stbtt_GetBakedQuad), не фиксированная сетка колонок, как у старого
// шрифта.
// ============================================================================
class TextRenderer {
public:
    bool create();  // грузит шрифт, запекает атлас, компилирует шейдер, заводит VAO/VBO
    void destroy();
    bool isReady() const { return m_program != 0 && m_atlasTexture != 0; }

    // Высота строки (baseline-to-baseline) в пикселях при данном scale —
    // 1.0 = запечённый размер атласа (см. kBakedPixelHeight в .cpp).
    float lineHeight(float scale) const { return m_bakedPixelHeight * 1.15f * scale; }

    // Реальная пропорциональная ширина строки в пикселях — тильды
    // (~испорченное~ слово, см. Diaries.h) НЕ считаются (это разметка,
    // не отображаемый символ), сама испорченная последовательность
    // занимает ровно ту же ширину, что заняли бы настоящие буквы внутри.
    float textWidth(const std::string& markedUpText, float scale) const;

    // Перенос строк по реальной ширине (в пикселях, не в колонках сетки)
    // — та же логика, что была у DungeonScene::WrapDiaryText(), только
    // на пропорциональных ширинах вместо фиксированных колонок. "Слово"
    // с тильдами (~WORD~) переносится как единый неразрывный токен.
    std::vector<std::string> wrapText(const std::string& markedUpText, float maxWidthPx, float scale) const;

    // Сбрасывает батч квадов на новый кадр — вызывается один раз перед
    // серией drawLine(), before endFrame().
    void beginFrame(int screenW, int screenH);

    // Рисует ОДНУ строку (уже перенесённую, см. wrapText()) с базовой
    // линией в (x,baselineY), пиксели экрана, (0,0) = верхний левый угол.
    // Тильды разбирают строку на "обычные"/"испорченные" куски на лету:
    // обычные — настоящие глиф-квады из атласа; испорченные — то же
    // место на экране, но процедурное "чернильное пятно" (см. text.frag)
    // вместо реальных букв. Узор пятна зависит только от экранной
    // позиции (не от времени/кадра) — стабилен между кадрами сам по
    // себе, отдельный seed не нужен.
    void drawLine(const std::string& markedUpLine, float x, float baselineY,
                  float scale, const glm::vec3& color, float alpha = 1.0f);

    // Заливает накопленный батч в GL_DYNAMIC_DRAW VBO и рисует одним
    // draw call. Alpha-blending включается/выключается здесь же (не
    // трогает состояние блендинга вне вызова).
    void endFrame();

    // Сплошной полупрозрачный прямоугольник (подсветка выбранной строки
    // списка и т.п.) — ЭТИМ ЖЕ слоем и в ТЕХ ЖЕ пиксельных координатах,
    // что и drawLine() выше, чтобы подсветка гарантированно совпадала с
    // положением текста поверх неё (раньше подсветка рисовалась старым
    // сеточным слоем независимо от пиксельной раскладки TextRenderer —
    // два независимых вычисления одной и той же позиции неизбежно
    // расходились). Добавляется в батч ДО соответствующего drawLine(),
    // чтобы текст лёг поверх заливки, а не под ней.
    void drawRect(float x0, float y0, float x1, float y1, const glm::vec3& color, float alpha = 1.0f);

private:
    GLuint m_program = 0;
    GLuint m_atlasTexture = 0;
    int m_atlasW = 0, m_atlasH = 0;
    float m_bakedPixelHeight = 0.0f;

    // stbtt_bakedchar — непрозрачно для .h (не тянуть stb_truetype.h
    // сюда, он подключается только в .cpp, implementation-блок собран
    // там же, как и STB_IMAGE_IMPLEMENTATION у WallTexture.cpp), поэтому
    // держим как сырой массив байт нужного размера.
    static constexpr int kFirstChar = 32;  // ' '
    static constexpr int kNumChars = 95;   // 32..126 включительно
    void* m_bakedChars = nullptr;          // stbtt_bakedchar[kNumChars], см. .cpp

    GLuint m_vao = 0, m_vbo = 0;
    GLsizei m_vboCapacityBytes = 0;

    struct Vertex { glm::vec2 pos; glm::vec2 uv; glm::vec3 color; float alpha; float mode; };
    std::vector<Vertex> m_batch;

    int m_screenW = 0, m_screenH = 0;

    GLint m_uniScreenSize = -1;
    GLint m_uniAtlasTex = -1;

    // Ширина/продвижение курсора для ОДНОГО символа (в "запечённых"
    // пикселях атласа, до умножения на scale) — обёртка над
    // stbtt_GetBakedQuad с фиктивным курсором, используется и
    // textWidth()/wrapText(), и drawLine() внутри.
    float charAdvance(char c) const;

    // originX/baselineY — фиксированная точка старта строки на экране
    // (пиксели). localCursorX — курсор в "родных" пикселях шрифта
    // (масштаб атласа при запекании, ДО умножения на scale), общий на
    // всю строку и продолжающийся через несколько вызовов подряд (один
    // на "обычный" кусок, другой на "испорченный") — экранная позиция
    // каждого квада считается как originX + localCursorX*scale, поэтому
    // курсор обязан быть один и тот же для всех кусков строки, а не
    // сбрасываться на каждом вызове.
    void appendGlyphRun(const std::string& run, float originX, float baselineY,
                         float scale, const glm::vec3& color, float alpha,
                         float& localCursorX);

    // То же самое, но кусок "испорчен" — один квад того же суммарного
    // размера, что заняли бы настоящие буквы, помечен mode=1 (см.
    // text.frag — процедурное пятно вместо сэмпла атласа).
    void appendCorruptRun(const std::string& run, float originX, float baselineY,
                           float scale, const glm::vec3& color, float alpha,
                           float& localCursorX);
};
