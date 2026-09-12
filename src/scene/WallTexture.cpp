#include "WallTexture.h"
#include "render/AssetPath.h"

// ---- Загрузка изображений (см. load() ниже) ----
// Однозаголовочная public-domain библиотека, implementation-блок собран
// только здесь, в одной единице трансляции (раньше был в DungeonScene.cpp,
// хотя stb_image использовался только внутри loadWallTexture()).
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
            "[wall texture] file not found: %s (искал вверх от папки exe и от текущей рабочей директории)\n",
            relPath.c_str()
        );
        return false;
    }

    // Переворачиваем по Y при загрузке — так UV, посчитанные в шейдере
    // из мировых координат (см. assets/shaders/scene.frag), не оказываются
    // перевёрнутыми относительно того, как обычно ориентированы текстурные пакеты.
    stbi_set_flip_vertically_on_load(1);

    // Item 2 (review): loaded as 3 channels (RGB), not 4 — the fragment
    // shader only ever reads texture(wallTex, texUV).rgb (see scene.frag),
    // so the alpha channel was pure unused VRAM/bandwidth.
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
    // RGB (3 bytes/pixel) is NOT guaranteed to be 4-byte-aligned per row
    // like RGBA always is (width*4 is always %4==0; width*3 isn't, e.g.
    // this texture is 1254 wide -> 3762 bytes/row, 3762%4==2). GL's
    // default GL_UNPACK_ALIGNMENT is 4, so without this the driver pads
    // each row out to the next 4-byte boundary while reading OUR tightly
    // packed buffer, walking past its end on every row but the last —
    // a heap over-read that crashed on load (0xC0000005). Same fix
    // AsciiEffect.cpp already applies before its own GL_RED uploads.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // GL_RGB8 (sized internal format) instead of the old GL_RGBA: exact
    // same bits-per-texel as before minus the unused alpha channel.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    // Item 2 (review): mip levels now generated (glGenerateMipmap) so
    // distant walls sample from a properly downfiltered level instead of
    // the full-res level GL_NEAREST would otherwise pick — that mismatch
    // is exactly what causes texel aliasing/shimmering at a distance
    // (worse here than usual: the ASCII post-effect picks glyphs from
    // per-cell brightness, so shimmering texels can flip which glyph gets
    // chosen frame-to-frame). Deliberately staying on
    // GL_NEAREST_MIPMAP_NEAREST rather than the reviewer's suggested
    // GL_LINEAR_MIPMAP_LINEAR, though: the comment this replaced explains
    // GL_NEAREST is an intentional aesthetic choice (blocky 8-bit source
    // textures, further quantized by the ASCII postprocess anyway), and
    // that reasoning still holds — GL_NEAREST_MIPMAP_NEAREST keeps blocky,
    // non-blurred sampling *within* whichever mip level is selected, it
    // just picks a lower-resolution level at a distance instead of
    // aliasing the full-res one. MAG_FILTER is unaffected (mipmapping is
    // a minification-only concept), so close-up walls look identical to
    // before.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);

    m_width = w;
    m_height = h;

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
    m_width = 0;
    m_height = 0;
}
