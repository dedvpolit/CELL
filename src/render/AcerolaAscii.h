#pragma once
#include <GL/glew.h>

// The scene's ASCII renderer, after AcerolaFX_ASCII.fx: stroke glyphs follow contrast and
// silhouette edges (Difference of Gaussians + depth/normal edges + Sobel, voted per cell in a
// compute shader); elsewhere a fill glyph ranked by luminance. Requires OpenGL 4.3
//
// Edges are detected at scene resolution. Cells are measured in window pixels, the grid
// ascii_post.frag uses for the HUD
class AcerolaAscii {
public:
    struct FrameParams {
        float nearPlane = 0.05f;
        float farPlane = 50.0f;
        bool colorEnabled = false;
        float lensStrength = 0.0f;   // barrel distortion, 0 = off
        bool glitchActive = false;
        float glitchU = 0.5f, glitchV = 0.5f;
        float glitchRadiusCells = 4.0f;
    };

    void init();
    void shutdown();

    // Rebuilds the glyph atlas; render targets are resized lazily by run()
    void setCellSize(int cellSize);

    // Returns an RGBA8 texture with one texel per glyph cell (rgb = tint, a = glyph index / 255),
    // or 0 for an empty output (minimized window); glyphAtlas() expands it into pixels.
    // sceneColorTex must have a mip chain; nearPlane/farPlane must match its depth buffer
    GLuint run(GLuint sceneColorTex, GLuint sceneDepthTex, int sceneW, int sceneH,
               int outW, int outH, const FrameParams& params);

    // R8 atlas, glyphs of cellSize x cellSize side by side, rows stored top first
    GLuint glyphAtlas() const { return m_glyphTex; }
    // R8 edge mask at scene resolution, valid after run(); for the debug view
    GLuint edgesTexture() const { return m_edgesTex; }

    // Fraction of a cell's pixels that must agree on a direction for a stroke glyph
    // Lower values turn fine texture (brick seams) into noisy strokes
    float edgeThreshold = 0.375f;

private:
    void createTargets(int sceneW, int sceneH, int outW, int outH);
    void destroyTargets();
    void buildGlyphAtlas();

    int m_cellSize = 8;
    int m_sceneW = 0, m_sceneH = 0, m_outW = 0, m_outH = 0, m_cellsX = 0, m_cellsY = 0;
    GLuint m_quadVAO = 0, m_quadVBO = 0, m_vertexShader = 0;

    GLuint m_blurTex = 0, m_dogTex = 0, m_edgesTex = 0, m_directionTex = 0, m_outTex = 0;
    GLuint m_blurFBO = 0, m_dogFBO = 0, m_edgesFBO = 0, m_directionFBO = 0;
    GLuint m_glyphTex = 0;

    struct BlurH { GLuint prog = 0; GLint sceneTex; } m_blurH;
    struct BlurV { GLuint prog = 0; GLint blurTex, texel, tau, threshold; } m_blurV;
    struct Edges {
        GLuint prog = 0;
        GLint depthTex, sceneTex, dogTex, texel, nearPlane, farPlane, depthThreshold, normalThreshold;
    } m_edges;
    struct Sobel { GLuint prog = 0; GLint edgesTex; } m_sobel;
    struct Compute {
        GLuint prog = 0;
        GLint sceneTex, directionTex, cellSize, outSize, sceneSize, mipLevel, lensStrength;
        GLint edgeThreshold, colorEnabled, glitchActive, glitchUV, glitchRadiusCells;
    } m_comp;

    bool m_initialized = false;
};

// (meow-tick*-meow)
