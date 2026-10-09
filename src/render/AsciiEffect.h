#pragma once
#include <GL/glew.h>
#include <algorithm>
#include <vector>
#include <chrono>
#include "AcerolaAscii.h"

//   AsciiEffect fx;
//   fx.begin();
//   renderYourDungeonScene();
//   fx.end(windowWidth, windowHeight);
class AsciiEffect {
public:
    // What the scene area shows;
    // the HUD and UI are drawn on top in every mode
    // Plain is the player-facing "shaders off" view;
    // Scene and Edges are dev views (T)
    enum class RenderView { Ascii = 0, Scene = 1, Edges = 2, Plain = 3 };
    void setRenderView(RenderView view) { m_renderView = view; }

    bool init(int sceneWidth, int sceneHeight, int cellSize = 8);
    void shutdown();

    void begin();
    void end(int windowWidth, int windowHeight);

    int sceneWidth() const { return m_fboW; }
    int sceneHeight() const { return m_fboH; }

    GLuint getUiFontTexture() const
    {
        return m_uiFontTex;
    }

    int getUiGlyphCount() const
    {
        return m_uiGlyphCount;
    }

    // fraction01 is the fill; enabled = false fades the bar out
    void setStamina(float fraction01, bool enabled = true);

    // Glitch spot: uv center, radius in ASCII cells. Call every frame
    void setWallGlitch(bool active, float u, float v, float radiusCells);

    // Health 0..1: how much the stamina frame and the screen bleed
    void setHealth(float fraction01);

    // SHARPNESS range: the minimum is still legible
    static constexpr int kMinCellSize = 6;
    static constexpr int kMaxCellSize = 20;

    // Rebuilds the atlases on a real change; call on slider release
    void setUserCellSize(int cellSize);
    int getUserCellSize() const { return m_cellSize; }

    // UI text over everything:
    // glyphIndex + 1 per cell (0 = transparent). cols/rows must match getMenuGridColsForWindow()/getMenuGridRowsForWindow()
    void setUIOverlay(bool enabled, const std::vector<unsigned char>& grid, int cols, int rows);

    // The scene grid follows the window size and the live cell size
    // Menus use a separate grid, independent of SHARPNESS, so they always fit
    static constexpr int kMenuReferenceCellSize = 11;
    static constexpr int kMenuMinGridRows = 90;
    static constexpr int kMenuMinGridCols = 124;
    static constexpr int kMenuMinCellSize = 4;
    static int menuCellSizeForWindow(int windowWidth, int windowHeight)
    {
        int cell = kMenuReferenceCellSize;
        if (windowHeight > 0) cell = std::min(cell, windowHeight / kMenuMinGridRows);
        if (windowWidth > 0)  cell = std::min(cell, windowWidth / kMenuMinGridCols);
        return std::max(cell, kMenuMinCellSize);
    }
    int getMenuGridColsForWindow(int windowWidth, int windowHeight) const
    {
        return windowWidth / menuCellSizeForWindow(windowWidth, windowHeight);
    }
    int getMenuGridRowsForWindow(int windowWidth, int windowHeight) const
    {
        return windowHeight / menuCellSizeForWindow(windowWidth, windowHeight);
    }

    // 0 = none
    // 1 = black; the caller animates it
    void setFadeAlpha(float alpha01);

    // Tints glyphs only; off by default
    void setColorEnabled(bool enabled) { m_colorEnabled = enabled; }
    bool isColorEnabled() const { return m_colorEnabled; }

    // Must match this frame's projection
    void setDepthRange(float nearPlane, float farPlane) { m_nearPlane = nearPlane; m_farPlane = farPlane; }

    void setLensEffectEnabled(bool enabled) { m_lensEffectEnabled = enabled; }

    // Where end() draws the finished frame: 0 for the window, or an offscreen target (CRT)
    void setWindowFramebuffer(GLuint fbo) { m_windowFramebuffer = fbo; }

    void setTime(float seconds) { m_time = seconds; }

private:
    GLuint m_fbo = 0;
    GLuint m_sceneTex = 0;
    GLuint m_depthTex = 0;
    GLuint m_program = 0;
    GLuint m_quadVAO = 0, m_quadVBO = 0;

    int m_fboW = 0, m_fboH = 0;
    int m_cellSize = 8;

    GLuint m_uiFontTex = 0;
    int m_uiGlyphCount = 0;

    float m_staminaFrac         = 1.0f;
    bool  m_staminaEnabledTarget = false;
    float m_staminaAlpha        = 0.0f;

    bool m_glitchActive = false;
    float m_glitchU = 0.5f, m_glitchV = 0.5f;
    float m_glitchRadiusCells = 4.0f;
    bool  m_staminaFadeTimeValid = false;
    std::chrono::steady_clock::time_point m_staminaFadeLastTime;
    float m_healthFrac    = 1.0f;


    GLuint m_uiOverlayTex = 0;
    bool   m_uiOverlayEnabled = false;
    int    m_uiOverlayCols = 0, m_uiOverlayRows = 0;

    float m_fadeAlpha = 0.0f;

    bool m_colorEnabled = false;
    AcerolaAscii m_acerola;
    float m_nearPlane = 0.05f, m_farPlane = 50.0f;
    GLint m_uniAcerolaCellTex = -1;
    GLint m_uniRenderView = -1;
    GLint m_uniDebugSceneTex = -1;
    GLint m_uniDebugEdgesTex = -1;
    RenderView m_renderView = RenderView::Ascii;
    GLint m_uniAcerolaGlyphTex = -1;

    bool m_lensEffectEnabled = false;
    GLuint m_windowFramebuffer = 0;
    float m_time = 0.0f;

    void createFBO(int w, int h);
    void destroyFBO();
    void generateUiFontAtlas();
    void createQuad();
    GLint m_uniScreenResolution = -1;
    GLint m_uniCellSize = -1;
    GLint m_uniUiFontTex = -1;
    GLint m_uniUiGlyphCount = -1;
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

    // Allocated per size, then updated in place
    int m_uiOverlayTexW = 0;
    int m_uiOverlayTexH = 0;

};
