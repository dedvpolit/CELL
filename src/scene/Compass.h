#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>

// ============================================================================
// Compass — экранный оверлей 3D-компаса с ASCII-мини-картой на верхнем
// диске (клавиша V показывает/прячет, см. m_compassVisible/m_poseBlend в
// DungeonScene — переключение и анимация появления/скрытия остаются в
// DungeonScene, т.к. это состояние ввода/позы, а не самого рендера).
// Рисуется своим отдельным шейдером (assets/shaders/compass.{vert,frag}),
// после AsciiEffect::end() (не проходит через основной ASCII-постпроцесс).
//
// Вынесено из DungeonScene при разбиении монолита на модули.
//
// ПРИМЕЧАНИЕ: render() ниже сознательно не принимает позицию/направление
// камеры — в исходном коде эти параметры (camPos/front/cameraRight/
// cameraUp) вычислялись в DungeonScene::renderCompassOverlay() и
// передавались в DungeonScene::renderCompass(), но фактически нигде
// внутри renderCompass() не использовались (компас — чисто экранный
// оверлей с фиксированным screenCenter, а не объект в мировых
// координатах). Это уже было так до рефакторинга — поведение не
// менялось, просто убран неиспользуемый параметр там, где он был
// собственным методом render(). Вычисление getCameraRenderPosition()/
// getCameraRenderUp() в DungeonScene не тронуто (см. комментарий там).
// ============================================================================
class Compass {
public:
    void create();
    void destroy();

    // Тот же атлас глифов, что использует плоская мини-карта в
    // AsciiEffect — общий, чтобы обе мини-карты не расходились
    // визуально (см. DungeonScene::setCompassMinimapFont()).
    void setMinimapFont(GLuint texture, int glyphCount) {
        m_minimapFontTex = texture;
        m_minimapGlyphCount = glyphCount;
    }

    // poseBlend — 0..1, насколько компас "вынут" (см. DungeonScene::
    // m_poseBlend), no-op при poseBlend<=0. yawDeg — поворот игрока для
    // ориентации мини-карты. minimapTexture — см. MinimapFog::minimapTexture().
    // Признак успешной компиляции/линковки шейдера компаса — публичный
    // геттер нужен только для DungeonScene::init(), которая проверяет
    // общий успех инициализации (см. return m_program != 0 && ...).
    bool isReady() const { return m_program != 0; }

    // enemySpottedAlphas/enemyMinimapOffsets — УЛУЧШЕНИЕ ("4 врага по
    // всей карте" — теперь массив вместо одного значения, см.
    // DungeonScene::kEnemyCount). Каждый элемент — см.
    // EnemyAI::spottedMarkerAlpha() (0..1, плавно гаснет перед
    // истечением "памяти", а не щёлкает мгновенно) и целая клетка ЭТОГО
    // врага минус целая клетка игрока (X), и наоборот (Z) — та же
    // система координат, что и у сетки внутри compass.frag (см. большой
    // комментарий там); считается один раз в DungeonScene::
    // renderCompassOverlay(), а не в шейдере, потому что ничего
    // специфичного для GPU в этой арифметике нет. Векторы передаются по
    // значению (небольшие, фиксированный размер kEnemyCount) — проще,
    // чем городить ещё один raw-массив с ручным подсчётом size().
    void render(float poseBlend, float yawDeg,
                GLuint minimapTexture, bool colorEnabled,
                std::vector<float> enemySpottedAlphas,
                std::vector<glm::vec2> enemyMinimapOffsets);

private:
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLsizei m_vertexCount = 0;

    GLuint m_minimapFontTex = 0;
    int m_minimapGlyphCount = 13;

    // ---- Cached uniform locations for m_program (see render()) ----
    // Same rationale as DungeonScene/AsciiEffect (see comments there):
    // glGetUniformLocation() does a name lookup in the driver and was
    // being called ~9 times every frame the compass is visible, for
    // names that never change after the program is linked. Resolved
    // once in create() instead (cacheUniformLocations()).
    GLint m_uniScreenCenter = -1;
    GLint m_uniScreenScale = -1;
    GLint m_uniView = -1;
    GLint m_uniProjection = -1;
    GLint m_uniMinimapTex = -1;
    GLint m_uniMinimapYawDeg = -1;
    GLint m_uniMinimapFontTex = -1;
    GLint m_uniMinimapGlyphCount = -1;
    GLint m_uniColorEnabled = -1;
    // Массивы (см. большой комментарий у render() выше) — базовый
    // location индекса 0 достаточен для заливки glUniform*fv() на весь
    // массив разом (тот же приём, что и у devLightPos[8] в
    // DungeonScene.cpp).
    GLint m_uniEnemySpottedAlpha = -1;
    GLint m_uniEnemyMinimapOffset = -1;
    GLint m_uniEnemyCount = -1;
    void cacheUniformLocations();
};
