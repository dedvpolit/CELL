#pragma once
#include <GL/glew.h>
#include <string>

// ============================================================================
// WallTexture — загрузка и владение GL-текстурой стен подземелья через
// stb_image (см. загрузку в assets/textures/walls/, resolve через
// render/AssetPath — тот же поиск "вверх от папки exe", что используют
// шейдеры). Вынесено из DungeonScene при разбиении монолита на модули —
// раньше loadWallTexture()/локальная locateWallTextureAsset() жили прямо
// внутри DungeonScene.cpp вперемешку с остальной логикой сцены.
//
// Чтобы поставить другую стеновую текстуру, ДОСТАТОЧНО положить нужный
// файл в assets/textures/walls/ и поменять kDefaultName ниже — сам
// шейдер/пайплайн трогать не нужно.
// ============================================================================
class WallTexture {
public:
    static constexpr const char* kDefaultName = "str_stonebrk1_8bit.png";

    // Грузит assets/textures/walls/<filename> через stb_image (создаёт
    // GL-текстуру при первом вызове, дальше переиспользует тот же ID).
    // Возвращает false, если файл не найден или не декодировался — при
    // этом старая текстура (если была) остаётся как есть, чтобы не
    // ронять рендер.
    bool load(const std::string& filename);

    void destroy();

    GLuint id() const { return m_texture; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    // Контраст-стретч texColor вокруг серой точки (0.5) ДО умножения на
    // ambient/факельный свет — см. wallTexContrast в assets/shaders/scene.frag.
    // 1.0 = без изменений. Публичное поле — можно крутить в реальном
    // времени (например, из DevTools) без пересборки шейдера.
    float contrast = 1.1f;

private:
    GLuint m_texture = 0;
    int m_width = 0;
    int m_height = 0;
};
