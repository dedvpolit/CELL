#include "TextRenderer.h"
#include "render/AssetPath.h"
#include "render/ShaderLoader.h"
#include "render/ShaderProgram.h"

// Однозаголовочная public-domain библиотека (см. third_party/stb_truetype.h,
// авторства Sean Barrett — тот же автор, что и у stb_image.h/stb_image_write.h,
// уже используемых в проекте) — implementation-блок собран только здесь, в
// одной единице трансляции, тем же приёмом, что и WallTexture.cpp.
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <cstdio>
#include <cstddef>

bool TextRenderer::create()
{
    // ---- 1. Читаем файл шрифта целиком в память ----
    // VT323 (assets/fonts/VT323-Regular.ttf, OFL-лицензия — см.
    // assets/fonts/OFL.txt) — моноширинный "CRT-терминал", часть
    // репозитория Google Fonts. stb_truetype не умеет читать с диска
    // само, нужен сырой буфер байт всего файла (см. usage example в
    // самом stb_truetype.h).
    const std::string fullPath = AssetPath::Resolve("assets/fonts/VT323-Regular.ttf");
    if (fullPath.empty())
    {
        std::fprintf(stderr, "[TextRenderer] font not found: assets/fonts/VT323-Regular.ttf\n");
        return false;
    }

    FILE* f = std::fopen(fullPath.c_str(), "rb");
    if (!f)
    {
        std::fprintf(stderr, "[TextRenderer] failed to open font file: %s\n", fullPath.c_str());
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long fileSize = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> fontBuffer((size_t)fileSize);
    const size_t readBytes = std::fread(fontBuffer.data(), 1, (size_t)fileSize, f);
    std::fclose(f);
    if (readBytes != (size_t)fileSize)
    {
        std::fprintf(stderr, "[TextRenderer] failed to read font file: %s\n", fullPath.c_str());
        return false;
    }

    // ---- 2. Запекаем атлас (простая однопроходная упаковка построчно,
    // см. stbtt_BakeFontBitmap() в stb_truetype.h — этого достаточно для
    // одного размера/одного шрифта, полноценный stbtt_PackFontRange с
    // multi-size паковкой здесь избыточен) ----
    // Размер в пикселях — заметно больше старого сеточного UI-шрифта
    // (11px/символ, см. AsciiEffect::kMenuReferenceCellSize) — именно
    // ради управляемого размера текста и затевался переход на TTF (см.
    // обсуждение: "текст дневников нужно увеличить", старый шрифт не
    // умел ничего, кроме фиксированной 1 клетки на символ).
    m_bakedPixelHeight = 40.0f;
    m_atlasW = 512;
    m_atlasH = 512;

    std::vector<unsigned char> atlasBitmap((size_t)m_atlasW * (size_t)m_atlasH);
    m_bakedChars = new stbtt_bakedchar[kNumChars];

    const int bakeResult = stbtt_BakeFontBitmap(
        fontBuffer.data(), 0, m_bakedPixelHeight,
        atlasBitmap.data(), m_atlasW, m_atlasH,
        kFirstChar, kNumChars,
        (stbtt_bakedchar*)m_bakedChars
    );
    if (bakeResult <= 0)
    {
        // Отрицательный/нулевой результат = не все глифы влезли в атлас
        // (см. комментарий у stbtt_BakeFontBitmap в stb_truetype.h) —
        // атлас частично валиден (влезшие глифы запеклись нормально),
        // поэтому не считаем это фатальной ошибкой инициализации, только
        // предупреждаем в консоль.
        std::fprintf(stderr, "[TextRenderer] warning: font atlas may be incomplete (bakeResult=%d)\n", bakeResult);
    }

    // ---- 3. Заливаем атлас в GL-текстуру (1 канал — альфа-маска глифов,
    // читается как .r в text.frag; GL_R8, не устаревший GL_ALPHA из
    // usage-примера в stb_truetype.h, т.к. это core-профиль 3.3) ----
    glGenTextures(1, &m_atlasTexture);
    glBindTexture(GL_TEXTURE_2D, m_atlasTexture);
    // Тот же баг/фикс, что и у WallTexture.cpp: ширина строки 1 байт/px
    // не гарантированно кратна 4 (GL_UNPACK_ALIGNMENT по умолчанию),
    // хотя здесь 512 и так кратно — оставлено для единообразия и на
    // случай, если m_atlasW когда-нибудь станет не степенью двойки.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_atlasW, m_atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, atlasBitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // ---- 4. Шейдер (см. assets/shaders/text.{vert,frag}) ----
    const std::string vertSrc = ShaderLoader::LoadSource("assets/shaders/text.vert");
    const std::string fragSrc = ShaderLoader::LoadSource("assets/shaders/text.frag");
    GLuint vs = ShaderProgram::CompileShader(GL_VERTEX_SHADER, vertSrc.c_str(), "TextRenderer");
    GLuint fs = ShaderProgram::CompileShader(GL_FRAGMENT_SHADER, fragSrc.c_str(), "TextRenderer");
    m_program = ShaderProgram::LinkProgram(vs, fs, "TextRenderer");

    if (m_program)
    {
        m_uniScreenSize = glGetUniformLocation(m_program, "screenSize");
        m_uniAtlasTex   = glGetUniformLocation(m_program, "fontAtlas");
    }

    // ---- 5. Динамический VAO/VBO под батч квадов — пустой на старте,
    // реально заполняется/растится в endFrame() по мере надобности (тот
    // же buffer-growth приём, что и у остальных динамических буферов в
    // движке — см. AsciiEffect::m_uiOverlayTex рядом с этим же кодом). ----
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, alpha));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, mode));

    glBindVertexArray(0);

    return m_program != 0 && m_atlasTexture != 0;
}

void TextRenderer::destroy()
{
    if (m_vbo)           { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao)           { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_atlasTexture)  { glDeleteTextures(1, &m_atlasTexture); m_atlasTexture = 0; }
    if (m_program)       { glDeleteProgram(m_program); m_program = 0; }
    if (m_bakedChars)    { delete[] (stbtt_bakedchar*)m_bakedChars; m_bakedChars = nullptr; }
    m_vboCapacityBytes = 0;
    m_batch.clear();
}

float TextRenderer::charAdvance(char c) const
{
    if (!m_bakedChars) return 0.0f;
    const int idx = (unsigned char)c - kFirstChar;
    if (idx < 0 || idx >= kNumChars) return 0.0f;
    return ((const stbtt_bakedchar*)m_bakedChars)[idx].xadvance;
}

float TextRenderer::textWidth(const std::string& markedUpText, float scale) const
{
    float w = 0.0f;
    for (char c : markedUpText)
    {
        if (c == '~') continue; // разметка испорченного слова — не отображаемый символ
        w += charAdvance(c);
    }
    return w * scale;
}

std::vector<std::string> TextRenderer::wrapText(const std::string& raw, float maxWidthPx, float scale) const
{
    // Та же логика, что и у старой DungeonScene::WrapDiaryText() (перенос
    // по пробелам, "~WORD~" — один неразрывный токен), только ширина
    // считается настоящими пропорциональными продвижениями шрифта
    // (charAdvance()), а не фиксированными колонками сетки.
    std::vector<std::string> lines;
    std::string currentLine;
    float currentWidth = 0.0f;
    const float spaceWidth = charAdvance(' ') * scale;

    size_t pos = 0;
    while (pos <= raw.size())
    {
        const size_t spacePos = raw.find(' ', pos);
        const std::string word = raw.substr(pos, spacePos == std::string::npos ? std::string::npos : spacePos - pos);

        if (!word.empty())
        {
            const float wordWidth = textWidth(word, scale);
            const float neededWidth = (currentWidth <= 0.0f) ? wordWidth : currentWidth + spaceWidth + wordWidth;

            if (neededWidth > maxWidthPx && currentWidth > 0.0f)
            {
                lines.push_back(currentLine);
                currentLine.clear();
                currentWidth = 0.0f;
            }

            if (!currentLine.empty()) { currentLine += ' '; currentWidth += spaceWidth; }
            currentLine += word;
            currentWidth += wordWidth;
        }

        if (spacePos == std::string::npos) break;
        pos = spacePos + 1;
    }

    if (!currentLine.empty()) lines.push_back(currentLine);
    return lines;
}

void TextRenderer::beginFrame(int screenW, int screenH)
{
    m_screenW = screenW;
    m_screenH = screenH;
    m_batch.clear();
}

void TextRenderer::appendGlyphRun(const std::string& run, float originX, float baselineY,
                                   float scale, const glm::vec3& color, float alpha,
                                   float& localCursorX)
{
    if (!m_bakedChars) return;
    stbtt_bakedchar* bc = (stbtt_bakedchar*)m_bakedChars;

    for (char c : run)
    {
        const int idx = (unsigned char)c - kFirstChar;
        if (idx < 0 || idx >= kNumChars) continue; // незнакомый символ — просто пропускаем, курсор не двигаем

        float dummyY = 0.0f;
        stbtt_aligned_quad q;
        // stbtt_GetBakedQuad сама продвигает localCursorX на xadvance
        // этого символа — тот же курсор общий на всю строку (см.
        // комментарий у объявления в .h), поэтому соседние "обычный"/
        // "испорченный" куски стыкуются без наложения и без зазоров.
        stbtt_GetBakedQuad(bc, m_atlasW, m_atlasH, idx, &localCursorX, &dummyY, &q, 1);

        if (c == ' ') continue; // курсор сдвинут, рисовать нечего

        const Vertex v0{ {originX + q.x0 * scale, baselineY + q.y0 * scale}, {q.s0, q.t0}, color, alpha, 0.0f };
        const Vertex v1{ {originX + q.x1 * scale, baselineY + q.y0 * scale}, {q.s1, q.t0}, color, alpha, 0.0f };
        const Vertex v2{ {originX + q.x1 * scale, baselineY + q.y1 * scale}, {q.s1, q.t1}, color, alpha, 0.0f };
        const Vertex v3{ {originX + q.x0 * scale, baselineY + q.y1 * scale}, {q.s0, q.t1}, color, alpha, 0.0f };

        m_batch.push_back(v0); m_batch.push_back(v1); m_batch.push_back(v2);
        m_batch.push_back(v2); m_batch.push_back(v3); m_batch.push_back(v0);
    }
}

void TextRenderer::appendCorruptRun(const std::string& run, float originX, float baselineY,
                                     float scale, const glm::vec3& color, float alpha,
                                     float& localCursorX)
{
    if (!m_bakedChars || run.empty()) return;
    const stbtt_bakedchar* bc = (const stbtt_bakedchar*)m_bakedChars;

    // Не рисуем реальные буквы вообще — только считаем, сколько места
    // они бы заняли (те же xadvance, что и в appendGlyphRun выше), и
    // рисуем один квад той ширины: процедурное "чернильное пятно" вместо
    // текста (см. text.frag), а не читаемые символы.
    const float startLocal = localCursorX;
    for (char c : run)
    {
        const int idx = (unsigned char)c - kFirstChar;
        if (idx < 0 || idx >= kNumChars) continue;
        localCursorX += bc[idx].xadvance;
    }
    const float endLocal = localCursorX;
    if (endLocal <= startLocal) return;

    const float x0 = originX + startLocal * scale;
    const float x1 = originX + endLocal * scale;
    // Приближённые ascent/descent относительно baseline — реальных
    // метрик шрифта здесь не нужно, пятно не обязано пиксель-в-пиксель
    // совпадать с высотой букв, только читаться "на месте слова".
    const float y0 = baselineY - 0.80f * m_bakedPixelHeight * scale;
    const float y1 = baselineY + 0.22f * m_bakedPixelHeight * scale;

    const glm::vec3 blotColor = color * 0.6f; // темнее обычного текста — пятно, не замена букв тем же тоном

    const Vertex v0{ {x0, y0}, {0.0f, 0.0f}, blotColor, alpha, 1.0f };
    const Vertex v1{ {x1, y0}, {0.0f, 0.0f}, blotColor, alpha, 1.0f };
    const Vertex v2{ {x1, y1}, {0.0f, 0.0f}, blotColor, alpha, 1.0f };
    const Vertex v3{ {x0, y1}, {0.0f, 0.0f}, blotColor, alpha, 1.0f };

    m_batch.push_back(v0); m_batch.push_back(v1); m_batch.push_back(v2);
    m_batch.push_back(v2); m_batch.push_back(v3); m_batch.push_back(v0);
}

void TextRenderer::drawLine(const std::string& markedUpLine, float x, float baselineY,
                             float scale, const glm::vec3& color, float alpha)
{
    if (!isReady()) return;

    float localCursorX = 0.0f;
    bool corrupted = false;
    std::string run;

    // Разбираем ~тильды~ на лету, стыкуя "обычные"/"испорченные" куски
    // одним общим курсором (localCursorX) — см. комментарий у
    // appendGlyphRun() в .h.
    for (char c : markedUpLine)
    {
        if (c == '~')
        {
            if (!run.empty())
            {
                if (corrupted) appendCorruptRun(run, x, baselineY, scale, color, alpha, localCursorX);
                else           appendGlyphRun(run, x, baselineY, scale, color, alpha, localCursorX);
                run.clear();
            }
            corrupted = !corrupted;
            continue;
        }
        run += c;
    }

    if (!run.empty())
    {
        if (corrupted) appendCorruptRun(run, x, baselineY, scale, color, alpha, localCursorX);
        else           appendGlyphRun(run, x, baselineY, scale, color, alpha, localCursorX);
    }
}

void TextRenderer::drawRect(float x0, float y0, float x1, float y1, const glm::vec3& color, float alpha)
{
    const Vertex v0{ {x0, y0}, {0.0f, 0.0f}, color, alpha, 2.0f };
    const Vertex v1{ {x1, y0}, {0.0f, 0.0f}, color, alpha, 2.0f };
    const Vertex v2{ {x1, y1}, {0.0f, 0.0f}, color, alpha, 2.0f };
    const Vertex v3{ {x0, y1}, {0.0f, 0.0f}, color, alpha, 2.0f };
    m_batch.push_back(v0); m_batch.push_back(v1); m_batch.push_back(v2);
    m_batch.push_back(v2); m_batch.push_back(v3); m_batch.push_back(v0);
}

void TextRenderer::endFrame()
{
    if (!isReady() || m_batch.empty()) return;

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    const GLsizeiptr neededBytes = (GLsizeiptr)(m_batch.size() * sizeof(Vertex));
    if (neededBytes > m_vboCapacityBytes)
    {
        // Растим буфер с запасом (x1.5), а не ровно под текущий кадр —
        // чтобы не перевыделять VBO каждый раз, когда следующий открытый
        // дневник чуть длиннее предыдущего.
        m_vboCapacityBytes = (GLsizeiptr)((double)neededBytes * 1.5);
        glBufferData(GL_ARRAY_BUFFER, m_vboCapacityBytes, nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, neededBytes, m_batch.data());

    glUseProgram(m_program);
    if (m_uniScreenSize >= 0) glUniform2f(m_uniScreenSize, (float)m_screenW, (float)m_screenH);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_atlasTexture);
    if (m_uniAtlasTex >= 0) glUniform1i(m_uniAtlasTex, 0);

    // Текст — плоский экранный оверлей поверх уже готового кадра (тот же
    // принцип, что и у Compass, см. большой комментарий в TextRenderer.h)
    // — глубина ему не нужна и не должна ни от чего зависеть.
    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_batch.size());

    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);

    glBindVertexArray(0);
    m_batch.clear();
}
