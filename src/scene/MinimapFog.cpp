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

    // 4 байта на тексель: R=стена(255)/пол(0), G=тип среза угла (0..4,
    // сырое целое), B=расширен ли коридор именно здесь (0/255), A=часть
    // ли диагональной цепочки (0/255) — см. комментарии в MinimapFog.h.
    // Любой из векторов может быть пуст (старый вызывающий код до
    // соответствующего шага) — тогда соответствующий канал всегда 0, что
    // даёт прежнее поведение.
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

// ---------------- Fog of war ----------------
//
// [comment corrupted in source file - original text lost/unrecoverable]
// [comment corrupted in source file - original text lost/unrecoverable]
// [comment corrupted in source file - original text lost/unrecoverable]
// [comment corrupted in source file - original text lost/unrecoverable]
// [comment corrupted in source file - original text lost/unrecoverable]
//
// [comment corrupted in source file - original text lost/unrecoverable]
// [comment corrupted in source file - original text lost/unrecoverable]
// [comment corrupted in source file - original text lost/unrecoverable]
// [comment corrupted in source file - original text lost/unrecoverable]
void MinimapFog::revealVisibleCells(int mapW, int mapH,
                                     const glm::vec3& camPos, float yaw,
                                     const std::function<bool(int, int)>& isWall)
{
    static constexpr float FOV_DEG     = 75.0f;  // угол конуса обзора, градусы
    static constexpr float VIEW_RADIUS = 9.0f;   // дальность в клетках карты

    static constexpr int   RAY_COUNT = 64;  // сколько направлений лучей
    static constexpr float STEP      = 0.2f; // шаг вдоль луча, в клетках

    // Клетка под ногами всегда открыта, независимо от того,
    // куда во направлен взгляд.
    {
        int pcx = (int)std::floor(camPos.x);
        int pcz = (int)std::floor(camPos.z);
        if (pcx >= 0 && pcx < mapW && pcz >= 0 && pcz < mapH)
            m_explored[(size_t)pcz * mapW + pcx] = 1;
    }

    // Небольшой радиус вокруг игрока раскрывается БЕЗУСЛОВНО, независимо
    // от направления взгляда (yaw) — иначе стены/факелы сбоку или сзади
    // от текущего направления камеры (вне конуса FOV_DEG ниже) никогда
    // не попадают ни под один луч и остаются "неразведанными", хотя
    // игрок физически стоит прямо рядом с ними. Раньше это давало
    // впечатление, что факелы на мини-карте появляются то тут, то там
    // "случайно" — на самом деле это просто зависело от того, куда в
    // конкретный момент была повёрнута камера. Радиус небольшой (в
    // клетках карты) — намеренно не такой, как VIEW_RADIUS у конуса
    // обзора, чтобы не открывать всю карту вокруг, только непосредственно
    // прилегающие стены.
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
    // Тот же порядок операций, что и у getFront()/расчёта фронта на мини-карте
    // в AsciiEffect: направление = (cos(yaw), sin(yaw)) в плоскости XZ.
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
                break;  // [comment corrupted in source file - original text lost/unrecoverable]
        }
    }
}

// [comment corrupted in source file - original text lost/unrecoverable]

void MinimapFog::updateMinimap(int mapW, int mapH, const std::vector<int>& map,
                                const glm::vec3& camPos, float yaw,
                                const std::function<bool(int, int)>& isWall,
                                const std::vector<unsigned char>& torchCellLookup)
{
    // [comment corrupted in source file - original text lost/unrecoverable]
    // [comment corrupted in source file - original text lost/unrecoverable]
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

    // [comment corrupted in source file - original text lost/unrecoverable]
    int pcx = (int)std::floor(camPos.x);
    int pcz = (int)std::floor(camPos.z);

    // Perf: persistent member buffer instead of a fresh heap allocation
    // every call (every frame) — see m_minimapPixels declaration.
    // .resize() is a no-op after the first call (size never changes,
    // kMinimapSize is a compile-time constant), so this doesn't
    // reallocate in steady state.
    m_minimapPixels.resize((size_t)N * N);
    std::vector<unsigned char>& pixels = m_minimapPixels;

    // [comment corrupted in source file - original text lost/unrecoverable]
    // O(1) lookup built once in buildTorchCellLookup() (see placeTorches())
    // instead of rescanning the whole torch list (up to MAX_TORCHES) for
    // every one of the N*N minimap cells, every frame.
    auto isTorchCell = [&](int mx, int mz)
    {
        if (mx < 0 || mx >= mapW || mz < 0 || mz >= mapH)
            return false;
        return torchCellLookup[(size_t)mz * mapW + mx] != 0;
    };

    for (int j = 0; j < N; ++j)
    {
        for (int i = 0; i < N; ++i)
        {
            int mx = pcx - half + i;
            int mz = pcz - half + j;

            // [comment corrupted in source file - original text lost/unrecoverable]
            // [comment corrupted in source file - original text lost/unrecoverable]
            // [comment corrupted in source file - original text lost/unrecoverable]
            // [comment corrupted in source file - original text lost/unrecoverable]
            unsigned char v = 0;

            bool inBounds =
                mx >= 0 && mx < mapW &&
                mz >= 0 && mz < mapH;

            // [comment corrupted in source file - original text lost/unrecoverable]
            // [comment corrupted in source file - original text lost/unrecoverable]
            // [comment corrupted in source file - original text lost/unrecoverable]
            // [comment corrupted in source file - original text lost/unrecoverable]
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

                if (v == 170 && isTorchCell(mx, mz))
                    v = 255;
            }

            pixels[(size_t)j * N + i] = v;
        }
    }

    glBindTexture(
        GL_TEXTURE_2D,
        m_minimapTexture
    );

    // Perf: glTexImage2D reallocates GPU storage for the texture from
    // scratch — was being called every single frame even though N (=
    // kMinimapSize) never changes after the very first upload. Now only
    // the FIRST call allocates storage (glTexImage2D); every call after
    // that just overwrites the existing storage in place
    // (glTexSubImage2D), which is what every frame after the first
    // actually needs. Sampler parameters are texture STATE, not pixel
    // data — they don't need to be (and previously were being)
    // re-specified on every single upload either, only once right after
    // the texture is created.
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
    // The next updateMinimap() call will glGenTextures() a brand new
    // texture object (m_minimapTexture is now 0) — it has no storage
    // yet, so the allocate-vs-subimage flag must be reset too, or the
    // very first upload after a destroy()/re-init would wrongly try
    // glTexSubImage2D on a texture that was never glTexImage2D'd.
    m_minimapTextureAllocated = false;
}
