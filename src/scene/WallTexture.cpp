#include "WallTexture.h"
#include "render/AssetPath.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstdio>

bool WallTexture::load(const std::string& filename)
{
    const std::string relPath = std::string("assets/textures/walls/") + filename;
    const std::string fullPath = AssetPath::Resolve(relPath);

    if (fullPath.empty())
    {
        std::fprintf(
            stderr,
            "[wall texture] file not found: %s (searched upward from the exe folder and the working directory)\n",
            relPath.c_str()
        );
        return false;
    }

    // Flip on Y at load time, so UVs computed in the shader from world coordinates are not upside down relative to how texture packs are usually oriented
    stbi_set_flip_vertically_on_load(1);

    // RGB only; the shader reads .rgb
    int w = 0, h = 0, channelsInFile = 0;
    unsigned char* pixels = stbi_load(fullPath.c_str(), &w, &h, &channelsInFile, 3);

    if (!pixels)
    {
        std::fprintf(
            stderr,
            "[wall texture] failed to decode '%s': %s\n",
            fullPath.c_str(),
            stbi_failure_reason()
        );
        return false;
    }

    if (m_texture == 0)
        glGenTextures(1, &m_texture);

    glBindTexture(GL_TEXTURE_2D, m_texture);
    // RGB rows are not 4-byte aligned (e.g. 1254 * 3 bytes)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    // Mips stop distant walls from shimmering (and flipping glyphs); NEAREST keeps the blocky look
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);

    std::printf(
        "[wall texture] loaded '%s' (%dx%d, %d channels in file)\n",
        fullPath.c_str(),
        w,
        h,
        channelsInFile
    );

    return true;
}

void WallTexture::destroy()
{
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
}
