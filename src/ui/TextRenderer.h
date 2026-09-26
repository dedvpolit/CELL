#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// TTF text renderer (stb_truetype, VT323) for diary/journal text and credits: a screen-space
// overlay drawn with its own shader after AsciiEffect::end(), so it stays crisp instead of going
// through the ASCII post-process. The grid UI font is fixed-size (one glyph per cell) and cannot do
// readable prose; layout here uses real proportional advances (stbtt_GetBakedQuad).
class TextRenderer {
public:
    bool create();  // loads the font, bakes the atlas, compiles the shader, sets up VAO/VBO
    void destroy();
    bool isReady() const { return m_program != 0 && m_atlasTexture != 0; }

    float lineHeight(float scale) const { return m_bakedPixelHeight * 1.15f * scale; }

    // Real proportional line width in pixels. Tildes (~corrupted~ word) do not count toward it
    // (they are markup, not a displayed character); the corrupted sequence itself takes exactly the
    // width the real letters inside it would have.
    float textWidth(const std::string& markedUpText, float scale) const;

    // Line wrapping by real width (pixels, not grid columns): wrap on spaces, and a word with
    // tildes (~WORD~) wraps as one unbreakable token.
    std::vector<std::string> wrapText(const std::string& markedUpText, float maxWidthPx, float scale) const;

    void beginFrame(int screenW, int screenH);

    // Draws one already-wrapped line with its baseline at (x, baselineY) in screen pixels, (0,0) =
    // top-left. Tildes split it into normal chunks (atlas quads) and corrupted chunks (a procedural
    // ink stain of the same width whose pattern depends only on screen position, so it is stable
    // between frames).
    void drawLine(const std::string& markedUpLine, float x, float baselineY,
                  float scale, const glm::vec3& color, float alpha = 1.0f);

    // Uploads the accumulated batch into a GL_DYNAMIC_DRAW VBO and draws it in one call. Alpha
    // blending is toggled here and restored (blend state outside the call is not touched).
    void endFrame();

    // A semi-transparent rectangle (e.g. a selected row) in the same layer and pixel coordinates as
    // drawLine(), so it lines up with the text. Add it before the corresponding drawLine() so the
    // text lands on top.
    void drawRect(float x0, float y0, float x1, float y1, const glm::vec3& color, float alpha = 1.0f);

private:
    GLuint m_program = 0;
    GLuint m_atlasTexture = 0;
    int m_atlasW = 0, m_atlasH = 0;
    float m_bakedPixelHeight = 0.0f;

    // stbtt_bakedchar is opaque to the .h (stb_truetype.h is pulled in only in the .cpp, with its
    // implementation block), so it is kept as a raw byte array of the right size.
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

    // Cursor advance width for one character (in baked atlas pixels, before multiplying by scale):
    // a wrapper over stbtt_GetBakedQuad with a dummy cursor, used by textWidth()/wrapText() and
    // drawLine() internally.
    float charAdvance(char c) const;

    // originX/baselineY is the line's fixed start in pixels; localCursorX is the cursor in the
    // atlas's native pixels, carried across the chunk calls of one line (screen x = originX +
    // localCursorX * scale), so it must not reset between calls.
    void appendGlyphRun(const std::string& run, float originX, float baselineY,
                         float scale, const glm::vec3& color, float alpha,
                         float& localCursorX);

    // The same, but the chunk is "corrupted": one quad of the total size the real letters would
    // take, tagged mode = 1 (a procedural stain instead of sampling the atlas).
    void appendCorruptRun(const std::string& run, float originX, float baselineY,
                           float scale, const glm::vec3& color, float alpha,
                           float& localCursorX);
};
