#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <exception>
#include "app/FatalError.h"
#include "app/WindowManager.h"
#include "app/Application.h"
#include "audio/AudioMixer.h"
#include "render/AsciiEffect.h"
#include "scene/DungeonScene.h"

//meowmeowmeowmeowmeowmeowmeowmeowmeow

// Mouse look only during gameplay; the cursor is free in menus
static bool s_mouseLookEnabled = false;

static void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    if (!s_mouseLookEnabled) return;
    DungeonScene* scene = (DungeonScene*)glfwGetWindowUserPointer(window);
    if (scene) scene->processMouse(xpos, ypos);
}

static int run() {
    WindowManager windowManager;
    if (!windowManager.create(1280, 720, "CELL"))
        return 1;

    GLFWwindow* window = windowManager.window();

    DungeonScene scene;
    scene.init();

    const int kSceneW = 1280, kSceneH = 720;
    const int kDefaultCellSize = 11;

    AsciiEffect ascii;
    ascii.init(kSceneW, kSceneH, kDefaultCellSize);

    scene.setCompassUiFont(
        ascii.getUiFontTexture(),
        ascii.getUiGlyphCount()
    );

    glfwSetWindowUserPointer(window, &scene);
    glfwSetCursorPosCallback(window, mouseCallback);
    // The session starts in the menu with a visible cursor
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    Application app;
    app.init(window, scene, ascii);

    double lastTime = glfwGetTime();

    while (!windowManager.shouldClose()) {
        double now = glfwGetTime();
        float deltaTime = (float)(now - lastTime);
        lastTime = now;
        deltaTime = std::min(deltaTime, 0.05f); // guard against a dt spike after pause/lag

        // Minimized: nothing is visible, so do not render or advance the game.
        // Wake up often enough to keep the streamed music fed (two ~100 ms chunks are queued)
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) || fbW == 0 || fbH == 0)
        {
            AudioMixer::instance().update(deltaTime);
            app.restartPerfCounter();
            glfwWaitEventsTimeout(0.05);
            continue;
        }

        app.tick(window, scene, ascii, windowManager, deltaTime);
        s_mouseLookEnabled = app.isMouseLookEnabled();

        windowManager.swapBuffers();
        glfwPollEvents();
    }

    // Autosave on window close; a no-op without an active session
    scene.saveActiveSlot();
    app.shutdown();
    scene.shutdown();
    ascii.shutdown();
    windowManager.destroy();
    return 0;
}

int main() {
    try {
        return run();
    } catch (const std::exception& e) {
        ReportFatalError(e.what());
    } catch (...) {
        ReportFatalError("Unknown error.");
    }
    return 1;
}
