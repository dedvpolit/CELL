#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include "app/WindowManager.h"
#include "app/Application.h"
#include "render/AsciiEffect.h"
#include "scene/DungeonScene.h"

// Мышь двигает камеру ТОЛЬКО во время реального геймплея (FADE_TO_GAME/
// PLAYING) — см. Application::isMouseLookEnabled(), обновляется каждый
// кадр в Application::tick(). В остальное время (меню, паузы, фейды)
// курсор виден/захвачен для UI, а не для FPS-обзора.
static bool s_mouseLookEnabled = false;

static void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    if (!s_mouseLookEnabled) return;
    DungeonScene* scene = (DungeonScene*)glfwGetWindowUserPointer(window);
    if (scene) scene->processMouse(xpos, ypos);
}

int main() {
    WindowManager windowManager;
    if (!windowManager.create(1280, 720, "CELL"))
        return -1;

    GLFWwindow* window = windowManager.window();

    DungeonScene scene;
    scene.init();

    // Обычное разрешение внутреннего рендера сцены — НЕ дев-настройка,
    // всегда 1280x720. Кинематографичные размеры (для noclip/трейлера)
    // берутся из DevTools.h через геттеры DungeonScene (см.
    // Application::tick()), чтобы весь дев-тюнинг настраивался в одном месте.
    const int kNormalSceneW = 1280, kNormalSceneH = 720;
    const int kNormalCellSize = 11;

    AsciiEffect ascii;
    ascii.init(kNormalSceneW, kNormalSceneH, kNormalCellSize);

    scene.setCompassMinimapFont(
        ascii.getMinimapFontTexture(),
        ascii.getMinimapGlyphCount()
    );

    glfwSetWindowUserPointer(window, &scene);
    glfwSetCursorPosCallback(window, mouseCallback);
    // Начинаем с обычного видимого курсора — сессия стартует в меню, где
    // нужен клик по кнопкам. Application::tick() сам переключает режим на
    // GLFW_CURSOR_DISABLED, когда начинается реальный геймплей.
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    Application app;
    app.init(window, scene, ascii);

    double lastTime = glfwGetTime();

    while (!windowManager.shouldClose()) {
        double now = glfwGetTime();
        float deltaTime = (float)(now - lastTime);
        lastTime = now;
        deltaTime = std::min(deltaTime, 0.05f); // защита от скачка dt после паузы/лагов

        app.tick(window, scene, ascii, windowManager, deltaTime);
        s_mouseLookEnabled = app.isMouseLookEnabled();

        windowManager.swapBuffers();
        glfwPollEvents();
    }

    scene.saveActiveSlot(); // автосейв текущего прогресса при закрытии
                             // окна (крестик/Alt+F4) — не только при явном
                             // выходе в меню кнопкой (см. Application.cpp:
                             // RETURN_TO_MENU); saveActiveSlot() сам ничего
                             // не делает, если активной игровой сессии нет
                             // (m_activeSlot < 0 — окно закрыли прямо со
                             // стартового экрана).
    scene.shutdown();
    ascii.shutdown();
    windowManager.destroy();
    return 0;
}
