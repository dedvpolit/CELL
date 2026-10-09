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

    // Performance mode: swap interval 2 gives a steady 30 fps on 60 Hz at about half the load
    void setPerformanceMode(bool enabled)
    {
        m_performanceMode = enabled;
        applySwapInterval();
    }
    bool isPerformanceMode() const { return m_performanceMode; }
    void togglePerformanceMode() { setPerformanceMode(!m_performanceMode); }

    // Benchmark mode: vsync off. With vsync the GPU downclocks to fill the frame
    // the [perf] gpu timings only show real costs while this is on. Overrides performance mode
    void setUncapped(bool enabled)
    {
        m_uncapped = enabled;
        applySwapInterval();
    }
    bool isUncapped() const { return m_uncapped; }
    void toggleUncapped() { setUncapped(!m_uncapped); }

private:
    GLFWwindow* m_window = nullptr;
    bool m_isFullscreen = false;
    int m_windowedX = 100, m_windowedY = 100;
    int m_windowedW = 1280, m_windowedH = 720;
    bool m_performanceMode = false;
    bool m_uncapped = false;

    void applySwapInterval() { glfwSwapInterval(m_uncapped ? 0 : (m_performanceMode ? 2 : 1)); }
};
