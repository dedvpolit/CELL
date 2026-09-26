#include "MinimapFog.h"
#include <cmath>

void MinimapFog::uploadMapTexture(int mapW, int mapH, const std::vector<int>& map,
                                  const std::vector<WallShapes::CornerCut>& cornerCuts,
                                  const std::vector<unsigned char>& corridorWidened,
                                  const std::vector<unsigned char>& diagonalChainMask)
{
    if (m_mapTexture == 0)
    {
        glGenTextures(
            1,
            &m_mapTexture
        );
    }

    // 4 bytes per texel: R = wall (255) / floor (0), G = corner-cut type (0..4, raw integer), B =
    // corridor widened here (0/255), A = part of a diagonal chain (0/255); see MinimapFog.h. Any of
    // the vectors can be empty; the corresponding channel is then 0.
    const bool hasCuts = cornerCuts.size() == (size_t)mapW * (size_t)mapH;
    const bool hasWidened = corridorWidened.size() == (size_t)mapW * (size_t)mapH;
    const bool hasChains = diagonalChainMask.size() == (size_t)mapW * (size_t)mapH;

    std::vector<unsigned char> pixels(
        (size_t)mapW * mapH * 4,
        0
    );

    for (int z = 0;
         z < mapH;
         ++z)
    {
        for (int x = 0;
             x < mapW;
             ++x)
        {
            const size_t cellIdx = (size_t)z * mapW + x;
            pixels[cellIdx * 4 + 0] =
                (map[z * mapW + x] == 1)
                ? 255
                : 0;
            pixels[cellIdx * 4 + 1] =
                hasCuts
                ? (unsigned char)cornerCuts[cellIdx]
                : 0;
            pixels[cellIdx * 4 + 2] =
                (hasWidened && corridorWidened[cellIdx])
                ? 255
                : 0;
            pixels[cellIdx * 4 + 3] =
                (hasChains && diagonalChainMask[cellIdx])
                ? 255
                : 0;
        }
    }

    glBindTexture(
        GL_TEXTURE_2D,
        m_mapTexture
    );

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        mapW,
        mapH,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels.data()
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        GL_NEAREST
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MAG_FILTER,
        GL_NEAREST
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_S,
        GL_CLAMP_TO_EDGE
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        GL_CLAMP_TO_EDGE
    );

    glBindTexture(
        GL_TEXTURE_2D,
        0
    );
}

void MinimapFog::revealVisibleCells(int mapW, int mapH,
                                     const glm::vec3& camPos, float yaw,
                                     const std::function<bool(int, int)>& isWall)
{
    static constexpr float FOV_DEG     = 75.0f;  // FOV cone angle, degrees
    static constexpr float VIEW_RADIUS = 9.0f;   // range in map cells

    static constexpr int   RAY_COUNT = 64;  // number of ray directions
    static constexpr float STEP      = 0.2f; // step along the ray, in cells

    {
        int pcx = (int)std::floor(camPos.x);
        int pcz = (int)std::floor(camPos.z);
        if (pcx >= 0 && pcx < mapW && pcz >= 0 && pcz < mapH)
            m_explored[(size_t)pcz * mapW + pcx] = 1;
    }

    // A small radius around the player is revealed regardless of yaw: otherwise walls beside or
    // behind the camera (outside the FOV cone) would stay unexplored although the player stands
    // next to them. It is smaller than VIEW_RADIUS, so only adjacent walls are revealed.
    static constexpr float NEARBY_RADIUS = 2.5f;
    {
        int pcx = (int)std::floor(camPos.x);
        int pcz = (int)std::floor(camPos.z);

        const int r = (int)std::ceil(NEARBY_RADIUS);
        for (int dz = -r; dz <= r; ++dz)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                if ((float)(dx * dx + dz * dz) > NEARBY_RADIUS * NEARBY_RADIUS)
                    continue;

                int cx = pcx + dx;
                int cz = pcz + dz;
                if (cx < 0 || cx >= mapW || cz < 0 || cz >= mapH)
                    continue;

                m_explored[(size_t)cz * mapW + cx] = 1;
            }
        }
    }

    const float halfFovRad = glm::radians(FOV_DEG * 0.5f);
    // Same operation order as getFront() and the minimap front calculation in AsciiEffect:
    // direction = (cos(yaw), sin(yaw)) in the XZ plane.
    const float yawRad = glm::radians(yaw);

    for (int r = 0; r <= RAY_COUNT; ++r)
    {
        float t = (float)r / (float)RAY_COUNT; // 0..1
        float angle = yawRad - halfFovRad + t * (2.0f * halfFovRad);

        float dx = std::cos(angle);
        float dz = std::sin(angle);

        float dist = 0.0f;
        while (dist < VIEW_RADIUS)
        {
            dist += STEP;

            float wx = camPos.x + dx * dist;
            float wz = camPos.z + dz * dist;

            int cx = (int)std::floor(wx);
            int cz = (int)std::floor(wz);

            if (cx < 0 || cx >= mapW || cz < 0 || cz >= mapH)
                break;

            m_explored[(size_t)cz * mapW + cx] = 1;

            if (isWall(cx, cz))
                break;
        }
    }
}

void MinimapFog::updateMinimap(int mapW, int mapH, const std::vector<int>& map,
                                const glm::vec3& camPos, float yaw,
                                const std::function<bool(int, int)>& isWall,
                                const std::vector<unsigned char>& torchCellLookup,
                                const std::vector<unsigned char>& guideTorchCellLookup,
                                const std::vector<unsigned char>& diaryReadWallLookup)
{
    revealVisibleCells(mapW, mapH, camPos, yaw, isWall);

    if (m_minimapTexture == 0)
    {
        glGenTextures(
            1,
            &m_minimapTexture
        );
    }

    const int N = kMinimapSize;
    const int half = N / 2;

    int pcx = (int)std::floor(camPos.x);
    int pcz = (int)std::floor(camPos.z);

    // Persistent member buffer instead of a fresh heap allocation on every call (every frame):
    // resize() is a no-op after the first call because the size never changes (kMinimapSize is a
    // compile-time constant).
    m_minimapPixels.resize((size_t)N * N);
    std::vector<unsigned char>& pixels = m_minimapPixels;

    // O(1) lookup built once (MapGenerator::BuildTorchCellLookup()) instead of rescanning the whole
    // torch list for each of the N*N minimap cells every frame.
    auto isTorchCell = [&](int mx, int mz)
    {
        if (mx < 0 || mx >= mapW || mz < 0 || mz >= mapH)
            return false;
        return torchCellLookup[(size_t)mz * mapW + mx] != 0;
    };

    // Landmark torches: an empty vector is valid (landmarks not built yet) and means false
    // everywhere, instead of being indexed out of bounds.
    auto isGuideTorchCell = [&](int mx, int mz)
    {
        if (guideTorchCellLookup.empty())
            return false;
        if (mx < 0 || mx >= mapW || mz < 0 || mz >= mapH)
            return false;
        return guideTorchCellLookup[(size_t)mz * mapW + mx] != 0;
    };

    auto isDiaryReadWallCell = [&](int mx, int mz)
    {
        if (diaryReadWallLookup.empty())
            return false;
        if (mx < 0 || mx >= mapW || mz < 0 || mz >= mapH)
            return false;
        return diaryReadWallLookup[(size_t)mz * mapW + mx] != 0;
    };

    for (int j = 0; j < N; ++j)
    {
        for (int i = 0; i < N; ++i)
        {
            int mx = pcx - half + i;
            int mz = pcz - half + j;

            unsigned char v = 0;

            bool inBounds =
                mx >= 0 && mx < mapW &&
                mz >= 0 && mz < mapH;

            bool revealed =
                inBounds &&
                m_explored[(size_t)mz * mapW + mx] != 0;

            if (revealed)
            {
                int cell = map[mz * mapW + mx];

                if (cell == 1)
                    v = 170;
                else if (cell == 2)
                    v = 85;

                // Final priority is decided below: landmark torch (220, the same '*' as a regular
                // torch but tinted) > read-diary highlight (200) > regular torch (255) > regular
                // wall (170). A regular torch must not beat the highlight: a tight pocket has only
                // 2-3 perimeter wall cells but 3 torches, so the highlight would almost never show.
                if (v == 170 && isGuideTorchCell(mx, mz))
                    v = 220;
                else if (v == 170 && isDiaryReadWallCell(mx, mz))
                    v = 200;
                else if (v == 170 && isTorchCell(mx, mz))
                    v = 255;
            }

            pixels[(size_t)j * N + i] = v;
        }
    }

    glBindTexture(
        GL_TEXTURE_2D,
        m_minimapTexture
    );

    // glTexImage2D reallocates GPU storage, so only the first call allocates it; later calls
    // overwrite it in place (glTexSubImage2D). Sampler parameters are texture state, not pixel
    // data, so they are set once right after the texture is created, not on every upload.
    if (!m_minimapTextureAllocated)
    {
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_R8,
            N,
            N,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            pixels.data()
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MIN_FILTER,
            GL_NEAREST
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MAG_FILTER,
            GL_NEAREST
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_S,
            GL_CLAMP_TO_EDGE
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_T,
            GL_CLAMP_TO_EDGE
        );

        m_minimapTextureAllocated = true;
    }
    else
    {
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            N,
            N,
            GL_RED,
            GL_UNSIGNED_BYTE,
            pixels.data()
        );
    }

    glBindTexture(
        GL_TEXTURE_2D,
        0
    );
}

void MinimapFog::destroy()
{
    if (m_mapTexture) {
        glDeleteTextures(1, &m_mapTexture);
        m_mapTexture = 0;
    }
    if (m_minimapTexture) {
        glDeleteTextures(1, &m_minimapTexture);
        m_minimapTexture = 0;
    }
    // The next updateMinimap() call creates a brand new texture object (m_minimapTexture is now 0)
    // with no storage yet, so the allocate-vs-subimage flag must be reset too. Otherwise the first
    // upload after destroy()/re-init would call glTexSubImage2D on a texture that never got
    // storage.
    m_minimapTextureAllocated = false;
}
