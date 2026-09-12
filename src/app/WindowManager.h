#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// ============================================================================
// WindowManager — создание GLFW-окна, vsync, переключение fullscreen.
// Вынесено из main.cpp при разбиении на модули (Этап 5).
// ============================================================================
class WindowManager {
public:
    bool create(int width, int height, const char* title);
    void destroy();

    GLFWwindow* window() const { return m_window; }
    bool shouldClose() const { return glfwWindowShouldClose(m_window); }
    void swapBuffers() { glfwSwapBuffers(m_window); }
    void getFramebufferSize(int& w, int& h) const { glfwGetFramebufferSize(m_window, &w, &h); }

    // Запоминает позицию/размер окна ПЕРЕД входом в fullscreen, чтобы
    // при выходе восстановить их (а не всегда открываться заново по
    // центру экрана в дефолтном размере).
    void toggleFullscreen();

    // ---- Performance / Stability mode (см. README/DevTools) ----
    // На слабых ноутбуках со встроенной видеокартой честные СТАБИЛЬНЫЕ
    // 30fps почти всегда ощущаются лучше "плавающих" 60fps, которые
    // проседают под тепловым троттлингом — гоняя GPU на полную каждую
    // секунду, чип греется сильнее и быстрее упирается в термолимит,
    // после чего частоты (и fps) падают ниже, чем если бы GPU изначально
    // работал вполовину нагрузки. glfwSwapInterval(2) ждёт кадр монитора
    // ДВАЖДЫ перед свапом — на обычном 60Гц экране это даёт ровно 30fps
    // и примерно вдвое меньшую среднюю загрузку GPU/CPU за секунду.
    // Toggle меняет реальный интервал немедленно (см. main.cpp — клавиша
    // включения).
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
