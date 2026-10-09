#pragma once
#include <GL/glew.h>

// CRT monitor look over the whole finished frame (scene, HUD, menus, TTF text): glow, scanlines,
// a curved screen with rounded corners, a little noise and optional phosphor persistence.
// When enabled, everything meant for the window is drawn into the offscreen target returned by
// beginFrame(), and present() composites it to the default framebuffer
class CrtEffect {
public:
    void init();
    void shutdown();

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    // Phosphor persistence (afterglow trails). Its two full-size buffers exist only while it is on
    void setPersistence(bool enabled) { m_persistence = enabled; }

    // Call once per frame before anything is drawn to the window;
    // (re)creates the targets on a size change
    // Returns the framebuffer the frame should be drawn into (0 when disabled)
    GLuint beginFrame(int width, int height);

    // Composites the captured frame to the window
    void present(float time, float deltaTime);

private:
    void createTargets(int width, int height);
    void destroyTargets();
    void createPhosphorTargets();
    void destroyPhosphorTargets();

    bool m_enabled = false;
    bool m_persistence = false;
    int m_width = 0, m_height = 0;

    GLuint m_quadVao = 0, m_quadVbo = 0;
    GLuint m_captureTex = 0, m_captureFbo = 0;
    GLuint m_phosphorTex[2] = {}, m_phosphorFbo[2] = {};
    GLuint m_bloomTex[2] = {}, m_bloomFbo[2] = {};
    int m_phosphorCurrent = 0;
    bool m_phosphorValid = false;

    struct Phosphor { GLuint prog = 0; GLint current, previous, decay; } m_phosphor;
    struct Blur { GLuint prog = 0; GLint source, texelStep; } m_blur;
    struct Final {
        GLuint prog = 0;
        GLint phosphor, bloom, resolution, time;
    } m_final;
};
