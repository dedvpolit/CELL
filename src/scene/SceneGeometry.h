#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <functional>
#include "WallShapes.h"

// ============================================================================
// SceneGeometry — построение и владение геометрией подземелья: пол, стены,
// факелы (меши), кнопка победы, а также chunked-разбиение для куллинга
// (см. Culling.h) и PVS (potentially-visible-set, см. build()/buildPVS()).
//
// Вынесено из DungeonScene при разбиении монолита на модули. В отличие от
// MapGenerator/MinimapFog (которые НЕ владеют своими данными), SceneGeometry
// владеет GL-буферами (VAO/VBO/EBO) — как и Compass/AsciiEffect — потому что
// DungeonScene::render() читает их каждый кадр напрямую для draw call'ов,
// так что владение GL-ресурсами естественно остаётся здесь, а не переезжает
// обратно в DungeonScene.
// ============================================================================

// Вынесены из DungeonScene (были приватными вложенными структурами) — теперь
// нужны и в DungeonScene::render() (для чтения m_chunks), и здесь при
// построении, поэтому вынесены на уровень файла.
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    float matId;
};

// Небольшие переиспользуемые строители мешей (цилиндр/сфера) — вынесены
// из static-области SceneGeometry.cpp, чтобы их могли использовать и
// другие модули (см. PlayerTorchViewmodel.h — держит факел в руке
// игрока теми же примитивами, что и настенные факелы, просто пересчитывая
// вершины каждый кадр в мировых координатах относительно камеры).
void AddCylinder(std::vector<Vertex>& verts, std::vector<GLuint>& indices, glm::vec3 base, glm::vec3 tip,
                 float radiusBase, float radiusTip, int segments,
                 glm::vec3 color, float matId);
void AddSphere(std::vector<Vertex>& verts, std::vector<GLuint>& indices, glm::vec3 center, float radius,
               int segments, int rings, glm::vec3 color, float matId);

struct GeoChunk {
    // Item 1: main geometry (walls/floor/torches/win button) is indexed —
    // these are offsets/counts into the shared EBO, not into the vertex buffer.
    GLint   mainIndexFirst = 0;
    GLsizei mainIndexCount = 0;
    // Particles stay non-indexed GL_POINTS (indexing buys nothing for a
    // point cloud), so these are still vertex offsets into the particle VBO.
    GLint   particleFirst = 0;
    GLsizei particleCount = 0;
    glm::vec3 aabbMin{ 0.0f };
    glm::vec3 aabbMax{ 0.0f };
};

class SceneGeometry {
public:
    static const int kChunkSize = 16; // было DungeonScene::CHUNK_SIZE

    // Строит пол/стены/факелы/кнопку победы по карте+данным факелов (см.
    // MapGenerator) и грузит результат в GL (VAO/VBO/EBO + отдельный VAO/VBO
    // для частиц-огня факелов). Разбивает геометрию на чанки kChunkSize x
    // kChunkSize клеток для последующего фрустум/дистанционного куллинга
    // в DungeonScene::render() (см. Culling.h).
    //
    // cornerCuts — результат WallShapes::BuildCornerCuts(mapW,mapH,map,seed)
    // (см. WallShapes.h), размер ОБЯЗАН быть mapW*mapH. Вариативность формы
    // угла клетки стены — Шаг 1 прототипа "неровные стены" (срез угла).
    //
    // columnCentersXZ — мировые XZ-центры свободностоящих колонн (Шаг 2,
    // см. Columns.h::BuildColumns). К моменту вызова build() соответствующие
    // клетки в map уже должны быть полом (BuildColumns мутирует map сам).
    //
    // chamferSizes — размер среза угла ПОКЛЕТОЧНО (mapW*mapH, тот же
    // индекс, что и cornerCuts); обычно WallShapes::kChamferSize (мелкий
    // низкополигональный скос), но клетки диагональных "лестниц" (см.
    // DiagonalCorridors.h) получают WallShapes::kChamferSizeChain (почти
    // половина клетки) — иначе цепочка среза читается как ряд едва
    // заметных царапин на углах, а не как связная диагональная стена (см.
    // историю правок — реальный скриншот в игре показал именно эту
    // проблему).
    //
    // wallColors/floorColors — ГОТОВЫЙ цвет стены/пола ПОКЛЕТОЧНО (а не
    // индекс палитры) — вызывающий код (DungeonScene) уже смешал цвета
    // двух ближайших зон в узкой полосе вокруг границы Voronoi-региона
    // (см. DungeonScene.cpp: BlendedZoneColor()), чтобы переход между
    // соседними регионами был плавным, а не мгновенным скачком палитры
    // ровно на границе клеток (баг: "зелёная стена резко становится
    // красной без градиента"). SceneGeometry сам ничего не знает про
    // Zoning/ZoneGrid — он просто получает уже готовый цвет на клетку,
    // ровно как и с chamferSizes выше. Пустой вектор — все клетки
    // получают палитру 0 (исходный цвет движка до зонирования).
    void build(int mapW, int mapH, const std::vector<int>& map,
               const glm::vec3& winButtonPos,
               const std::vector<glm::vec3>& torchWallBase,
               const std::vector<glm::vec3>& torchNormal,
               const std::vector<glm::vec3>& torchFlamePos,
               const std::vector<WallShapes::CornerCut>& cornerCuts,
               const std::vector<glm::vec2>& columnCentersXZ,
               const std::vector<float>& chamferSizes,
               const std::vector<glm::vec3>& wallColors,
               const std::vector<glm::vec3>& floorColors,
               const std::vector<glm::vec3>& diaryPositions = {});

    // Цвета преднастроенных палитр стена/пол по индексу (см.
    // Zoning::ZoneStyle::paletteIndex) — вынесено в публичный статический
    // метод (а не file-local static в .cpp), т.к. DungeonScene теперь тоже
    // читает эти цвета напрямую при смешивании соседних зон (см.
    // BlendedZoneColor() в DungeonScene.cpp) перед вызовом build() выше.
    static void GetZonePalette(int paletteIndex, glm::vec3& outWallColor, glm::vec3& outFloorColor);

    // Строит consevative PVS (см. GeoChunk-комментарий в старом
    // DungeonScene.h/сейчас здесь): флуд-филл по клеткам пола от каждого
    // чанка, отключается (pvsEnabled()==false) если чанков больше 64 (не
    // влезает в один uint64_t битмаску на чанк) — тогда render() просто не
    // использует PVS-префильтр, откатываясь на фрустум+дистанцию как раньше.
    void buildPVS(int mapW, int mapH, const std::function<bool(int, int)>& isWall);

    void destroy();

    GLuint vao() const { return m_vao; }
    GLuint ebo() const { return m_ebo; }
    GLsizei indexCount() const { return m_indexCount; }

    // Только для тестов/отладки (см. смок-тест колонн): позволяет читать
    // назад CPU-эквивалент загруженных вершин через glGetBufferSubData,
    // не храня отдельную CPU-копию только ради этого.
    GLuint vbo() const { return m_vbo; }
    GLsizei vertexCount() const { return m_vertexCount; }

    GLuint particleVao() const { return m_particleVao; }
    GLsizei particleVertexCount() const { return m_particleVertexCount; }

    const std::vector<GeoChunk>& chunks() const { return m_chunks; }
    int chunksX() const { return m_chunksX; }

    const std::vector<uint64_t>& pvsMask() const { return m_chunkPvsMask; }
    bool pvsEnabled() const { return m_chunkPvsEnabled; }

private:
    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    GLsizei m_vertexCount = 0;
    GLsizei m_indexCount = 0;

    GLuint m_particleVao = 0, m_particleVbo = 0;
    GLsizei m_particleVertexCount = 0;

    std::vector<GeoChunk> m_chunks;
    int m_chunksX = 0;

    std::vector<uint64_t> m_chunkPvsMask;
    bool m_chunkPvsEnabled = false;
};
