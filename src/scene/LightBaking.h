#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include "WallShapes.h"

// ============================================================================
// STATUS: Bake()/BakedVisibility ниже — NOT CURRENTLY USED (см. историю: баг
// с GL_R8 normalized-vs-integer текстурой, не удалось проверить без GPU).
// Движок использует оригинальный, доказанно корректный shadowedByWall() в
// scene.frag для света факелов. Но HasLineOfSight() (ниже) — ОБЩАЯ, отдельная
// от факелов функция того же самого трассировщика — АКТИВНО используется
// EnemyAI (см. EnemyAI.h) для проверки видимости враг↔игрок.
//
// Если Bake() будет когда-нибудь возвращён в дело, найденный (но
// непереприменённый) фикс: заливать текстуру в формате GL_R8UI/
// GL_RED_INTEGER, а не GL_R8/GL_RED, и сэмплировать через usampler2DArray
// в GLSL, а не sampler2DArray — GL_R8 нормализованный формат, поэтому
// сохранённый байт 1 читается как 1/255 в float-шейдере, а не как 1.0, что
// тихо ломало каждую проверку "видимо" в `vis < 0.5`.
// ============================================================================

// ============================================================================
// LightBaking — precomputes, ONCE per level load, whether each map cell is in
// line-of-sight of each torch. This is the exact same geometric test as
// shadowedByWall() in assets/shaders/scene.frag (grid DDA + corner-cut
// half-plane test + column ray-vs-circle test), just run on the CPU, once,
// against every (torch, cell) pair, instead of on the GPU, every fragment,
// every frame.
//
// Why this is a valid trade: the map geometry AND torch positions are both
// static after generation (see MapGenerator::PlaceTorches — torches are
// placed once and never move). Only the FLICKER (time-based brightness) is
// dynamic, and that stays exactly as-is in the fragment shader. Only the
// binary "can this point see that torch" test moves off the GPU.
//
// Precision trade-off: visibility is baked at 1 sample per map cell, so the
// shadow boundary becomes quantized to the map grid instead of the previous
// continuous (dithered) per-pixel edge. Given the final output is crushed to
// ASCII glyphs anyway, this is expected to be visually indistinguishable in
// practice; if it isn't, kSubCellsPerAxis below can be raised (e.g. to 2 or
// 4) for a smoother boundary at a proportional (still tiny) memory cost.
// ============================================================================
namespace LightBaking {

// Sub-cell sampling resolution per map cell per axis. 1 = one sample at the
// cell center (cheapest, matches the granularity most tests need). Raise
// this if a baked shadow edge ever looks noticeably more "blocky" than the
// old per-pixel raymarch did.
constexpr int kSubCellsPerAxis = 1;

struct BakedVisibility {
    int mapW = 0;
    int mapH = 0;
    int subRes = 0;      // mapW * kSubCellsPerAxis (== mapH * kSubCellsPerAxis)
    int torchCount = 0;
    // Flattened as: data[torch * subRes*subResH + subZ * subResW + subX]
    // 1 = lit (line of sight to torch), 0 = shadowed. One byte per texel so
    // it uploads directly as a GL_R8 GL_TEXTURE_2D_ARRAY layer.
    std::vector<uint8_t> data;

    bool valid() const { return !data.empty(); }
};

// mapW,mapH        — map dimensions (same as DungeonScene::m_mapW/H)
// map              — DungeonScene::m_map (1 = wall, everything else = not)
// cornerCuts       — DungeonScene::m_wallCornerCuts, same indexing as map
// diagonalChainMask— DungeonScene::m_diagonalChainMask, same indexing as map
//                    (drives the 0.35 vs 0.92 chamfer size, exactly mirroring
//                    the cellData.a branch in shadowedByWall())
// columnCentersXZ  — DungeonScene::m_columnCentersXZ
// columnRadius     — Columns::kColumnRadius
// torchFlamePos    — ALL placed torches (DungeonScene::m_torchFlamePos), not
//                    just the "active" nearest-N subset — the bake is keyed
//                    by permanent torch index so the active-set reshuffling
//                    that already happens every frame in Lighting::update()
//                    just needs to carry the permanent index through.
BakedVisibility Bake(
    int mapW, int mapH,
    const std::vector<int>& map,
    const std::vector<WallShapes::CornerCut>& cornerCuts,
    const std::vector<unsigned char>& diagonalChainMask,
    const std::vector<glm::vec2>& columnCentersXZ,
    float columnRadius,
    const std::vector<glm::vec3>& torchFlamePos);

// Разовая (не запечённая, считается по требованию) проверка прямой
// видимости между двумя произвольными точками мира — тот же алгоритм
// (грид-DDA + срезанные углы + колонны), что и внутри Bake()/
// shadowedByWall() в scene.frag, просто вызываемый напрямую, без
// кэширования на карту. Для EnemyAI (восприятие игрока) — расстояния,
// на которых это нужно, требуют максимум пару вызовов в секунду (см.
// EnemyAI.h), так что отдельный запечённый кэш здесь избыточен.
bool HasLineOfSight(
    const glm::vec2& fromXZ,
    const glm::vec2& toXZ,
    int mapW, int mapH,
    const std::vector<int>& map,
    const std::vector<WallShapes::CornerCut>& cornerCuts,
    const std::vector<unsigned char>& diagonalChainMask,
    const std::vector<glm::vec2>& columnCentersXZ,
    float columnRadius);

} // namespace LightBaking
