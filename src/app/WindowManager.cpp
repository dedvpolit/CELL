#include <GL/glew.h>
#include "WindowManager.h"
#include "FatalError.h"
#include "stb_image.h"
#include <cstdio>
#include <string>

static void glfwErrorCallback(int code, const char* description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
}

bool WindowManager::create(int width, int height, const char* title)
{
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        ReportFatalError("Could not initialize GLFW: no display or window system is available.");
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_window) {
        ReportFatalError("Could not create an OpenGL 3.3 window. Please update your graphics driver.");
        glfwTerminate();
        return false;
    }

    // The window icon (taskbar/alt-tab) is separate from the .exe icon (baked in via app.rc). GLFW
    // cannot use .ico, so a .png is loaded via stb_image; STB_IMAGE_IMPLEMENTATION is already
    // defined in another .cpp, and defining it again here would be a link error. Not fatal if the
    // file is missing: the default system icon is used.
    {
        int iconW = 0, iconH = 0, iconChannels = 0;
        unsigned char* iconPixels = stbi_load("assets/icon.png", &iconW, &iconH, &iconChannels, 4);
        if (iconPixels)
        {
            GLFWimage icon;
            icon.width = iconW;
            icon.height = iconH;
            icon.pixels = iconPixels;
            glfwSetWindowIcon(m_window, 1, &icon);
            stbi_image_free(iconPixels);
        }
    }

    glfwMakeContextCurrent(m_window);

    // Without vsync the render loop is unbounded (hundreds or thousands of fps on capable
    // hardware), wasting GPU/CPU and heat for no benefit since the display refreshes only at the
    // monitor rate.
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    const GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK) {
        ReportFatalError(std::string("Could not initialize OpenGL extensions: ")
                         + reinterpret_cast<const char*>(glewGetErrorString(glewStatus)));
        glfwTerminate();
        m_window = nullptr;
        return false;
    }

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
        glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
        glfwGetWindowSize(m_window, &m_windowedW, &m_windowedH);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!mode) return;

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
