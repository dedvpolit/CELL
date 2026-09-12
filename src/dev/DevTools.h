#pragma once
// ============================================================================
// DevTools.h — необязательные клавиши разработчика/бета-тестера.
//
// Здесь и только здесь описана вся раскладка дев-клавиш: N (noclip),
// M (debug-карта), +/- (дальность обзора в noclip), H/J (debug-урон/лечение
// здоровья, пока нет боевой системы), C (вкл/выкл кинематографичный буст
// разрешения/детализации ASCII, который noclip обычно включает вместе с
// собой — см. IsCinematicResolutionEnabled() ниже), I (невидимость для
// врага — см. ToggleInvisibleToEnemy() ниже), L/K/U (композиция сцены:
// статичный свет / манекен врага / отмена последнего из двух — см.
// ConsumeSpawnLightKey()/ConsumeSpawnDummyEnemyKey()/ConsumeUndoKey()
// ниже, реальная логика — в DungeonScene), G (перебор цветовых гамм
// окружения — см. ConsumeCyclePaletteKey() ниже). Логика "что происходит
// при noclip" или "что рисует debug-карта" по-прежнему живёт в
// DungeonScene — этот файл отвечает только за то, КОГДА и КАКИЕ клавиши
// переключают эти флаги, и за все числовые константы дев-тюнинга.
//
// Как отключить дев-инструменты для релизной сборки — два варианта:
//
//   1) Ничего не удалять, просто поставить 0 здесь:
//         #define DEV_TOOLS_ENABLED 0
//      Игра соберётся как обычно, но N/M/+/-/H/J перестанут что-либо
//      делать — обычный игрок не получит дев-возможностей.
//
//   2) Удалить (или переименовать) этот файл целиком. DungeonScene.h
//      подключает его через __has_include(), поэтому отсутствие файла
//      НЕ ЛОМАЕТ сборку — DungeonScene просто не увидит HAS_DEV_TOOLS
//      и все вызовы DevTools::... в DungeonScene.cpp окажутся внутри
//      неактивной #ifdef HAS_DEV_TOOLS ветки (см. processInput() там).
// ============================================================================
#define DEV_TOOLS_ENABLED 1

#if DEV_TOOLS_ENABLED

#include <GLFW/glfw3.h>
#include <algorithm>

namespace DevTools {

// ---- Дальность обзора (см. RENDER_DISTANCE в шейдере DungeonScene) ----
// Множитель поверх обычной дальности прорисовки/тумана. 1.0 = как у
// обычного игрока. Меняется только клавишами +/- и только пока активен
// noclip — вне noclip всегда 1.0, чтобы не влиять на игровой баланс.
inline float s_viewDistanceMul = 1.0f;

constexpr float kViewDistanceMin = 1.0f;
constexpr float kViewDistanceMax = 8.0f;
constexpr float kViewDistanceStep = 2.0f; // за секунду при удержании клавиши

// Стартовое значение множителя при входе в noclip — сразу неплохой кадр
// для трейлера, дальше подстраивается точнее клавишами +/-.
constexpr float kNoclipDefaultViewDistanceMul = 2.5f;

inline float GetViewDistanceMultiplier() {
    return s_viewDistanceMul;
}

// Edge-triggered переключение noclip по клавише N. outNoclipEnabled —
// состояние, которым владеет DungeonScene (её m_noclipEnabled); эта
// функция только решает, когда его перещёлкнуть, и заодно выставляет
// стартовую дальность обзора для кинематографичных кадров.
inline void ToggleNoclip(GLFWwindow* window, bool& outNoclipEnabled, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS;
    if (down && !keyWasDown) {
        outNoclipEnabled = !outNoclipEnabled;
        s_viewDistanceMul = outNoclipEnabled ? kNoclipDefaultViewDistanceMul : 1.0f;
    }
    keyWasDown = down;
}

// Edge-triggered переключение debug-карты по клавише M.
inline void ToggleDebugMap(GLFWwindow* window, bool& outVisible, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS;
    if (down && !keyWasDown) {
        outVisible = !outVisible;
    }
    keyWasDown = down;
}

// Edge-triggered переключение "невидимости" для врага по клавише I —
// пока включено, EnemyAI::update() форсирует canSee=false для честного
// зрительного обнаружения (см. большой комментарий в EnemyAI.cpp у
// параметра playerInvisible), независимо от реальных FOV/LOS/радиуса, И
// глушит слух (враг не реагирует на шум шагов/бега). Полная
// неприметность — удобно для тестирования уровня/врага без помех от ИИ.
inline void ToggleInvisibleToEnemy(GLFWwindow* window, bool& outInvisible, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS;
    if (down && !keyWasDown) {
        outInvisible = !outInvisible;
    }
    keyWasDown = down;
}

// ---- Композиция сцены для тестирования: свет (L) / манекен врага (K) /
// отмена (U) — см. DungeonScene::spawnDevLightAtPlayerView()/
// spawnDevDummyEnemyAtPlayerView()/undoLastDevAction(). Сами эти три
// функции ниже — ТОЛЬКО детекторы "нажали ли только что", без владения
// состоянием (тем же принципом, что и остальной этот файл) — вся
// реальная логика (куда спавнить, история для undo) живёт в
// DungeonScene, у которой есть доступ к камере/сцене/списку объектов.
inline bool ConsumeSpawnLightKey(GLFWwindow* window, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS;
    const bool fire = down && !keyWasDown;
    keyWasDown = down;
    return fire;
}

inline bool ConsumeSpawnDummyEnemyKey(GLFWwindow* window, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS;
    const bool fire = down && !keyWasDown;
    keyWasDown = down;
    return fire;
}

inline bool ConsumeUndoKey(GLFWwindow* window, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS;
    const bool fire = down && !keyWasDown;
    keyWasDown = down;
    return fire;
}

// Перебор цветовых гамм окружения по клавише G — см.
// DungeonScene::cycleDevPalette()/uniform devPaletteOverride в
// scene.frag. Как и остальные функции здесь — только детектор нажатия,
// сам список гамм и текущий индекс живут в DungeonScene.
inline bool ConsumeCyclePaletteKey(GLFWwindow* window, bool& keyWasDown) {
    const bool down = glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS;
    const bool fire = down && !keyWasDown;
    keyWasDown = down;
    return fire;
}

// ---- Кинематографичный буст рендера для трейлерных кадров (см. main.cpp) ----
// Всё, что относится к "как именно выглядит noclip на записи", собрано
// здесь одним куском — размеры и включатель ниже.
//
// Разрешение внутреннего FBO сцены и размер ASCII-ячейки в
// кинематографичном режиме. Обычная игра всегда использует 1280x720 и
// cellSize=11 (см. main.cpp) — это НЕ дев-настройка, трогать эти значения
// незачем; здесь только то, чем расширяется картинка в noclip.
//
// ВАЖНО: kCinematicCellSize должен расти пропорционально росту
// kCinematicSceneW/H относительно обычных 1280x720, иначе размер
// символа на экране (≈ windowWidth * cellSize / sceneW) меняется вместе
// с режимом — раньше здесь стояло 6 (меньше обычных 11) при сцене,
// увеличенной в 1.5 раза, из-за чего символы в noclip внезапно
// становились почти втрое мельче, а не оставались "как везде".
// 1920/1280 = 1080/720 = 1.5, поэтому 11 * 1.5 = 16.5 → 17.
constexpr int kCinematicSceneW    = 1920;
constexpr int kCinematicSceneH    = 1080;
constexpr int kCinematicCellSize  = 17; // масштабируется вместе со сценой (11 * 1.5), а не уменьшается

// Буст разрешения/детализации — штука не всегда нужная (пересоздание FBO
// и шрифтовых атласов может ощутимо лагать на слабом железе, а иногда
// хочется просто полетать в noclip без визуальных сюрпризов). Поэтому
// это отдельный тумблер, а не часть самого noclip: клавиша C включает и
// выключает его независимо, в любой момент — в т.ч. уже находясь в
// noclip (см. main.cpp: применяется сразу, без выхода из noclip).
// По умолчанию включён — совпадает с поведением "noclip = трейлер",
// как было раньше.
inline bool s_cinematicResolutionEnabled = true;

inline bool IsCinematicResolutionEnabled() {
    return s_cinematicResolutionEnabled;
}

// Edge-triggered переключение буста разрешения/детализации по клавише C.
// Состояние клавиши (keyWasDown) хранится статиком внутри функции — этому
// тумблеру не нужно, чтобы им владел DungeonScene, в отличие от noclip/
// debug-карты, за которые снаружи цепляется остальная логика сцены.
inline void ToggleCinematicResolution(GLFWwindow* window) {
    static bool keyWasDown = false;
    const bool down = glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
    if (down && !keyWasDown) {
        s_cinematicResolutionEnabled = !s_cinematicResolutionEnabled;
    }
    keyWasDown = down;
}

// Плавная регулировка дальности обзора клавишами +/- (пока держатся).
// '=' — тот же физический ключ, что и '+' на большинстве раскладок без
// Shift, поэтому слушаем оба варианта плюс numpad +/-.
inline void UpdateViewDistance(GLFWwindow* window, bool noclipEnabled, float deltaTime) {
    if (!noclipEnabled) {
        s_viewDistanceMul = 1.0f;
        return;
    }

    const bool plusDown =
        glfwGetKey(window, GLFW_KEY_EQUAL)  == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS;
    const bool minusDown =
        glfwGetKey(window, GLFW_KEY_MINUS)       == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS;

    if (plusDown)  s_viewDistanceMul += kViewDistanceStep * deltaTime;
    if (minusDown) s_viewDistanceMul -= kViewDistanceStep * deltaTime;

    s_viewDistanceMul = std::clamp(s_viewDistanceMul, kViewDistanceMin, kViewDistanceMax);
}

// Edge-triggered debug-урон/лечение по 10хп клавишами H/J — временная
// замена настоящей боевой системы, см. DungeonScene::m_health.
inline void ApplyHealthDebugKeys(
    GLFWwindow* window,
    float& health,
    float maxHealth,
    bool& hKeyWasDown,
    bool& jKeyWasDown)
{
    const bool hDown = glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS;
    if (hDown && !hKeyWasDown) {
        health = std::max(health - 10.0f, 0.0f);
    }
    hKeyWasDown = hDown;

    const bool jDown = glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS;
    if (jDown && !jKeyWasDown) {
        health = std::min(health + 10.0f, maxHealth);
    }
    jKeyWasDown = jDown;
}

} // namespace DevTools

// Единственный флаг, на который смотрит остальной код (DungeonScene.h/.cpp),
// чтобы понять, доступны ли дев-инструменты в этой сборке.
#define DEV_TOOLS_ACTIVE 1

#endif // DEV_TOOLS_ENABLED
