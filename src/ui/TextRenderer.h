#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// TTF text (stb_truetype, VT323) for diaries, journal and credits, drawn after AsciiEffect::end() so it stays crisp
class TextRenderer {
public:
    bool create();  // loads the font, bakes the atlas, compiles the shader, sets up VAO/VBO
    void destroy();
    bool isReady() const { return m_program != 0 && m_atlasTexture != 0; }

    float lineHeight(float scale) const { return m_bakedPixelHeight * 1.15f * scale; }

    // Width in pixels; tildes are markup and do not count
    float textWidth(const std::string& markedUpText, float scale) const;

    // Line wrapping by real width (pixels, not grid columns):
    // wrap on spaces, and a word with tildes (~WORD~) wraps as one unbreakable token
    std::vector<std::string> wrapText(const std::string& markedUpText, float maxWidthPx, float scale) const;

    void beginFrame(int screenW, int screenH);

    // Draws one wrapped line with its baseline at (x, baselineY), origin top-left
    // Tildes delimit corrupted runs drawn as ink stains
    void drawLine(const std::string& markedUpLine, float x, float baselineY,
                  float scale, const glm::vec3& color, float alpha = 1.0f);

    // Uploads the batch and draws it in one call; blend state is restored
    void endFrame();

    // Rectangle in the text layer's coordinates; add it before the line so the text draws on top
    void drawRect(float x0, float y0, float x1, float y1, const glm::vec3& color, float alpha = 1.0f);

private:
    GLuint m_program = 0;
    GLuint m_atlasTexture = 0;
    int m_atlasW = 0, m_atlasH = 0;
    float m_bakedPixelHeight = 0.0f;

    // Opaque storage for stbtt_bakedchar; stb_truetype.h is included only in the .cpp
    static constexpr int kFirstChar = 32;  // ' '
    static constexpr int kNumChars = 95;   // 32..126 inclusive
    void* m_bakedChars = nullptr;          // stbtt_bakedchar[kNumChars], see the .cpp

    GLuint m_vao = 0, m_vbo = 0;
    GLsizei m_vboCapacityBytes = 0;

    struct Vertex { glm::vec2 pos; glm::vec2 uv; glm::vec3 color; float alpha; float mode; };
    std::vector<Vertex> m_batch;

    int m_screenW = 0, m_screenH = 0;

    GLint m_uniScreenSize = -1;
    GLint m_uniAtlasTex = -1;

    // Advance of one character in atlas pixels
    float charAdvance(char c) const;

    // localCursorX is carried across the chunks of one line
    void appendGlyphRun(const std::string& run, float originX, float baselineY,
                         float scale, const glm::vec3& color, float alpha,
                         float& localCursorX);

    // Corrupted chunk: one quad spanning the letters' width
    void appendCorruptRun(const std::string& run, float originX, float baselineY,
                           float scale, const glm::vec3& color, float alpha,
                           float& localCursorX);
};
