#pragma once
#include <GL/glew.h>
#include <string>

// Loads and owns the dungeon wall GL texture via stb_image (see assets/textures/walls/, resolved
// through render/AssetPath: the same "walk up from the exe folder" search the shaders use). To swap
// the wall texture, drop the file into assets/textures/walls/ and change kDefaultName below:
// nothing in the shader/pipeline has to be touched.
class WallTexture {
public:
    static constexpr const char* kDefaultName = "str_stonebrk1_8bit.png";

    bool load(const std::string& filename);

    void destroy();

    GLuint id() const { return m_texture; }

    // Contrast-stretches texColor around the gray point (0.5) before multiplying by ambient/torch
    // light. 1.0 = unchanged. A public field, so it can be tweaked live (e.g. from DevTools)
    // without rebuilding the shader.
    float contrast = 1.1f;

private:
    GLuint m_texture = 0;
};
