#pragma once
#include <GL/glew.h>
#include <string>

// The wall texture (assets/textures/walls/).
// To swap it, drop in a file and change kDefaultName
class WallTexture {
public:
    static constexpr const char* kDefaultName = "str_stonebrk1_8bit.png";

    bool load(const std::string& filename);

    void destroy();

    GLuint id() const { return m_texture; }

    // Contrast stretch around 0.5 before lighting; 1 = unchanged
    float contrast = 1.1f;

private:
    GLuint m_texture = 0;
};
