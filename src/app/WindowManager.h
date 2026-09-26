#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>

class WindowManager {
public:
    bool create(int width, int height, const char* title);
    void destroy();

    GLFWwindow* window() const { return m_window; }
    bool shouldClose() const { return glfwWindowShouldClose(m_window); }
    void swapBuffers() { glfwSwapBuffers(m_window); }

    void toggleFullscreen();

    // Performance mode: on weak integrated GPUs a stable 30 fps feels better than a 60 fps that
    // dips under thermal throttling. swapInterval(2) waits two refreshes (30 fps on 60 Hz) at
    // roughly half the load.
    void setPerformanceMode(bool enabled)
    {
        m_performanceMode = enabled;
        glfwSwapInterval(enabled ? 2 : 1);
    }
    bool isPerformanceMode() const { return m_performanceMode; }
    void togglePerformanceMode() { setPerformanceMode(!m_performanceMode); }

private:
    GLFWwindow* m_window = nullptr;
    bool m_isFullscreen = false;
    int m_windowedX = 100, m_windowedY = 100;
    int m_windowedW = 1280, m_windowedH = 720;
    bool m_performanceMode = false;
};
