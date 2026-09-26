#pragma once
#include <GL/glew.h>
#include <algorithm>
#include <vector>
#include <chrono>

//   AsciiEffect fx;
//   fx.begin();
//     renderYourDungeonScene();
//   fx.end(windowWidth, windowHeight);
class AsciiEffect {
public:
    bool init(int sceneWidth, int sceneHeight, int cellSize = 8);
    void shutdown();

    void begin();
    void end(int windowWidth, int windowHeight);

    void resize(int sceneWidth, int sceneHeight);

    GLuint getUiFontTexture() const
    {
        return m_uiFontTex;
    }

    int getUiGlyphCount() const
    {
        return m_uiGlyphCount;
    }

    // Stamina bar at the bottom: fraction01 is the fill (0..1); enabled = false fades it out
    // (m_staminaAlpha, on real elapsed time inside end()). Call every frame between begin() and
    // end(); the value persists until the next call.
    void setStamina(float fraction01, bool enabled = true);

    // "Unreliable vision" glyph glitch: active = false disables it; uv is the 0..1 spot center,
    // radiusCells its size in ASCII-grid cells (independent of cols/rows). Call every frame between
    // begin() and end().
    void setWallGlitch(bool active, float u, float v, float radiusCells);

    // Player health fraction (0..1), see DungeonScene::getHealthFraction(). Controls how much the
    // stamina bar's frame drips: 1.0 = a clean frame, 0.0 = dripping all the way around. Call every
    // frame together with setStamina().
    void setHealth(float fraction01);

    // Cinematic mode for trailer shots (usually with noclip): cinematicCellSize is the cell size in
    // this mode (DevTools.h::kCinematicCellSize). It regenerates both font atlases, so call it only
    // on a mode change.
    void setCinematicMode(bool enabled, int cinematicCellSize = 6);

    // ASCII cell size in normal mode, the SHARPNESS setting. Smaller = finer cells, more glyphs, a
    // crisper image; larger = bigger glyphs, a stronger ASCII effect. The range is picked by hand:
    // the minimum is still legible, the maximum does not turn the grid into a few giant characters.
    static constexpr int kMinCellSize = 6;
    static constexpr int kMaxCellSize = 20;

    // While cinematic mode is active this is only remembered and takes effect when it turns off. It
    // regenerates the atlases on an actual change: call it on slider release, not on every pixel of
    // a drag.
    void setUserCellSize(int cellSize);
    int getUserCellSize() const { return m_normalCellSize; }

    // UI text over everything (menu title, buttons): grid holds glyph indices per cell (0 =
    // transparent, otherwise glyphIndex + 1), sized cols*rows; cols/rows must match the grid the
    // shader samples (getMenuGridColsForWindow()/RowsForWindow() for menus) or the text drifts.
    void setUIOverlay(bool enabled, const std::vector<unsigned char>& grid, int cols, int rows);

    // The scene's ASCII grid comes from the real window size in the shader (screenResolution /
    // cellSize), not from the FBO size.
    //
    // Menus use their own grid overlaid via UV, independent of SHARPNESS, so they always fit. The
    // menu cell is kMenuReferenceCellSize (11 px) and shrinks on small windows so the grid keeps
    // kMenuMinGridRows x kMenuMinGridCols (every menu screen needs 87 x 119); at 1280x720 it is 8
    // px, the size of a glyph bitmap. Always use menuCellSizeForWindow(), not the constant.
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

    // Smooth full-screen fade (0 = none, 1 = fully black) for the transition between the start menu
    // and gameplay, applied on top of everything (scene, UI, stamina) in the shader. Not
    // smoothed here: the caller (Application) passes an already interpolated value.
    void setFadeAlpha(float alpha01);

    // Color mode of the ASCII render: only the glyph color changes, the scene is still drawn purely
    // with glyphs. Off by default (black and white).
    void setColorEnabled(bool enabled) { m_colorEnabled = enabled; }
    bool isColorEnabled() const { return m_colorEnabled; }

    void setLensEffectEnabled(bool enabled) { m_lensEffectEnabled = enabled; }

    void setTime(float seconds) { m_time = seconds; }

private:
    GLuint m_fbo = 0;
    GLuint m_sceneTex = 0;
    GLuint m_depthTex = 0;
    GLuint m_fontTex = 0;
    GLuint m_program = 0;
    GLuint m_quadVAO = 0, m_quadVBO = 0;

    int m_fboW = 0, m_fboH = 0;
    int m_cellSize = 8;

    GLuint m_uiFontTex = 0;
    int m_uiGlyphCount = 0;

    int m_rampLength = 0;

    float m_staminaFrac         = 1.0f;
    bool  m_staminaEnabledTarget = false;
    float m_staminaAlpha        = 0.0f;

    bool m_glitchActive = false;
    float m_glitchU = 0.5f, m_glitchV = 0.5f;
    float m_glitchRadiusCells = 4.0f;
    bool  m_staminaFadeTimeValid = false;
    std::chrono::steady_clock::time_point m_staminaFadeLastTime;
    float m_healthFrac    = 1.0f;

    bool m_cinematicMode  = false;
    int  m_normalCellSize = 8; // remembered in init() as the value to return to

    GLuint m_uiOverlayTex = 0;
    bool   m_uiOverlayEnabled = false;
    int    m_uiOverlayCols = 0, m_uiOverlayRows = 0;

    float m_fadeAlpha = 0.0f;

    bool m_colorEnabled = false;

    bool m_lensEffectEnabled = false;
    float m_time = 0.0f;

    void createFBO(int w, int h);
    void destroyFBO();
    void generateFontAtlas();
    void generateUiFontAtlas();
    void createQuad();
    GLint m_uniSceneTex = -1;
    GLint m_uniFontTex = -1;
    GLint m_uniScreenResolution = -1;
    GLint m_uniCellSize = -1;
    GLint m_uniRampLength = -1;
    GLint m_uniUiFontTex = -1;
    GLint m_uniUiGlyphCount = -1;
    GLint m_uniStaminaFrac = -1;
    GLint m_uniGlitchActive = -1, m_uniGlitchUV = -1;
    GLint m_uniGlitchRadiusCells = -1;
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

    // The UI overlay texture is allocated once per size and then overwritten in place:
    // setUIOverlay() runs every frame in menus (the title animation rebuilds the grid), but
    // cols/rows rarely change.
    int m_uiOverlayTexW = 0;
    int m_uiOverlayTexH = 0;

};
