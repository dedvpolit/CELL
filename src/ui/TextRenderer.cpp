#include "TextRenderer.h"
#include "render/AssetPath.h"
#include "render/ShaderLoader.h"
#include "render/ShaderProgram.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <cstdio>
#include <cstddef>

bool TextRenderer::create()
{
    // 1. Read the font file (VT323); stb_truetype needs the whole file in memory.
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

    // 2. Bake the atlas with stbtt_BakeFontBitmap: one size, one font.
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
        // Not every glyph fit; the ones that did are usable.
        std::fprintf(stderr, "[TextRenderer] warning: font atlas may be incomplete (bakeResult=%d)\n", bakeResult);
    }

    // 3. Upload as a single-channel GL_R8 mask.
    glGenTextures(1, &m_atlasTexture);
    glBindTexture(GL_TEXTURE_2D, m_atlasTexture);
    // One byte per pixel: rows need not be 4-byte aligned.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_atlasW, m_atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, atlasBitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

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
        if (c == '~') continue; // corrupted-word markup, not a displayed character
        w += charAdvance(c);
    }
    return w * scale;
}

std::vector<std::string> TextRenderer::wrapText(const std::string& raw, float maxWidthPx, float scale) const
{
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
        if (idx < 0 || idx >= kNumChars) continue; // unknown character: skip it, cursor doesn't move

        float dummyY = 0.0f;
        stbtt_aligned_quad q;
        // The cursor is shared across chunks, so normal and corrupted runs join seamlessly.
        stbtt_GetBakedQuad(bc, m_atlasW, m_atlasH, idx, &localCursorX, &dummyY, &q, 1);

        if (c == ' ') continue; // cursor advanced, nothing to draw

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

    // Advance by the letters' widths and draw one stain quad over the span.
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
    // Approximate ascent and descent; the stain only needs to read as a word.
    const float y0 = baselineY - 0.80f * m_bakedPixelHeight * scale;
    const float y1 = baselineY + 0.22f * m_bakedPixelHeight * scale;

    const glm::vec3 blotColor = color * 0.6f;

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
        // Grow with 1.5x headroom to avoid reallocating for every longer page.
        m_vboCapacityBytes = (GLsizeiptr)((double)neededBytes * 1.5);
        glBufferData(GL_ARRAY_BUFFER, m_vboCapacityBytes, nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, neededBytes, m_batch.data());

    glUseProgram(m_program);
    if (m_uniScreenSize >= 0) glUniform2f(m_uniScreenSize, (float)m_screenW, (float)m_screenH);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_atlasTexture);
    if (m_uniAtlasTex >= 0) glUniform1i(m_uniAtlasTex, 0);

    // Text is a flat screen overlay on top of the already finished frame (like Compass): no depth
    // is needed, and it must not depend on any depth state.
    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_batch.size());

    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
    if (!blendWasEnabled) glDisable(GL_BLEND);

    glBindVertexArray(0);
    m_batch.clear();
}
