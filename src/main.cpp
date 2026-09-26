#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <exception>
#include "app/FatalError.h"
#include "app/WindowManager.h"
#include "app/Application.h"
#include "render/AsciiEffect.h"
#include "scene/DungeonScene.h"

// The mouse moves the camera only during actual gameplay (FADE_TO_GAME/PLAYING): see
// Application::isMouseLookEnabled(), updated every frame in Application::tick(). Otherwise (menu,
// pause, fades) the cursor is visible/free for the UI, not FPS look.
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

    // The internal scene render is always 1280x720; the cinematic sizes (noclip/trailer) come from
    // DevTools.h via DungeonScene's getters (see Application::tick()).
    const int kNormalSceneW = 1280, kNormalSceneH = 720;
    const int kNormalCellSize = 11;

    AsciiEffect ascii;
    ascii.init(kNormalSceneW, kNormalSceneH, kNormalCellSize);

    scene.setCompassUiFont(
        ascii.getUiFontTexture(),
        ascii.getUiGlyphCount()
    );

    glfwSetWindowUserPointer(window, &scene);
    glfwSetCursorPosCallback(window, mouseCallback);
    // Start with a plain visible cursor: the session begins in the menu, which needs clickable
    // buttons. Application::tick() switches to GLFW_CURSOR_DISABLED once actual gameplay starts.
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    Application app;
    app.init(window, scene, ascii);

    double lastTime = glfwGetTime();

    while (!windowManager.shouldClose()) {
        double now = glfwGetTime();
        float deltaTime = (float)(now - lastTime);
        lastTime = now;
        deltaTime = std::min(deltaTime, 0.05f); // guard against a dt spike after pause/lag

        app.tick(window, scene, ascii, windowManager, deltaTime);
        s_mouseLookEnabled = app.isMouseLookEnabled();

        windowManager.swapBuffers();
        glfwPollEvents();
    }

    // autosave on window close (X button/Alt+F4); a no-op if there is no active game session
    scene.saveActiveSlot();
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
