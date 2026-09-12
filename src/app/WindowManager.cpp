#include <GL/glew.h>
#include "WindowManager.h"

bool WindowManager::create(int width, int height, const char* title)
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_window)
        return false;

    glfwMakeContextCurrent(m_window);

    // ВАЖНО: без этого цикл рендера ничем не ограничен — раньше игра
    // рендерила столько кадров в секунду, сколько вообще может выдать
    // GPU (сотни/тысячи fps на мощном железе), впустую нагружая
    // видеокарту/CPU и грея ноутбук без какой-либо пользы (экран всё
    // равно обновляется максимум с частотой монитора). glfwSwapInterval(1)
    // включает vsync — glfwSwapBuffers() сам синхронизируется с монитором,
    // и цикл не молотит быстрее, чем реально нужно.
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    glewInit();

    m_windowedW = width;
    m_windowedH = height;

    return true;
}

void WindowManager::destroy()
{
    glfwTerminate();
    m_window = nullptr;
}

void WindowManager::toggleFullscreen()
{
    if (!m_isFullscreen) {
        // Перед входом в fullscreen запоминаем позицию и размер окна.
        glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
        glfwGetWindowSize(m_window, &m_windowedW, &m_windowedH);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);

        glfwSetWindowMonitor(
            m_window, monitor, 0, 0,
            mode->width, mode->height, mode->refreshRate
        );

        m_isFullscreen = true;
    } else {
        glfwSetWindowMonitor(
            m_window, nullptr,
            m_windowedX, m_windowedY,
            m_windowedW, m_windowedH, 0
        );

        m_isFullscreen = false;
    }
}
