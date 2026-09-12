#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include "WallShapes.h"

// ============================================================================
// MinimapFog — fog of war (какие клетки карты игрок уже видел) и две
// GL-текстуры, которые из этого строятся:
//   - "полная карта" (см. mapTexture()) — используется и debug-оверлеем
//     (клавиша M, весь лабиринт целиком, без тумана), и основным
//     фрагментным шейдером сцены (см. assets/shaders/scene.frag, uMode==0
//     для процедурных стен без текстуры);
//   - "мини-карта" (см. minimapTexture()) — N x N клеток вокруг игрока,
//     закрашенных по m_explored, рисуется на компасе (см. Compass.h/.cpp).
//
// Вынесено из DungeonScene при разбиении монолита на модули. В отличие
// от WallTexture, эта подсистема НЕ владеет исходными данными карты
// (m_map/m_mapW/m_mapH остаются в DungeonScene, будущий MapGenerator) —
// вместо этого её методы принимают нужные данные параметрами (карту,
// позицию/поворот камеры, предикат isWall, готовый lookup факелов).
// Такое разделение позволяет вынести GL-текстуры и логику "тумана"
// прямо сейчас, не дожидаясь отдельного шага по переносу владения
// самой картой.
// ============================================================================
class MinimapFog {
public:
    static const int kMinimapSize = 21; // было DungeonScene::MINIMAP_SIZE

    // (Пере)загружает m_mapTexture из текущего состояния map — ПОЛНАЯ
    // карта, без учёта fog of war (uMode==0 в scene.frag читает готовые
    // значения "стена/не стена", тумана там нет; сам туман применяется
    // только к мини-карте, см. updateMinimap() ниже).
    //
    // Текстура теперь GL_RGBA8 (была GL_RGB8): R — как раньше, 1.0=стена/
    // 0.0=пол; G — тип среза угла клетки (WallShapes::CornerCut, см.
    // WallShapes.h), закодирован как ЦЕЛОЕ значение 0..4 в байте (не
    // масштабировано под диапазон — шейдер сам делает round(g*255.0),
    // чтобы получить обратно точное целое); B — стала ли эта floor-клетка
    // полом ИМЕННО от расширения коридора (см. CorridorWidth.h), 1.0/0.0;
    // A — форсирован ли этот срез угла как часть диагональной "лестницы"
    // (см. DiagonalCorridors.h), 1.0/0.0 — отличает "срезано по цепочке"
    // от "срезано по обычной вероятности региона" НА ГЛАЗ на debug-карте,
    // иначе цепочка ничем не отличалась бы от одиночного случайного среза.
    // Нужно шейдеру scene.frag (shadowedByWall()) — только R и G, B/A там
    // не используются — и debug_map.frag — там нужны все четыре.
    void uploadMapTexture(int mapW, int mapH, const std::vector<int>& map,
                          const std::vector<WallShapes::CornerCut>& cornerCuts,
                          const std::vector<unsigned char>& corridorWidened,
                          const std::vector<unsigned char>& diagonalChainMask);

    // Раскрывает клетки вокруг игрока и по конусу обзора (FOV) в
    // m_explored — эффект "тумана войны": однажды увиденная клетка
    // остаётся раскрытой навсегда (не гаснет обратно), пока приложение
    // не будет перезапущено (m_explored и полная карта пересоздаются
    // заново только в generateMap()/init()).
    void revealVisibleCells(int mapW, int mapH,
                             const glm::vec3& camPos, float yaw,
                             const std::function<bool(int, int)>& isWall);

    // Вызывает revealVisibleCells(), затем перестраивает m_minimapTexture
    // (N x N клеток вокруг игрока) с учётом текущего m_explored и
    // переданного lookup факелов (см. DungeonScene::buildTorchCellLookup()).
    void updateMinimap(int mapW, int mapH, const std::vector<int>& map,
                        const glm::vec3& camPos, float yaw,
                        const std::function<bool(int, int)>& isWall,
                        const std::vector<unsigned char>& torchCellLookup);

    void destroy();

    GLuint mapTexture() const { return m_mapTexture; }
    GLuint minimapTexture() const { return m_minimapTexture; }

    // Сбрасывает "туман" — вызывается из generateMap() при генерации
    // новой карты (та же самая карта на всю сессию, но метод существует
    // на случай будущей повторной генерации/рестарта).
    void resetExplored(int mapW, int mapH) {
        m_explored.assign((size_t)mapW * mapH, 0);
    }

    // ---- Сохранение/загрузка (см. save/SaveSystem.h) ----
    // Позволяет забрать текущий "туман" целиком (для записи в файл
    // сохранения) и вернуть его назад при "Продолжить" — иначе после
    // загрузки старого сейва игрок видел бы на мини-карте только то,
    // что раскрыто с текущего кадра (resetExplored()), а не всё, что
    // он уже успел исследовать в этом прохождении.
    const std::vector<unsigned char>& explored() const { return m_explored; }
    void setExplored(std::vector<unsigned char> explored) {
        m_explored = std::move(explored);
    }

private:
    GLuint m_mapTexture = 0;
    GLuint m_minimapTexture = 0;

    // 0 = не раскрыта, 1 = раскрыта. Индекс тот же, что и у map: z*mapW+x.
    std::vector<unsigned char> m_explored;

    // Perf: persistent scratch buffer for updateMinimap()'s pixel data —
    // was a fresh std::vector<unsigned char> heap-allocated and freed
    // EVERY call (every frame), same anti-pattern as DungeonScene's
    // render() scratch buffers before those were fixed (see
    // reserveRenderScratchBuffers() there). kMinimapSize*kMinimapSize is
    // tiny (441 bytes) so this alone was never going to be the dominant
    // cost, but it's a real per-frame malloc/free for zero benefit.
    std::vector<unsigned char> m_minimapPixels;

    // Perf: once the minimap texture exists at its (fixed, kMinimapSize)
    // resolution, re-uploads should use glTexSubImage2D (overwrite
    // existing GPU storage) instead of glTexImage2D (reallocate GPU
    // storage from scratch every single frame). True only after the
    // first successful upload.
    bool m_minimapTextureAllocated = false;
};
