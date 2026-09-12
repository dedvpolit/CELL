#include "DungeonScene.h"
#include "render/ShaderLoader.h"
#include "render/ShaderProgram.h"
#include "WallTexture.h"
#include "MapGenerator.h"
#include "LightBaking.h"
#include "save/SaveSystem.h"
#include "ui/MainMenu.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <cstdio>
#include <random>
#include <algorithm>
#include <filesystem>

const int DungeonScene::MAX_TORCHES;
const int DungeonScene::MAX_ACTIVE_TORCHES;

// [comment corrupted in source file - original text lost/unrecoverable]

// Исходники GLSL для сцены (walls/floor/torches/particles) вынесены в
// assets/shaders/scene.{vert,frag} и читаются с диска в init() через
// ShaderLoader — раньше были встроены здесь как raw-string литералы.

// ================================================================
// DEBUG FULL-MAP OVERLAY (клавиша M)
// Простой шейдер для отладочной панели с полной картой лабиринта:
// либо сэмплирует m_mapTexture (uMode==0), либо просто заливает
// плоским цветом (uMode!=0) — используется и для фона панели, и для
// маркера игрока. Все вершины уже приходят в NDC-координатах,
// посчитанных на CPU в renderDebugMap(), поэтому вершинный шейдер
// тривиален.
// ================================================================
// Исходники вынесены в assets/shaders/debug_map.{vert,frag} (см. init()
// сцены /createDebugMapResources() ниже, ShaderLoader::LoadSource()).

// [comment corrupted in source file - original text lost/unrecoverable]


GLuint DungeonScene::compileShader(GLenum type, const char* src) {
    return ShaderProgram::CompileShader(type, src, "DungeonScene");
}

GLuint DungeonScene::linkProgram(GLuint vs, GLuint fs) {
    return ShaderProgram::LinkProgram(vs, fs, "DungeonScene");
}

// [comment corrupted in source file - original text lost/unrecoverable]

bool DungeonScene::isWall(int x, int z) const {
    if (x < 0 || x >= m_mapW || z < 0 || z >= m_mapH) return true;
    return m_map[z * m_mapW + x] == 1;
}

bool DungeonScene::isFloor(int x, int z) const {
    if (x < 0 || x >= m_mapW || z < 0 || z >= m_mapH) return false;
    return m_map[z * m_mapW + x] == 2;
}

WallShapes::CornerCut DungeonScene::wallCornerCut(int x, int z) const {
    if (x < 0 || x >= m_mapW || z < 0 || z >= m_mapH) return WallShapes::CornerCut::None;
    // m_wallCornerCuts заполняется в generateMap() сразу после m_map —
    // до этого (не должно случаться в обычном потоке управления) вектор
    // пуст, поэтому проверяем размер на всякий случай, а не индексируем
    // вслепую.
    if (m_wallCornerCuts.size() != (size_t)m_mapW * (size_t)m_mapH) return WallShapes::CornerCut::None;
    return m_wallCornerCuts[(size_t)z * m_mapW + x];
}

float DungeonScene::wallChamferSize(int x, int z) const {
    if (x < 0 || x >= m_mapW || z < 0 || z >= m_mapH) return WallShapes::kChamferSize;
    if (m_chamferSizes.size() != (size_t)m_mapW * (size_t)m_mapH) return WallShapes::kChamferSize;
    return m_chamferSizes[(size_t)z * m_mapW + x];
}

void DungeonScene::setCompassMinimapFont(
    GLuint texture,
    int glyphCount
)
{
    m_compass.setMinimapFont(texture, glyphCount);
}

// ---------------- Генерация лабиринта / факелов (см. MapGenerator.h) ----------------
// Сама генерация вынесена в MapGenerator — здесь только распаковка
// результата в поля DungeonScene (данные карты/факелов по-прежнему
// хранятся здесь, т.к. читаются каждый кадр геометрией/светом/коллизиями).

// Forward declaration — определение см. ниже в этом файле (после
// placeTorches()); нужна здесь, т.к. generateMap() (сразу ниже) уже её
// вызывает, а C++ не видит функции, объявленные позже по файлу.
static void BlendedZoneColor(const Zoning::ZoneGrid& zoneGrid, int cellX, int cellZ,
                              glm::vec3& outWallColor, glm::vec3& outFloorColor);

void DungeonScene::generateMap(unsigned int seed)
{
    MapGenerator::GenerateResult result = MapGenerator::Generate(seed);

    m_mapW = result.mapW;
    m_mapH = result.mapH;
    m_map = std::move(result.map);

    // Зонирование (см. Zoning.h) — грубые прямоугольные сектора, у
    // каждого свой профиль (вероятность среза угла / колонны / ширины
    // коридора). Строится ПЕРВЫМ, до всего остального ниже — все они
    // читают его через лямбды просто как источник вероятности для своей
    // клетки, вместо единой глобальной константы. Хранится в m_zoneGrid
    // (не локальная переменная) — нужен ещё и debug-карте (клавиша M)
    // для отрисовки границ/профиля секторов, см. DungeonScene.h.
    m_zoneGrid = Zoning::BuildZoneGrid(m_mapW, m_mapH, seed);

    // Ширина коридоров (см. CorridorWidth.h) — идёт ПЕРВОЙ из трёх
    // мутаций грида (перед колоннами и срезами): снос "стен-перегородок"
    // меняет, какие клетки вообще открыты, а значит и что именно
    // eligible для Columns/WallShapes ниже — они должны видеть уже
    // расширенный грид, а не наоборот.
    m_corridorWidened = CorridorWidth::ApplyWidening(
        m_mapW, m_mapH, m_map, seed,
        [this](int x, int z) { return Zoning::StyleAt(m_zoneGrid, x, z).widenProbability; });

    // Свободностоящие колонны (Шаг 2, см. Columns.h) — ОТКЛЮЧЕНЫ ПО
    // ЗАПРОСУ ("давай на совсем уберём колоны из игры"): несмотря на
    // несколько последовательных фиксов (исключение их клеток из
    // path-поиска, доп. коллайдер, общий анти-стак предохранитель — см.
    // историю правок в EnemyAI), ИИ всё равно иногда застревал у них —
    // реальная зона коллизии столба (kCollisionRadius врага 0.35 +
    // Columns::kColumnRadius 0.28 = 0.63) шире половины клетки (0.5),
    // так что она задевает и соседние, формально "разрешённые" клетки.
    // Проще и надёжнее вообще убрать источник проблемы, чем продолжать
    // гоняться за каждым отдельным геометрическим случаем.
    //
    // Сделано именно ТАК (просто не вызывать BuildColumns()), а не
    // удалением всего файла Columns.h/.cpp и всех мест, что на него
    // ссылаются (рендер колонн, коллизия игрока, EnemyAI::
    // m_pathfindingMap и т.д.) — m_columnCentersXZ просто остаётся
    // пустым вектором, и весь остальной код уже КОРРЕКТНО обрабатывает
    // пустой список колонн (пустые циклы, columnCount=0 в шейдерных
    // uniform'ах и т.п.) — никаких дополнительных правок в других
    // местах не потребовалось. BuildColumns() мутировал m_map (превращал
    // клетки-кандидаты в пол под колонну) — раз он не вызывается, эти
    // клетки просто остаются обычными стенами, как и было бы без всей
    // этой подсистемы, включая WallShapes::BuildCornerCuts() ниже,
    // который увидит их как рядовые стены и при необходимости даст им
    // обычный срез угла, никак не отличая от любой другой стены.
    //
    // Если понадобится вернуть колонны — единственная нужная правка это
    // раскомментировать вызов ниже, ничего больше трогать не нужно.
    //
    // m_columnCentersXZ = Columns::BuildColumns(
    //     m_mapW, m_mapH, m_map, seed,
    //     [this](int x, int z) { return Zoning::StyleAt(m_zoneGrid, x, z).columnProbability; });
    m_columnCentersXZ.clear();

    // Срезанные углы стен (Шаг 1 "неровные стены", см. WallShapes.h) —
    // тот же seed, что и у самого лабиринта выше, так что "Продолжить"
    // (загрузка сохранённого seed) детерминированно восстанавливает те
    // же срезы, не требуя хранить их отдельно в сейве.
    //
    // Диагональные "лестницы" (пункт 3 второго документа ТЗ, см.
    // DiagonalCorridors.h) — ищем ПОСЛЕ ширины/колонн (нужен финальный
    // грид), но ДО решения WallShapes: обёрнутая вероятность форсирует
    // 1.0 для клеток из достаточно длинной цепочки, иначе — обычный
    // профиль зоны без изменений. Так цепочка среза срезается целиком
    // (читается как грубая диагональ), а не редко и вразнобой.
    m_diagonalChainMask = DiagonalCorridors::DetectChains(m_mapW, m_mapH, m_map);
    m_wallCornerCuts = WallShapes::BuildCornerCuts(
        m_mapW, m_mapH, m_map, seed,
        [this](int x, int z) {
            const size_t idx = (size_t)z * m_mapW + x;
            if (idx < m_diagonalChainMask.size() && m_diagonalChainMask[idx]) return 1.0f;
            return Zoning::StyleAt(m_zoneGrid, x, z).chamferProbability;
        });

    // Размер среза поклеточно (см. m_chamferSizes в DungeonScene.h) —
    // строится ПОСЛЕ m_wallCornerCuts/m_diagonalChainMask: клетки из
    // цепочки получают WallShapes::kChamferSizeChain (иначе цепочка среза
    // читается как ряд едва заметных царапин, а не связная диагональ, см.
    // историю правок), все прочие срезанные клетки — обычный
    // WallShapes::kChamferSize.
    // Размер среза поклеточно (см. m_chamferSizes в DungeonScene.h) —
    // строится ПОСЛЕ m_wallCornerCuts/m_diagonalChainMask: клетки из
    // цепочки получают WallShapes::kChamferSizeChain (иначе цепочка среза
    // читается как ряд едва заметных царапин, а не связная диагональ, см.
    // историю правок), а ВСЕ ОСТАЛЬНЫЕ срезанные клетки — каждая СВОЙ
    // размер в диапазоне kChamferSizeVariedMin..Max (см. WallShapes.h,
    // "переменная длина среза на клетку" из ТЗ) вместо одного фиксированного
    // значения на всю карту — небольшое дополнительное разнообразие силуэта
    // поверх уже имеющейся вариативности "срезать/не срезать".
    m_chamferSizes.assign((size_t)m_mapW * (size_t)m_mapH, WallShapes::kChamferSize);
    for (int z = 0; z < m_mapH; ++z) {
        for (int x = 0; x < m_mapW; ++x) {
            const size_t idx = (size_t)z * m_mapW + x;
            if (m_wallCornerCuts[idx] == WallShapes::CornerCut::None) continue;
            m_chamferSizes[idx] = WallShapes::ComputeVariedChamferSize(seed, x, z);
        }
    }
    for (size_t i = 0; i < m_diagonalChainMask.size() && i < m_chamferSizes.size(); ++i) {
        if (m_diagonalChainMask[i]) m_chamferSizes[i] = WallShapes::kChamferSizeChain;
    }

    // Палитра по зоне поклеточно (см. m_paletteWallColors/m_paletteFloorColors
    // в DungeonScene.h) — теперь это ГОТОВЫЙ цвет (BlendedZoneColor(), см.
    // выше в этом файле), плавно смешанный между двумя ближайшими регионами
    // вблизи их общей Voronoi-границы, а не жёсткий индекс палитры только
    // ближайшего (тот старый способ давал мгновенный скачок цвета ровно на
    // границе регионов — см. историю правок с реальным скриншотом бага).
    // SceneGeometry::build() не должен знать про Zoning/ZoneGrid вообще
    // (ровно тот же приём, что и с m_chamferSizes выше — геометрия получает
    // уже готовые поклеточные данные, а не логику их вычисления).
    //
    // Стартовая safe-зона (см. MapGenerator.cpp: safeX0=2,safeZ0=2,
    // safeX1=11,safeZ1=11 — те же числа, здесь не экспортированы через
    // GenerateResult, поэтому взяты как радиус вокруг известной точки
    // спавна m_player.resetForNewGame(spawnPos) в newGame(), см. ниже)
    // ВСЕГДА получает палитру 0 (исходный цвет движка до зонирования) —
    // без этого игрок при разных seed стартовал бы в случайно
    // выбранном цвете вместо привычного, узнаваемого "дома" (см. историю
    // правок: реальный скриншот показал заодно наложившиеся друг на
    // друга debug-маркеры разных регионов около этой самой зоны).
    m_paletteWallColors.assign((size_t)m_mapW * (size_t)m_mapH, glm::vec3(0.0f));
    m_paletteFloorColors.assign((size_t)m_mapW * (size_t)m_mapH, glm::vec3(0.0f));
    for (int z = 0; z < m_mapH; ++z) {
        for (int x = 0; x < m_mapW; ++x) {
            BlendedZoneColor(m_zoneGrid, x, z,
                              m_paletteWallColors[(size_t)z * m_mapW + x],
                              m_paletteFloorColors[(size_t)z * m_mapW + x]);
        }
    }
    {
        constexpr float kSpawnX = 6.5f, kSpawnZ = 6.5f; // см. newGame(): spawnPos(6.5,0.5,6.5)
        constexpr float kSpawnPaletteRadius = 9.0f; // с запасом покрывает safeX0..safeX1 (2..11)
        glm::vec3 spawnWallColor, spawnFloorColor;
        SceneGeometry::GetZonePalette(0, spawnWallColor, spawnFloorColor);
        for (int z = 0; z < m_mapH; ++z) {
            for (int x = 0; x < m_mapW; ++x) {
                const float dx = ((float)x + 0.5f) - kSpawnX;
                const float dz = ((float)z + 0.5f) - kSpawnZ;
                if (dx * dx + dz * dz <= kSpawnPaletteRadius * kSpawnPaletteRadius) {
                    m_paletteWallColors[(size_t)z * m_mapW + x] = spawnWallColor;
                    m_paletteFloorColors[(size_t)z * m_mapW + x] = spawnFloorColor;
                }
            }
        }
    }

    m_endSafeX0 = result.endSafeX0;
    m_endSafeZ0 = result.endSafeZ0;
    m_endSafeX1 = result.endSafeX1;
    m_endSafeZ1 = result.endSafeZ1;
    m_winButtonPos = result.winButtonPos;

    m_smallSafeZoneCenters = std::move(result.smallSafeZoneCenters);
    m_smallSafeZoneRadius = result.smallSafeZoneRadius;

    // Дневники: позиции + тексты уже разложены 1:1 по карманам в
    // MapGenerator (см. Diaries::SelectForSeed) — здесь только сохраняем
    // результат и заводим по одному "прочитано" флагу на дневник (сброшен,
    // пока не подтянуто из сейва — см. loadSlot()/SaveSystem).
    m_diaries = std::move(result.diaries);
    m_diariesRead.assign(m_diaries.size(), false);

    // Fog of war: сбрасываем карту "что игрок ещё ни разу не видел".
    m_minimapFog.resetExplored(m_mapW, m_mapH);
}

void DungeonScene::placeTorches(unsigned int seed)
{
    MapGenerator::TorchPlacement placement = MapGenerator::PlaceTorches(
        m_mapW, m_mapH,
        m_endSafeX0, m_endSafeZ0, m_endSafeX1, m_endSafeZ1,
        m_smallSafeZoneCenters, m_smallSafeZoneRadius,
        [this](int x, int z) { return isWall(x, z); },
        [this](int x, int z) { return isFloor(x, z); },
        [this](int x, int z) { return wallCornerCut(x, z) != WallShapes::CornerCut::None; },
        MAX_TORCHES,
        seed
    );

    m_torchWallBase = std::move(placement.wallBase);
    m_torchNormal = std::move(placement.normal);
    m_torchFlamePos = std::move(placement.flamePos);
    m_torchColor = std::move(placement.color);
    m_torchIntensity = std::move(placement.intensity);
    m_torchCellLookup = std::move(placement.torchCellLookup);
}

// Плавный (не Voronoi-дискретный) цвет клетки — смешивает палитры ДВУХ
// ближайших регионов вблизи их общей границы вместо того, чтобы отдавать
// цвет ТОЛЬКО ближайшего (как это делал Zoning::StyleAt()). Баг, который
// это чинит: раньше ровно на Voronoi-границе двух регионов цвет стены/
// пола мгновенно (за 1 клетку) скакал с одной палитры на другую — без
// всякого перехода (см. историю правок — реальный скриншот показал
// именно это: зелёная стена, а через клетку — уже красная).
//
// Метод: находим ДВА ближайших центра региона, считаем разницу их
// расстояний до клетки. Если клетка гораздо ближе к одному центру, чем
// к другому (diff >= kPaletteBlendWidth) — чистый цвет ближайшего,
// поведение не отличается от старого. Если клетка примерно на границе
// (diff -> 0) — цвета смешиваются 50/50. Формула симметрична: подходя к
// границе с любой стороны, итоговый цвет сходится к одному и тому же
// значению (a+b)/2, поэтому шва на самой границе нет.
static void BlendedZoneColor(const Zoning::ZoneGrid& zoneGrid, int cellX, int cellZ,
                              glm::vec3& outWallColor, glm::vec3& outFloorColor)
{
    if (zoneGrid.centers.empty())
    {
        SceneGeometry::GetZonePalette(0, outWallColor, outFloorColor);
        return;
    }

    const float fx = (float)cellX, fz = (float)cellZ;
    size_t nearestIdx = 0, secondIdx = 0;
    float nearestDistSq = 1e30f, secondDistSq = 1e30f;

    for (size_t i = 0; i < zoneGrid.centers.size(); ++i)
    {
        const float dx = fx - zoneGrid.centers[i].x;
        const float dz = fz - zoneGrid.centers[i].z;
        const float distSq = dx * dx + dz * dz;
        if (distSq < nearestDistSq)
        {
            secondDistSq = nearestDistSq;
            secondIdx = nearestIdx;
            nearestDistSq = distSq;
            nearestIdx = i;
        }
        else if (distSq < secondDistSq)
        {
            secondDistSq = distSq;
            secondIdx = i;
        }
    }

    glm::vec3 wallA, floorA;
    SceneGeometry::GetZonePalette(zoneGrid.styles[nearestIdx].paletteIndex, wallA, floorA);

    if (zoneGrid.centers.size() < 2)
    {
        outWallColor = wallA;
        outFloorColor = floorA;
        return;
    }

    glm::vec3 wallB, floorB;
    SceneGeometry::GetZonePalette(zoneGrid.styles[secondIdx].paletteIndex, wallB, floorB);

    const float diff = std::sqrt(secondDistSq) - std::sqrt(nearestDistSq); // >= 0
    const float blend = std::clamp(1.0f - diff / Zoning::kPaletteBlendWidth, 0.0f, 1.0f) * 0.5f;

    outWallColor = glm::mix(wallA, wallB, blend);
    outFloorColor = glm::mix(floorA, floorB, blend);
}

// ---------------- NEW GAME / CONTINUE (см. save/SaveSystem.h) ----------------

void DungeonScene::loadMapAndGeometry(unsigned int seed)
{
    generateMap(seed);
    m_currentSeed = seed;

    // Полная карта — на GPU, чтобы фрагментный шейдер сцены видел
    // обновлённую геометрию сразу же (см. комментарий в init() ниже).
    m_minimapFog.uploadMapTexture(m_mapW, m_mapH, m_map, m_wallCornerCuts, m_corridorWidened, m_diagonalChainMask);

    placeTorches(seed);

    // Настоящие враги (см. большой комментарий у m_enemies в
    // DungeonScene.h) — свой EnemyAI::init() на каждого (те же
    // указатели на карту — она общая, но m_pathfindingMap строится как
    // собственная копия внутри каждого init(), небольшая, безопасно
    // задублировать 4 раза).
    for (int i = 0; i < kEnemyCount; ++i)
    {
        m_enemyAIs[i].init(
            m_mapW, m_mapH,
            &m_map, &m_wallCornerCuts, &m_diagonalChainMask,
            &m_columnCentersXZ, Columns::kColumnRadius
        );
    }
    spawnEnemiesAcrossMap(seed);

    // Dev-tools манекен (клавиша K) — независимый от 4 настоящих врагов
    // выше, своя инициализация (нужна та же карта для его собственного
    // resolveWallCollision(), хоть он и не преследует). Спавн-позиция —
    // безобидная заглушка, первое же нажатие K переставит его туда, куда
    // смотрит игрок.
    m_enemyAI.init(
        m_mapW, m_mapH,
        &m_map, &m_wallCornerCuts, &m_diagonalChainMask,
        &m_columnCentersXZ, Columns::kColumnRadius
    );
    const glm::vec3 dummySpawnPos(3.5f, 0.0f, 3.5f);
    m_enemyAI.setPosition(dummySpawnPos);

    // m_geometry.destroy() безопасен и при самом первом вызове (все GL-
    // хендлы ещё нулевые, см. SceneGeometry::destroy()) — так что этот
    // же путь используется и из init() (первая карта сессии), и из
    // newGame()/loadSlot() (пересборка уже существующей геометрии).
    m_geometry.destroy();

    // Мировые позиции дневников — центр клетки кармана (см. Diaries::
    // PlacedDiary::pocketCellX/Z), тем же +0.5 смещением к центру клетки,
    // что и у winButtonPos (см. MapGenerator.cpp::result.winButtonPos).
    std::vector<glm::vec3> diaryPositions;
    diaryPositions.reserve(m_diaries.size());
    for (const Diaries::PlacedDiary& d : m_diaries) {
        diaryPositions.emplace_back((float)d.pocketCellX + 0.5f, 0.0f, (float)d.pocketCellZ + 0.5f);
    }

    m_geometry.build(m_mapW, m_mapH, m_map, m_winButtonPos,
                      m_torchWallBase, m_torchNormal, m_torchFlamePos,
                      m_wallCornerCuts, m_columnCentersXZ, m_chamferSizes,
                      m_paletteWallColors, m_paletteFloorColors,
                      diaryPositions);

    // Perf diagnostic: how much geometry the current map produced. Every
    // corner-cut/chamfered cell breaks the greedy quad-merge run it would
    // otherwise be part of (see SceneGeometry::BuildFloorAndWallsGreedy)
    // and falls back to being emitted as its own small set of quads —
    // so a high chamfer probability (see WallShapes::kChamferProbability
    // / Zoning::kChamferProbabilityMax) directly inflates vertex/index
    // counts across the WHOLE map, not just near the camera. Printed
    // here so the effect of tuning those probabilities is visible
    // without needing a profiler.
    std::printf(
        "[perf] map %dx%d -> %d vertices, %d indices (%d triangles)\n",
        m_mapW, m_mapH,
        (int)m_geometry.vertexCount(),
        (int)m_geometry.indexCount(),
        (int)m_geometry.indexCount() / 3
    );

    // Оба шага ниже завязаны на РЕЗУЛЬТАТ m_geometry.build() (число
    // чанков/карта стен) — поэтому обязаны идти именно после него.
    reserveRenderScratchBuffers();
    m_geometry.buildPVS(m_mapW, m_mapH, [this](int x, int z) { return isWall(x, z); });
}

void DungeonScene::newGame()
{
    std::random_device rd;
    const unsigned int seed = rd();
    loadMapAndGeometry(seed);

    // Спавн — центр стартовой safe-zone, как и у самой первой карты
    // сессии (см. init()); resetForNewGame() дополнительно возвращает
    // взгляд/здоровье/стамину/компас к состоянию свежего старта.
    const glm::vec3 spawnPos(6.5f, 0.5f, 6.5f);
    m_player.resetForNewGame(spawnPos);

    // Новая игра всегда получает СВОЙ слот сохранения (первый пустой,
    // либо самый давний по времени записи — см. PickSlotForNewGame()) и
    // сразу же в него сохраняется: экран CONTINUE обязан увидеть эту
    // игру, даже если игрок ещё не сделал ни шага и тут же вышел в меню.
    // Имя пустое — игрок его ещё не задавал (см. m_saveName в .h): даст
    // при первом ручном SAVE из паузы, до тех пор слот подписан generic
    // "SLOT" на экранах CONTINUE/SAVE (см. Application.cpp).
    m_activeSlot = SaveSystem::PickSlotForNewGame();
    m_saveName.clear();
    saveActiveSlot();
}

bool DungeonScene::loadSlot(int slotIndex)
{
    SaveSystem::SaveData save = SaveSystem::LoadSlot(slotIndex);
    if (!save.valid) return false;

    // Тот же seed, что был сохранён, детерминированно разворачивается в
    // ТОЧНО тот же лабиринт и ту же расстановку факелов (см.
    // MapGenerator.h) — поэтому сама геометрия карты не хранится в файле.
    loadMapAndGeometry(save.seed);

    const glm::vec3 pos(save.posX, save.posY, save.posZ);
    m_player.restoreState(pos, save.yaw, save.pitch, save.healthFraction, save.staminaFraction);

    // Туман войны восстанавливаем только если его размер совпадает с
    // текущей картой (128x128 всегда одинаково, но проверка защищает от
    // файла, повреждённого вручную или сделанного другой версией
    // движка с другим размером карты) — иначе просто оставляем туман
    // таким, каким его уже сбросил loadMapAndGeometry()->generateMap()
    // (resetExplored(), см. generateMap() выше): "ничего не раскрыто".
    if (save.explored.size() == (size_t)m_mapW * m_mapH) {
        m_minimapFog.setExplored(std::move(save.explored));
    }

    // Дневники: loadMapAndGeometry() выше уже пересоздал m_diaries (тем
    // же seed -> тот же набор из пула, см. Diaries::SelectForSeed) и
    // сбросил m_diariesRead в "ничего не прочитано" — здесь только
    // проставляем обратно те индексы, что были прочитаны на момент
    // сохранения (см. SaveSystem::SaveData::diariesReadIndices).
    for (int idx : save.diariesReadIndices) {
        if (idx >= 0 && idx < (int)m_diariesRead.size())
            m_diariesRead[(size_t)idx] = true;
    }

    m_activeSlot = slotIndex;
    m_saveName = save.name; // сохраняем имя из файла — дальнейшие автосейвы не должны его затирать пустой строкой
    return true;
}

void DungeonScene::saveActiveSlot()
{
    if (m_activeSlot < 0) return; // нет активной сессии — нечего сохранять

    // БАГФИКС ("периодические просадки FPS на несколько секунд каждые
    // ~20 секунд") — весь снапшот состояния (seed/позиция/здоровье/туман/
    // дневники) собирается здесь же, синхронно, в обычный локальный
    // SaveData — это дёшево (копирование пары чисел + vector<unsigned
    // char> тумана войны, не мегабайты). А вот сама ЗАПИСЬ НА ДИСК теперь
    // уходит в фоновый поток (см. SaveSystem::SaveSlotAsync()) — раньше
    // SaveSystem::SaveSlot() блокировал этот же, основной игровой поток
    // на всё время системного вызова записи файла, что при периодическом
    // автосейве (см. Application::tick(), m_autosaveTimer) давало ровно
    // такую периодичность подвисаний.
    SaveSystem::SaveData data;
    data.seed = m_currentSeed;
    data.name = m_saveName;

    const glm::vec3 pos = m_player.camPos();
    data.posX = pos.x;
    data.posY = pos.y;
    data.posZ = pos.z;
    data.yaw = m_player.yaw();
    data.pitch = m_player.pitch();
    data.healthFraction = m_player.healthFraction();
    data.staminaFraction = m_player.staminaFraction();
    data.explored = m_minimapFog.explored();

    data.diariesReadIndices.clear();
    for (size_t i = 0; i < m_diariesRead.size(); ++i)
        if (m_diariesRead[i]) data.diariesReadIndices.push_back((int)i);

    SaveSystem::SaveSlotAsync(m_activeSlot, data);
}

// ---------------- Item 1: zero-allocation render loop (scratch-буферы) ----------------

void DungeonScene::reserveRenderScratchBuffers()
{
    const size_t n = m_geometry.chunks().size();
    m_chunkVisible.clear();
    m_chunkVisible.resize(n, 0);
    m_mainCounts.clear();
    m_mainCounts.reserve(n);
    m_mainOffsets.clear();
    m_mainOffsets.reserve(n);
    m_particleFirsts.clear();
    m_particleFirsts.reserve(n);
    m_particleCounts.clear();
    m_particleCounts.reserve(n);
    m_enemyOccluderScratch.clear();
    m_enemyOccluderScratch.reserve(kEnemyCount + 1); // +1 — dev-манекен (клавиша K)
}

// ---------------- Игрок (см. PlayerController.h) ----------------
// Камера/движение/коллизии/здоровье/стамина/шаги полностью вынесены в
// PlayerController — здесь только тонкие обёртки, сохраняющие прежний
// публичный API DungeonScene (main.cpp не пришлось менять).

void DungeonScene::processInput(GLFWwindow* window, float deltaTime)
{
    // УЛУЧШЕНИЕ ("4 врага по всей карте", позже увеличено до kEnemyCount)
    // — позиции всех настоящих врагов на конец ПРЕДЫДУЩЕГО кадра
    // (m_enemyAIs[i].update() выполняется в render(), которое идёт
    // позже в кадре, чем processInput()) — отставание на один кадр, при
    // типичных скоростях
    // движения незаметно (тот же трюк, которым обычно решают коллизию
    // игрока с динамическими телами без полноценной синхронизации
    // физики). Dev-tools манекен (m_enemyAI/m_testEnemy) сюда
    // сознательно НЕ включён — он декоративный инструмент тестирования,
    // а не часть игрового процесса, физически блокировать им игрока не
    // нужно.
    std::vector<glm::vec3> enemyPositions;
    enemyPositions.reserve(kEnemyCount);
    for (int i = 0; i < kEnemyCount; ++i)
        enemyPositions.push_back(m_enemyAIs[i].position());

    // Мировые позиции дневников — тот же расчёт (+0.5 к центру клетки
    // кармана), что и при построении меша (см. build() выше) — держим
    // оба места в одной формуле, а не кэшируем один общий вектор, чтобы
    // не тащить лишнее поле ради вычисления на 12 элементов раз в кадр.
    std::vector<glm::vec3> diaryPositions;
    diaryPositions.reserve(m_diaries.size());
    for (const Diaries::PlacedDiary& d : m_diaries)
        diaryPositions.emplace_back((float)d.pocketCellX + 0.5f, 0.0f, (float)d.pocketCellZ + 0.5f);

    m_player.processInput(
        window, deltaTime,
        [this](int x, int z) { return isFloor(x, z); },
        [this](int x, int z) { return wallCornerCut(x, z); },
        [this](int x, int z) { return wallChamferSize(x, z); },
        m_columnCentersXZ,
        m_winButtonPos,
        enemyPositions,
        diaryPositions,
        diariesReadCount()
    );

    // Сообщение "нужно ещё N дневников" — короткоживущее, не
    // AppState-уровня (та же логика, что и у экрана чтения выше): просто
    // таймер, Application.cpp сам решает, когда его показывать.
    if (m_player.consumeWinBlockedRequest())
        m_winBlockedMessageTimer = 2.5f;
    if (m_winBlockedMessageTimer > 0.0f)
        m_winBlockedMessageTimer = std::max(0.0f, m_winBlockedMessageTimer - deltaTime);

    m_nearbyDiaryIndex = m_player.nearbyDiaryIndex();

    // Открытие по E рядом с конкретным дневником — только из обычного
    // геймплея (m_openDiaryIndex/m_journalOpen оба -1/false здесь: пока
    // один из экранов чтения открыт, Application переключает AppState на
    // паузоподобный режим и этот processInput() вообще не вызывается —
    // см. Application.cpp::AppState::READING_DIARY/JOURNAL, тот же
    // принцип, что и у AppState::PAUSED).
    if (m_player.consumeDiaryOpenRequest() && m_nearbyDiaryIndex != -1)
    {
        m_openDiaryIndex = m_nearbyDiaryIndex;
        m_diariesRead[(size_t)m_openDiaryIndex] = true;
    }

    // Tab открывает журнал уже прочитанных, независимо от близости к
    // какому-либо карману.
    if (m_player.consumeJournalToggleRequest())
    {
        m_journalOpen = true;
        m_journalSelectedIndex = 0;
    }

    // ---- Dev-tools: свет (L) / манекен врага (K) / отмена (U) ----
    // См. большой комментарий у m_devSpotlights в DungeonScene.h.
#ifdef HAS_DEV_TOOLS
    if (DevTools::ConsumeSpawnLightKey(window, m_devLightKeyWasDown))
        spawnDevLightAtPlayerView();
    if (DevTools::ConsumeSpawnDummyEnemyKey(window, m_devEnemyKeyWasDown))
        spawnDevDummyEnemyAtPlayerView();
    if (DevTools::ConsumeUndoKey(window, m_devUndoKeyWasDown))
        undoLastDevAction();
    if (DevTools::ConsumeCyclePaletteKey(window, m_devPaletteKeyWasDown))
        cycleDevPalette();
#endif
}

// ---------------- Дневники: экран чтения / журнал (см. Diaries.h) ----------------
// Вынесено отдельно от processInput() выше: пока isReadingOverlayOpen(),
// Application.cpp зовёт tickReadingOverlayInput() ВМЕСТО processInput() —
// см. большой комментарий у DungeonScene::isReadingOverlayOpen() в .h.

void DungeonScene::tickReadingOverlayInput(GLFWwindow* window)
{
    const bool eDown    = glfwGetKey(window, GLFW_KEY_E)     == GLFW_PRESS;
    const bool tabDown  = glfwGetKey(window, GLFW_KEY_TAB)   == GLFW_PRESS;
    const bool upDown   = glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS;
    const bool downDown = glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS;

    if (m_openDiaryIndex != -1)
    {
        // Читаем конкретную запись (открыта либо E на месте в кармане,
        // либо E на подсвеченной строке журнала — см. ветку m_journalOpen
        // ниже) — E тоже закрывает, тем же ключом, каким открыли, а не
        // только Escape (см. Application.cpp).
        if (eDown && !m_overlayEKeyWasDown)
            m_openDiaryIndex = -1;
    }
    else if (m_journalOpen)
    {
        if (!m_diaries.empty())
        {
            if (upDown && !m_overlayUpKeyWasDown)
                m_journalSelectedIndex = (m_journalSelectedIndex - 1 + (int)m_diaries.size()) % (int)m_diaries.size();
            if (downDown && !m_overlayDownKeyWasDown)
                m_journalSelectedIndex = (m_journalSelectedIndex + 1) % (int)m_diaries.size();

            const bool selectionIsRead =
                m_journalSelectedIndex >= 0 &&
                m_journalSelectedIndex < (int)m_diariesRead.size() &&
                m_diariesRead[(size_t)m_journalSelectedIndex];

            // E открывает выбранную строку, только если она уже
            // прочитана — журнал не даёт подглядеть текст ненайденного
            // дневника раньше времени, только сам факт, что он есть
            // (см. buildReadingOverlayGrid() ниже: "ENTRY N ---").
            if (eDown && !m_overlayEKeyWasDown && selectionIsRead)
                m_openDiaryIndex = m_journalSelectedIndex;
        }

        if (tabDown && !m_overlayTabKeyWasDown)
            m_journalOpen = false;
    }

    m_overlayEKeyWasDown    = eDown;
    m_overlayTabKeyWasDown  = tabDown;
    m_overlayUpKeyWasDown   = upDown;
    m_overlayDownKeyWasDown = downDown;
}

bool DungeonScene::buildReadingOverlayGrid(std::vector<unsigned char>& grid, int cols, int rows) const
{
    grid.assign((size_t)std::max(0, cols) * std::max(0, rows), 0);
    if (!isReadingOverlayOpen()) return false;

    // ВАЖНО: с переходом на TextRenderer (см. ui/TextRenderer.h — TTF,
    // stb_truetype) сам ЧИТАЕМЫЙ ТЕКСТ (проза дневника, "LOG", подписи
    // записей журнала) рисуется ОТДЕЛЬНЫМ слоем поверх этого — см.
    // Application.cpp, вызывается после ascii.end() тем же принципом,
    // что и Compass. Этот grid остаётся только под рамку/капли крови/
    // подсветку выбора/мелкие подсказки — ровно то, для чего сеточный
    // шрифт подходит (фиксированные декоративные элементы), а не для
    // абзацев текста (там нужен управляемый размер шрифта — то, чего у
    // сеточного шрифта нет в принципе, 1 символ = 1 клетка всегда).
    int boxX0, boxY0, boxX1, boxY1;
    getReadingBoxBounds(cols, rows, boxX0, boxY0, boxX1, boxY1);
    MainMenu::DrawBox(grid, cols, rows, boxX0, boxY0, boxX1, boxY1,
                       /*thickness=*/1, /*filled=*/false, /*seed=*/1);

    if (m_openDiaryIndex != -1 && m_openDiaryIndex < (int)m_diaries.size())
    {
        const Diaries::PlacedDiary& d = m_diaries[(size_t)m_openDiaryIndex];

        MainMenu::PutText(grid, cols, rows, cols / 2 - 4, boxY1 - 1, "E CLOSE");

        // Потёки крови по верхнему/нижнему краю рамки — детерминированно
        // по poolIndex (не по кадру, не по факту наличия текста внутри —
        // остаются даже теперь, когда сама проза рисуется другим слоем).
        for (int i = 0; i < 5; ++i)
        {
            const int span = std::max(1, (boxX1 - boxX0) - 4);
            const unsigned char dripGlyph = (i % 2 == 0) ? MainMenu::GLYPH_DRIP_BIG : MainMenu::GLYPH_DRIP_SMALL;

            const unsigned int hTop = MainMenu::Hash(d.poolIndex * 97 + i, 4242);
            MainMenu::PutGlyph(grid, cols, rows, boxX0 + 2 + (int)(hTop % (unsigned)span), boxY0 + 1, dripGlyph);

            const unsigned int hBot = MainMenu::Hash(d.poolIndex * 131 + i, 9001);
            MainMenu::PutGlyph(grid, cols, rows, boxX0 + 2 + (int)(hBot % (unsigned)span), boxY1 - 1, dripGlyph);
        }
    }
    else if (m_journalOpen)
    {
        // Подсветка выбранной строки теперь рисуется TextRenderer'ом
        // (см. Application.cpp: m_textRenderer.drawRect()) в ТЕХ ЖЕ
        // пиксельных координатах, что и сама подпись строки — раньше
        // здесь была отдельная сеточная DrawBox() с независимо
        // посчитанной позицией, которая неизбежно расходилась с текстом
        // на новом слое (два разных вычисления одной и той же позиции).
        MainMenu::PutText(grid, cols, rows, cols / 2 - 10, boxY1 - 1, "TAB CLOSE, E READ");
    }

    return true;
}

void DungeonScene::getReadingBoxBounds(int cols, int rows, int& x0, int& y0, int& x1, int& y1) const
{
    // Единая рамка на оба режима (дневник/журнал) — см. обсуждение:
    // "размер окошек LOG и когда читает текст должен быть одинаковым".
    // Вынесено в отдельный метод (был инлайн в buildReadingOverlayGrid()),
    // т.к. Application.cpp теперь тоже нужны ЭТИ ЖЕ границы — чтобы
    // разместить TextRenderer-текст ровно внутри нарисованной здесь
    // рамки, а не рассинхронизировать два независимых вычисления одних
    // и тех же чисел в двух файлах.
    x0 = cols / 2 - 38; x1 = cols / 2 + 38;
    y0 = rows / 2 - 20; y1 = rows / 2 + 20;
}

void DungeonScene::spawnEnemiesAcrossMap(uint32_t seed)
{
    // Раскладка "по одному в каждом секторе сетки карты" — гарантирует,
    // что kEnemyCount врагов не сгрудятся друг с другом (в отличие от
    // чисто случайного выбора точек по всей карте, который иногда мог
    // бы случайно бросить нескольких в одну и ту же комнату). Сетка
    // считается ОТ kEnemyCount автоматически (было захардкожено "2x2
    // четверти" — работало только для ровно 4 врагов; для 7 два сектора
    // получали бы по два врага, а остальные — ни одного). Плюс минимум
    // kMinDistFromPlayerSpawn от стартовой точки игрока (6.5, 6.5, см.
    // newGame()) — чтобы не встретить кого-то из них в первую же
    // секунду игры прямо у себя на голове.
    std::mt19937 rng(seed ^ 0x5EED0004u); // свой независимый поток случайности, не связан с зонированием/колоннами/etc.

    const glm::vec3 playerSpawnPos(6.5f, 0.0f, 6.5f);
    const float kMinDistFromPlayerSpawn = 8.0f;
    const int kAttemptsPerSector = 200;

    // Ближайшая к квадрату сетка, вмещающая kEnemyCount секторов — для
    // 4 это по-прежнему ровно 2x2, для 7 это 3x3 (9 секторов, 2 из них
    // просто останутся без врага) — центр карты сектора закрывают
    // равномерно в обоих случаях, никогда не концентрируют несколько
    // врагов в одном и том же секторе.
    const int gridCols = (int)std::ceil(std::sqrt((double)std::max(1, kEnemyCount)));
    const int gridRows = (int)std::ceil((double)std::max(1, kEnemyCount) / (double)gridCols);

    const int sectorW = std::max(1, m_mapW / gridCols);
    const int sectorH = std::max(1, m_mapH / gridRows);

    for (int i = 0; i < kEnemyCount; ++i)
    {
        const int sx = i % gridCols;
        const int sz = (i / gridCols) % gridRows;

        const int x0 = sx * sectorW;
        const int x1 = (sx == gridCols - 1) ? m_mapW : x0 + sectorW; // последний столбец — до конца карты (на случай, если mapW не делится ровно)
        const int z0 = sz * sectorH;
        const int z1 = (sz == gridRows - 1) ? m_mapH : z0 + sectorH; // аналогично для последней строки

        std::uniform_int_distribution<int> distX(x0, std::max(x0, x1 - 1));
        std::uniform_int_distribution<int> distZ(z0, std::max(z0, z1 - 1));

        glm::vec3 chosen(x0 + 0.5f, 0.0f, z0 + 0.5f); // резервное значение, если вообще ничего не нашли
        bool found = false;

        for (int attempt = 0; attempt < kAttemptsPerSector && !found; ++attempt)
        {
            const int cx = distX(rng);
            const int cz = distZ(rng);
            if (!isFloor(cx, cz))
                continue;

            const glm::vec3 candidate(cx + 0.5f, 0.0f, cz + 0.5f);
            if (glm::length(candidate - playerSpawnPos) < kMinDistFromPlayerSpawn)
                continue;

            chosen = candidate;
            found = true;
        }

        if (!found)
        {
            // Резерв: полный перебор клеток сектора по порядку — редкий
            // случай (очень маленькая карта/сектор почти весь занят
            // стенами), но лучше детерминированно найти ХОТЬ ЧТО-ТО
            // проходимое, чем оставить врага в клетке-заглушке, которая
            // может оказаться стеной.
            for (int cz = z0; cz < z1 && !found; ++cz)
            {
                for (int cx = x0; cx < x1 && !found; ++cx)
                {
                    if (!isFloor(cx, cz))
                        continue;
                    chosen = glm::vec3(cx + 0.5f, 0.0f, cz + 0.5f);
                    found = true;
                }
            }
        }

        m_enemyAIs[i].setPosition(chosen);
        m_enemies[i].setPosition(chosen);
    }
}

bool DungeonScene::isEnemyVisibleToPlayer(const glm::vec3& enemyPos) const
{
    const glm::vec3 playerPos = m_player.camPos();

    glm::vec3 toEnemy = enemyPos - playerPos;
    toEnemy.y = 0.0f;
    const float dist = glm::length(toEnemy);

    // Дальность — сопоставима с тем, на сколько сам ИИ видит игрока на
    // бегу (kSightRadiusRun=14 в EnemyAI.cpp), не сильно больше и не
    // сильно меньше: игрок не должен "видеть" врага дальше, чем видел
    // бы его сам враг при таком же освещении/дистанции.
    const float kPlayerSightRadius = 14.0f;
    if (dist > kPlayerSightRadius)
        return false;

    if (dist > 0.001f)
    {
        const glm::vec3 toEnemyDir = toEnemy / dist;

        glm::vec3 front = m_player.getFront();
        front.y = 0.0f;
        const float frontLen = glm::length(front);
        if (frontLen > 0.0001f)
        {
            front /= frontLen;

            // Половина угла обзора — камера рендерится с вертикальным
            // FOV 63° (см. glm::perspective() выше в render()); 45°
            // половинного угла даёт где-то ~90° по горизонтали на
            // типичном широкоэкранном соотношении — с небольшим
            // запасом на периферийное зрение, а не строго "то, что
            // ровно влезает в кадр".
            const float kPlayerFOVHalfAngleDeg = 45.0f;
            const float cosHalfFOV = std::cos(glm::radians(kPlayerFOVHalfAngleDeg));
            if (glm::dot(front, toEnemyDir) < cosHalfFOV)
                return false;
        }
    }

    // Прямая видимость — та же функция, что использует восприятие
    // самого ИИ (LightBaking::HasLineOfSight), просто с камерой игрока
    // в роли наблюдателя вместо позиции врага.
    const glm::vec2 fromXZ(playerPos.x, playerPos.z);
    const glm::vec2 toXZ(enemyPos.x, enemyPos.z);
    return LightBaking::HasLineOfSight(
        fromXZ, toXZ,
        m_mapW, m_mapH,
        m_map, m_wallCornerCuts, m_diagonalChainMask,
        m_columnCentersXZ, Columns::kColumnRadius);
}

void DungeonScene::spawnDevLightAtPlayerView()
{
    // Молча не добавлять сверх лимита шейдерного массива (см.
    // devLightPos[8] в scene.frag/enemy.frag) — не самое дружелюбное
    // поведение (можно было бы вывести сообщение), но это
    // dev-инструмент, а не пользовательский UI; 8 фонариков разом и так
    // с запасом для тестирования одной сцены.
    if ((int)m_devSpotlights.size() >= 8)
        return;

    DevSpotlight light;
    light.position = m_player.camPos();
    light.direction = glm::normalize(m_player.getFront());
    m_devSpotlights.push_back(light);

    DevAction action;
    action.type = DevActionType::SpawnLight;
    m_devActionHistory.push_back(action);
}

void DungeonScene::spawnDevDummyEnemyAtPlayerView()
{
    if (!m_testEnemy.isLoaded())
        return;

    // Записываем историю ДО изменения состояния — undo должен знать,
    // каким оно было ПЕРЕД этим конкретным нажатием K.
    DevAction action;
    action.type = DevActionType::SpawnDummyEnemy;
    action.dummyWasActiveBefore = m_devDummyEnemyActive;
    m_devActionHistory.push_back(action);

    // Не прямо в точку камеры (иначе манекен оказался бы буквально
    // внутри игрока) — на разумном удалении перед ним, как будто
    // "поставили там, куда смотрю".
    const glm::vec3 front = glm::normalize(m_player.getFront());
    const glm::vec3 spawnPos = m_player.camPos() + front * 2.0f;

    m_enemyAI.setPosition(spawnPos);
    m_testEnemy.setPosition(spawnPos);
    // Лицом К игроку (обратное направление от front) — тот же приём осей,
    // что и везде в EnemyAI.cpp (atan2(dir.x, dir.z), см. комментарии
    // там про экспорт из Blender).
    const float yawDeg = glm::degrees(std::atan2(-front.x, -front.z));
    m_testEnemy.setYawDegrees(yawDeg);
    // Idle => клип Idle_Watchful (см. setClipName() в init()) — то самое
    // "оглядывается", уже готовое, отдельно искать/делать не пришлось.
    m_testEnemy.setState(EnemyCharacter::State::Idle);

    m_devDummyEnemyActive = true;
}

void DungeonScene::undoLastDevAction()
{
    if (m_devActionHistory.empty())
        return;

    const DevAction last = m_devActionHistory.back();
    m_devActionHistory.pop_back();

    if (last.type == DevActionType::SpawnLight)
    {
        if (!m_devSpotlights.empty())
            m_devSpotlights.pop_back();
    }
    else // SpawnDummyEnemy
    {
        m_devDummyEnemyActive = last.dummyWasActiveBefore;
    }
}

void DungeonScene::cycleDevPalette()
{
    // -1 (обычная зонная палитра) -> 0 -> 1 -> ... -> (kPaletteCount-1)
    // -> обратно в -1. Специально включает -1 В ЦИКЛ (а не отдельным
    // выключателем) — так одной и той же клавишей можно и перебрать все
    // гаммы, и вернуться к обычному виду, не заводя вторую клавишу.
    m_devPaletteOverride++;
    if (m_devPaletteOverride >= Zoning::kPaletteCount)
        m_devPaletteOverride = -1;
}

void DungeonScene::processMouse(double xpos, double ypos)
{
    m_player.processMouse(xpos, ypos);
}

void DungeonScene::tickMenuCameraSpin(float deltaTime)
{
    m_player.tickMenuCameraSpin(deltaTime);
}

void DungeonScene::tickPauseCameraIdle(float deltaTime)
{
    m_player.tickPauseCameraIdle(deltaTime);
}

glm::vec3 DungeonScene::getFront() const
{
    return m_player.getFront();
}

bool DungeonScene::init(){
    m_player.init();

    // ---- Одноразовая GL-инициализация (шейдер сцены/текстура стен/
    // компас/дебаг-оверлей) — НЕ зависит от конкретной карты, поэтому
    // выполняется ровно один раз здесь, до первой генерации лабиринта
    // (см. newGame() ниже, который наоборот может вызываться повторно —
    // по клику NEW GAME — и поэтому ничего из этого не трогает). ----
    const std::string sceneVertSrc = ShaderLoader::LoadSource("assets/shaders/scene.vert");
    const std::string sceneFragSrc = ShaderLoader::LoadSource("assets/shaders/scene.frag");
    GLuint vs = compileShader(GL_VERTEX_SHADER, sceneVertSrc.c_str());
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, sceneFragSrc.c_str());
    m_program = linkProgram(vs, fs);
    cacheUniformLocations();

    // Текстура стен — см. WallTexture::kDefaultName, чтобы поменять её
    // на другую. Если файл не нашёлся/не загрузился, игра не падает —
    // стены просто останутся без текстуры (см. проверку wallTexLoaded
    // в assets/shaders/scene.frag).
    m_wallTex.load(WallTexture::kDefaultName);

    m_compass.create();
    m_debugMapOverlay.create();
    m_playerTorch.init();

    // ---- Враг (скелетная анимация, см. EnemyCharacter.h/SkinnedModel.h) ----
    // Свой, отдельный шейдер (enemy.vert/enemy.frag) — не ветка в
    // scene.vert/scene.frag (см. большой комментарий в enemy.vert за тем,
    // почему). Освещается той же физикой (тени от факелов + свет в руке
    // + туман), просто с вершинным цветом вместо текстур стен.
    {
        const std::string enemyVertSrc = ShaderLoader::LoadSource("assets/shaders/enemy.vert");
        const std::string enemyFragSrc = ShaderLoader::LoadSource("assets/shaders/enemy.frag");
        GLuint evs = compileShader(GL_VERTEX_SHADER, enemyVertSrc.c_str());
        GLuint efs = compileShader(GL_FRAGMENT_SHADER, enemyFragSrc.c_str());
        m_enemyProgram = linkProgram(evs, efs);
        cacheEnemyUniformLocations();
    }

    // ---- Общая (одна на всех существ) модель врага ----
    // ОПТИМИЗАЦИЯ ПАМЯТИ: раньше здесь грузился ОТДЕЛЬНЫЙ экземпляр
    // SkinnedModel для m_testEnemy И ещё kEnemyCount раз для каждого
    // m_enemies[i] — 8 независимых копий геометрии/анимаций/текстуры
    // одного и того же файла на GPU и CPU. Теперь файл читается РОВНО
    // ОДИН РАЗ, здесь; ниже и манекен, и все настоящие враги просто
    // получают указатель на этот единственный экземпляр (см.
    // EnemyCharacter::attachSharedModel() и большой комментарий в
    // EnemyCharacter.h). Путь, куда нужно положить экспортированный
    // .glb — см. большой комментарий в EnemyCharacter.h; если файла там
    // нет, загрузка просто не удастся и ни один враг не будет
    // рисоваться (см. render()), игра не падает.
    const bool enemyModelLoaded = m_enemySharedModel.load("assets/models/enemy/the_wrapped.glb");
    if (!enemyModelLoaded)
    {
        std::fprintf(stderr,
            "[EnemyCharacter] no model at assets/models/enemy/the_wrapped.glb — "
            "enemies will not render until this file is provided (see EnemyCharacter.h).\n");
    }

    // Раскладка "состояние -> имя клипа" одинаковая у всех — тело в
    // маленькой лямбде, чтобы не повторять 6 строк 8 раз подряд.
    auto assignClipNames = [](EnemyCharacter& c)
    {
        c.setClipName(EnemyCharacter::State::Idle,     "Idle_Watchful");
        c.setClipName(EnemyCharacter::State::Walk,     "Walk_Nervous");
        c.setClipName(EnemyCharacter::State::Run,      "Run_Frantic");
        c.setClipName(EnemyCharacter::State::Attack,   "Attack_Lunge");
        c.setClipName(EnemyCharacter::State::WallSlam, "Wall_slam");
        // БЫЛО "Scream.lol" — реальное имя в присланном файле оказалось
        // просто "Scream" (проверено загрузчиком на настоящем файле,
        // см. девлог: все 6 клипов подтверждены по именам).
        c.setClipName(EnemyCharacter::State::Scream,   "Scream");
    };

    // Dev-tools манекен (клавиша K) — тестовая позиция/состояние под
    // управлением EnemyAI (см. render(): m_enemyAI.update() вызывается
    // каждый кадр), здесь только присоединение общей модели и
    // соответствие состояние->имя клипа, один раз при старте.
    m_testEnemy.attachSharedModel(enemyModelLoaded ? &m_enemySharedModel : nullptr);
    if (m_testEnemy.isLoaded())
        assignClipNames(m_testEnemy);

    // Настоящие враги (см. большой комментарий у m_enemies в
    // DungeonScene.h) — та же общая модель, что и у манекена выше,
    // свой ИИ (m_enemyAIs[i]) и своя стартовая позиция. Тот же файл
    // модели, та же раскладка клипов — kEnemyCount визуально идентичных
    // врагов, различаются только ИИ-состоянием и позицией.
    for (int i = 0; i < kEnemyCount; ++i)
    {
        m_enemies[i].attachSharedModel(enemyModelLoaded ? &m_enemySharedModel : nullptr);
        if (m_enemies[i].isLoaded())
            assignClipNames(m_enemies[i]);
    }

    // Первая карта сессии генерируется ТЕМ ЖЕ путём, что и обычная
    // "новая игра" (случайный seed, полный сброс игрока, автосейв в
    // свободный слот) — просто в момент запуска приложения, а не по
    // клику NEW GAME (см. Application.cpp) — единая точка правды для
    // логики генерации/сборки геометрии, ничего не дублируется.
    newGame();

    return m_program != 0 && m_compass.isReady();
}

void DungeonScene::cacheUniformLocations()
{
    if (!m_program) return;
    m_uniView             = glGetUniformLocation(m_program, "view");
    m_uniProjection        = glGetUniformLocation(m_program, "projection");
    m_uniCamPos            = glGetUniformLocation(m_program, "camPos");
    m_uniPlayerLightPos       = glGetUniformLocation(m_program, "playerLightPos");
    m_uniPlayerLightDir       = glGetUniformLocation(m_program, "playerLightDir");
    m_uniPlayerLightColor     = glGetUniformLocation(m_program, "playerLightColor");
    m_uniPlayerLightIntensity = glGetUniformLocation(m_program, "playerLightIntensity");
    m_uniDevLightPos          = glGetUniformLocation(m_program, "devLightPos");
    m_uniDevLightDir          = glGetUniformLocation(m_program, "devLightDir");
    m_uniDevLightCount        = glGetUniformLocation(m_program, "devLightCount");
    m_uniDevPaletteOverride   = glGetUniformLocation(m_program, "devPaletteOverride");
    m_uniIsViewmodelDraw      = glGetUniformLocation(m_program, "uIsViewmodelDraw");
    m_uniInvView              = glGetUniformLocation(m_program, "uInvView");
    m_uniViewmodelSway        = glGetUniformLocation(m_program, "uViewmodelSway");
    m_uniTime              = glGetUniformLocation(m_program, "uTime");
    m_uniRenderDistance    = glGetUniformLocation(m_program, "renderDistance");
    m_uniMapTex            = glGetUniformLocation(m_program, "mapTex");
    m_uniWallTex           = glGetUniformLocation(m_program, "wallTex");
    m_uniWallTexEnabled    = glGetUniformLocation(m_program, "wallTexEnabled");
    m_uniWallTexContrast   = glGetUniformLocation(m_program, "wallTexContrast");
    m_uniTorchPos          = glGetUniformLocation(m_program, "torchPos");
    m_uniTorchColor        = glGetUniformLocation(m_program, "torchColor");
    m_uniTorchIntensity    = glGetUniformLocation(m_program, "torchIntensity");
    m_uniTorchCount        = glGetUniformLocation(m_program, "torchCount");
    // Колонны (Шаг 2) — нужны шейдеру для ray-vs-circle теста в тенях
    // (см. assets/shaders/scene.frag: shadowedByWall()), т.к. клетка
    // колонны теперь пол в mapTex и сама по себе тени не давала бы (см.
    // историю правок про "тени как от квадратов"/отсутствие тени у колонн).
    m_uniColumnPos         = glGetUniformLocation(m_program, "columnPos");
    m_uniColumnCount       = glGetUniformLocation(m_program, "columnCount");
    m_uniColumnRadius      = glGetUniformLocation(m_program, "columnRadius");

    // Тени от врагов (см. большой комментарий у m_uniEnemyOccluderPosXZ в .h
    // и у shadowedByWall() в scene.frag).
    m_uniEnemyOccluderPosXZ  = glGetUniformLocation(m_program, "enemyOccluderPosXZ");
    m_uniEnemyOccluderCount  = glGetUniformLocation(m_program, "enemyOccluderCount");
    m_uniEnemyOccluderRadius = glGetUniformLocation(m_program, "enemyOccluderRadius");
    m_uniEnemyOccluderHeight = glGetUniformLocation(m_program, "enemyOccluderHeight");
}

void DungeonScene::cacheEnemyUniformLocations()
{
    if (!m_enemyProgram) return;
    m_uEnemyView                = glGetUniformLocation(m_enemyProgram, "view");
    m_uEnemyProjection           = glGetUniformLocation(m_enemyProgram, "projection");
    m_uEnemyModel                = glGetUniformLocation(m_enemyProgram, "model");
    m_uEnemyBoneMatrices         = glGetUniformLocation(m_enemyProgram, "boneMatrices");
    m_uEnemyCamPos               = glGetUniformLocation(m_enemyProgram, "camPos");
    m_uEnemyTime                 = glGetUniformLocation(m_enemyProgram, "uTime");
    m_uEnemyPlayerLightPos       = glGetUniformLocation(m_enemyProgram, "playerLightPos");
    m_uEnemyPlayerLightDir       = glGetUniformLocation(m_enemyProgram, "playerLightDir");
    m_uEnemyPlayerLightColor     = glGetUniformLocation(m_enemyProgram, "playerLightColor");
    m_uEnemyPlayerLightIntensity = glGetUniformLocation(m_enemyProgram, "playerLightIntensity");
    m_uEnemyDevLightPos          = glGetUniformLocation(m_enemyProgram, "devLightPos");
    m_uEnemyDevLightDir          = glGetUniformLocation(m_enemyProgram, "devLightDir");
    m_uEnemyDevLightCount        = glGetUniformLocation(m_enemyProgram, "devLightCount");
    m_uEnemyDevPaletteOverride   = glGetUniformLocation(m_enemyProgram, "devPaletteOverride");
    m_uEnemyTorchPos             = glGetUniformLocation(m_enemyProgram, "torchPos");
    m_uEnemyTorchColor           = glGetUniformLocation(m_enemyProgram, "torchColor");
    m_uEnemyTorchIntensity       = glGetUniformLocation(m_enemyProgram, "torchIntensity");
    m_uEnemyTorchCount           = glGetUniformLocation(m_enemyProgram, "torchCount");
    m_uEnemyMapTex               = glGetUniformLocation(m_enemyProgram, "mapTex");
    m_uEnemyColumnPos            = glGetUniformLocation(m_enemyProgram, "columnPos");
    m_uEnemyColumnCount          = glGetUniformLocation(m_enemyProgram, "columnCount");
    m_uEnemyColumnRadius         = glGetUniformLocation(m_enemyProgram, "columnRadius");
    m_uEnemyRenderDistance       = glGetUniformLocation(m_enemyProgram, "renderDistance");
    m_uEnemyDiffuseTex           = glGetUniformLocation(m_enemyProgram, "diffuseTex");
    m_uEnemyHasDiffuseTex        = glGetUniformLocation(m_enemyProgram, "hasDiffuseTex");
}

void DungeonScene::shutdown()
{
    m_player.shutdown();
    m_compass.destroy();
    m_debugMapOverlay.destroy();

    m_geometry.destroy();

    m_playerTorch.destroy();

    m_testEnemy.destroy();
    // БАГФИКС (найден по пути): m_enemies[] раньше вообще не
    // освобождались здесь (только манекен выше) — на выходе из
    // приложения не критично (ОС и так заберёт всё), но раз уж
    // приводим в порядок владение ресурсами врага (см.
    // m_enemySharedModel ниже), заодно чистим и это.
    for (int i = 0; i < kEnemyCount; ++i)
        m_enemies[i].destroy();
    // Общая модель (см. большой комментарий у m_enemySharedModel в .h) —
    // единственное место, где реально освобождаются её GPU-ресурсы
    // (VAO/VBO/EBO/диффузная текстура): ни m_testEnemy, ни m_enemies[]
    // больше ими не владеют, см. EnemyCharacter::destroy().
    m_enemySharedModel.destroy();
    if (m_enemyProgram)
    {
        glDeleteProgram(m_enemyProgram);
        m_enemyProgram = 0;
    }

    m_minimapFog.destroy();

    m_wallTex.destroy();

    if (m_program)
    {
        glDeleteProgram(
            m_program
        );

        m_program = 0;
    }
}

void DungeonScene::render(
    int viewportWidth,
    int viewportHeight,
    bool gameplayActive)
{
    // [comment corrupted in source file - original text lost/unrecoverable]
    // [comment corrupted in source file - original text lost/unrecoverable]
    m_minimapFog.updateMinimap(
        m_mapW, m_mapH, m_map, m_player.camPos(), m_player.yaw(),
        [this](int x, int z) { return isWall(x, z); },
        m_torchCellLookup
    );

    glEnable(GL_DEPTH_TEST);

    glm::vec3 front =
    getFront();

    glm::vec3 renderCamPos =
        m_player.getCameraRenderPosition();

    glm::vec3 renderCamUp =
        m_player.getCameraRenderUp();

    glm::mat4 view =
        glm::lookAt(
            renderCamPos,
            renderCamPos + front,
            renderCamUp
        );

    float aspect =
        (float)viewportWidth /
        (float)viewportHeight;

    // Дальность прорисовки/тумана. У обычного игрока всегда 16.0 —
    // getViewDistanceMultiplier() без DevTools.h (или при noclip
    // выключенном) всегда отдаёт 1.0, так что здесь ничего не меняется
    // для релизной сборки. В noclip множитель регулируется клавишами
    // +/- (см. DevTools.h) для кинематографичных кадров трейлера.
    const float kBaseRenderDistance = 16.0f;
    m_currentRenderDistance = kBaseRenderDistance * getViewDistanceMultiplier();

    glm::mat4 proj =
        glm::perspective(
            glm::radians(63.0f),
            aspect,
            0.05f,
            std::max(50.0f, m_currentRenderDistance + 10.0f)
        );

    m_lastNearPlane = 0.05f;
    m_lastFarPlane = std::max(50.0f, m_currentRenderDistance + 10.0f);

    glUseProgram(m_program);

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    glUniformMatrix4fv(
        m_uniView,
        1,
        GL_FALSE,
        glm::value_ptr(view)
    );

    glUniformMatrix4fv(
        m_uniProjection,
        1,
        GL_FALSE,
        glm::value_ptr(proj)
    );

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    glUniform3fv(
        m_uniCamPos,
        1,
        glm::value_ptr(renderCamPos)
    );

    // --------------------------------------------------
    // Факел в руке игрока — теперь статичен в системе координат камеры
    // (см. PlayerTorchViewmodel.h): позиция/ориентация модели больше не
    // пересчитывается на CPU каждый кадр вообще, вместо этого шейдер
    // (scene.vert, uIsViewmodelDraw) восстанавливает мировые координаты
    // через обратную матрицу вида — считаем её здесь ОДИН раз на кадр
    // (а не 250 раз, по разу на вершину модели, в самом шейдере).
    // --------------------------------------------------

    const glm::mat4 invView = glm::inverse(view);
    glUniformMatrix4fv(
        m_uniInvView,
        1,
        GL_FALSE,
        glm::value_ptr(invView)
    );

    // БАГФИКС ("свет пропадает вплотную к стене, будто проникает сквозь
    // неё"): раньше свет исходил из worldFlamePos() — точки МОДЕЛИ,
    // вынесенной вперёд от камеры на ~0.46 юнита. Если игрок стоит к
    // стене ближе, чем это расстояние (коллизия вполне такое позволяет),
    // эта точка оказывается ВНУТРИ или ЗА стеной — и для ближайшей же
    // стены освещение считается уже "с той стороны", где nDotL уходит в
    // ноль или отрицательные значения. Свет теперь исходит из позиции
    // КАМЕРЫ напрямую — она физически не может оказаться внутри стены
    // (иначе сломалась бы вся коллизия игрока), так что эта ошибка
    // структурно больше невозможна. Модель (viewmodel) при этом
    // по-прежнему вынесена вперёд — это чисто визуальная позиция меша,
    // никак не связанная с тем, откуда физически идёт свет.
    glUniform3fv(
        m_uniPlayerLightPos,
        1,
        glm::value_ptr(renderCamPos)
    );
    glUniform3fv(
        m_uniPlayerLightDir,
        1,
        glm::value_ptr(glm::normalize(front))
    );
    // "Ощущается как свет от фонарика, а не от факела" — было
    // (1.0, 0.85, 0.65), почти нейтрально-белый, как у светодиодного
    // фонаря. Настоящий огонь заметно теплее/оранжевее — взяли тот же
    // тон, что и у пламени факелов (см. flameColor в PlayerTorchViewmodel.cpp
    // и AddTorchMesh): (1.0, 0.60, 0.15).
    glUniform3f(
        m_uniPlayerLightColor,
        1.0f, 0.60f, 0.15f
    );
    glUniform1f(
        m_uniPlayerLightIntensity,
        // torchBlend плавно гасит свет при опускании факела (ЛКМ) —
        // "не резко пропадает, а потихоньку затухает" — та же кривая
        // (см. PlayerController::update(), m_torchBlend), что уже
        // плавно двигает саму модель.
        1.0f * m_player.torchBlend()
    );

    // ---- Dev-tools: статичные направленные фонарики (клавиша L) ----
    if (!m_devSpotlights.empty())
    {
        std::vector<glm::vec3> devLightPositions;
        std::vector<glm::vec3> devLightDirections;
        devLightPositions.reserve(m_devSpotlights.size());
        devLightDirections.reserve(m_devSpotlights.size());
        for (const DevSpotlight& light : m_devSpotlights)
        {
            devLightPositions.push_back(light.position);
            devLightDirections.push_back(light.direction);
        }
        glUniform3fv(m_uniDevLightPos, (int)devLightPositions.size(), glm::value_ptr(devLightPositions[0]));
        glUniform3fv(m_uniDevLightDir, (int)devLightDirections.size(), glm::value_ptr(devLightDirections[0]));
    }
    glUniform1i(m_uniDevLightCount, (int)m_devSpotlights.size());
    glUniform1i(m_uniDevPaletteOverride, m_devPaletteOverride);

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    glUniform1f(
        m_uniTime,
        (float)glfwGetTime()
    );

    // Дальность прорисовки/тумана (см. m_currentRenderDistance выше).
    glUniform1f(
        m_uniRenderDistance,
        m_currentRenderDistance
    );

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    glActiveTexture(
        GL_TEXTURE0
    );

    glBindTexture(
        GL_TEXTURE_2D,
        m_minimapFog.mapTexture()
    );

    glUniform1i(
        m_uniMapTex,
        0
    );

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_wallTex.id());
    glUniform1i(m_uniWallTex, 1);
    glUniform1f(m_uniWallTexEnabled, m_wallTex.id() != 0 ? 1.0f : 0.0f);
    glUniform1f(m_uniWallTexContrast, m_wallTex.contrast);
    glActiveTexture(GL_TEXTURE0);

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------
    //
    // Кэш активных факелов вынесен в Lighting.h — раньше здесь был инлайн-
    // блок пересчёта ближайших 32 факелов прямо в теле render().
    m_lighting.update(m_player.camPos(), m_currentRenderDistance, MAX_ACTIVE_TORCHES,
                       m_torchFlamePos, m_torchColor, m_torchIntensity,
                       glfwGetTime());

    const std::vector<glm::vec3>& activeTorchPos = m_lighting.activePositions();
    const std::vector<glm::vec3>& activeTorchColor = m_lighting.activeColors();
    const std::vector<float>& activeTorchIntensity = m_lighting.activeIntensities();
    int count = m_lighting.activeCount();

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    if (count > 0)
    {
        glUniform3fv(
            m_uniTorchPos,
            count,
            glm::value_ptr(
                activeTorchPos[0]
            )
        );

        glUniform3fv(
            m_uniTorchColor,
            count,
            glm::value_ptr(
                activeTorchColor[0]
            )
        );

        glUniform1fv(
            m_uniTorchIntensity,
            count,
            activeTorchIntensity.data()
        );
    }

    glUniform1i(
        m_uniTorchCount,
        count
    );

    // --------------------------------------------------
    // Колонны (Шаг 2) — для ray-vs-circle теста в тенях, см.
    // shadowedByWall() в scene.frag и комментарий у m_uniColumnPos выше.
    // Колонны редки (обычно 0-2 на карту, см. историю правок), поэтому
    // без активного "ближайших N" отбора, как у факелов — просто весь
    // список, с защитой от переполнения массива в шейдере (MAX_COLUMNS).
    // --------------------------------------------------

    {
        const int columnCount = std::min((int)m_columnCentersXZ.size(), 16); // MAX_COLUMNS в scene.frag
        if (columnCount > 0)
        {
            glUniform2fv(
                m_uniColumnPos,
                columnCount,
                glm::value_ptr(m_columnCentersXZ[0])
            );
        }
        glUniform1i(m_uniColumnCount, columnCount);
        glUniform1f(m_uniColumnRadius, Columns::kColumnRadius);
    }

    // --------------------------------------------------
    // Тени от врагов (см. большой комментарий у shadowedByWall() в
    // scene.frag и у m_uniEnemyOccluderPosXZ в .h) — та же схема, что и
    // у колонн чуть выше: просто выгружаем XZ-позиции врагов как
    // маленький массив uniform'ов, никакого отдельного прохода
    // рендера/FBO/текстуры для этого не нужно.
    //
    // ОПТИМИЗАЦИЯ (жалоба "при нескольких собранных вместе врагах и
    // torches=20 — сильная просадка FPS") — раньше сюда шли ВСЕ
    // загруженные враги, независимо от расстояния до игрока. Каждый
    // torch (до MAX_ACTIVE_TORCHES=20) на каждый освещаемый фрагмент
    // экрана и так уже платит за дорогой grid-DDA raymarch в
    // shadowedByWall() (см. историю правок там же про то, почему это
    // и без того самое тяжёлое место в шейдере) — цикл по врагам внутри
    // той же функции ДОБАВЛЯЕТ работу к КАЖДОМУ из этих 20 вызовов на
    // фрагмент, даже когда враг физически далеко на другом конце
    // лабиринта и не может отбрасывать тень ни на один видимый факел.
    // Отсекаем по дистанции до камеры тем же радиусом, что и дальность
    // прорисовки/тумана (см. m_currentRenderDistance) — враг ЗА этой
    // дальностью физически не может влиять на то, что вообще попадает в
    // кадр, так что его незачем тащить в uniform-массив и тестировать
    // 20 раз на каждый фрагмент.
    // --------------------------------------------------
    {
        const glm::vec3 camPosXZ = m_player.camPos();
        const float cullRadius = m_currentRenderDistance + 2.0f; // небольшой запас на случай факела почти на границе дальности
        const float cullRadiusSq = cullRadius * cullRadius;

        auto withinCullRadius = [&](const glm::vec3& p) {
            const float dx = p.x - camPosXZ.x;
            const float dz = p.z - camPosXZ.z;
            return (dx * dx + dz * dz) <= cullRadiusSq;
        };

        m_enemyOccluderScratch.clear();
        for (int i = 0; i < kEnemyCount; ++i)
        {
            if (m_enemies[i].isLoaded())
            {
                const glm::vec3 p = m_enemyAIs[i].position();
                if (withinCullRadius(p))
                    m_enemyOccluderScratch.push_back(glm::vec2(p.x, p.z));
            }
        }
        // Dev-tools манекен (клавиша K) — тоже отбрасывает тень, раз уж
        // это по сути ничего не стоит (см. комментарий выше про m_uEnemy*).
        if (m_devDummyEnemyActive && m_testEnemy.isLoaded())
        {
            const glm::vec3 p = m_testEnemy.position();
            if (withinCullRadius(p))
                m_enemyOccluderScratch.push_back(glm::vec2(p.x, p.z));
        }

        const int enemyOccluderCount = std::min((int)m_enemyOccluderScratch.size(), 8); // MAX_ENEMY_OCCLUDERS в scene.frag
        if (enemyOccluderCount > 0)
        {
            glUniform2fv(m_uniEnemyOccluderPosXZ, enemyOccluderCount,
                         glm::value_ptr(m_enemyOccluderScratch[0]));
        }
        glUniform1i(m_uniEnemyOccluderCount, enemyOccluderCount);

        // Радиус — визуальная "ширина плеч" силуэта; высота — реальный
        // рост врага в мире (см. EnemyCharacter::draw(): bind-поза 1.75
        // юнита * kModelScale=0.6 = 1.05). Держать в синхроне при правке
        // kModelScale там.
        const float kEnemyOccluderRadius = 0.30f;
        const float kEnemyOccluderHeight = 1.05f;
        glUniform1f(m_uniEnemyOccluderRadius, kEnemyOccluderRadius);
        glUniform1f(m_uniEnemyOccluderHeight, kEnemyOccluderHeight);
    }

    // --------------------------------------------------
    // Frustum + distance + occlusion culling
    //
    // Instead of drawing the entire static mesh (m_vertexCount vertices
    // covering the whole 128x128 maze) every frame regardless of what's
    // actually in view, we test each chunk's AABB (see buildGeometry())
    // against the camera frustum, the current render/fog distance, and a
    // cheap 2D line-of-sight check against the maze grid (Item 6), and
    // only issue draw calls for chunks that survive all three tests.
    // Chunks fully behind the player, beyond the fog, or hidden behind a
    // wall around a corner contribute zero GPU work this frame.
    // --------------------------------------------------

    glm::vec4 frustumPlanes[6];
    Culling::ExtractFrustumPlanes(proj * view, frustumPlanes);

    // A little slack beyond the fog distance so chunks don't visibly pop
    // out right where the fog is still fading them in.
    const float cullDistance = m_currentRenderDistance + (float)SceneGeometry::kChunkSize;
    const float cullDistanceSq = cullDistance * cullDistance;

    auto chunkWithinRange = [&](const GeoChunk& c) {
        glm::vec3 closest = glm::clamp(renderCamPos, c.aabbMin, c.aabbMax);
        glm::vec3 delta = closest - renderCamPos;
        return glm::dot(delta, delta) <= cullDistanceSq;
    };

    // Visibility is the same for a chunk's main geometry and its particles
    // (they occupy the same footprint), so it's computed once per chunk
    // and reused for both draw passes below, rather than testing twice.
    //
    // NOTE: occlusion culling (5-point line-of-sight sampling per chunk)
    // was tried here and removed — it produced visible black holes
    // whenever a chunk was only actually visible through a narrow/
    // diagonal gap that didn't line up with one of the sampled points.
    // A correct version needs proper cell-based shadowcasting, not point
    // sampling; frustum + distance culling alone (below) is already safe
    // and gives the bulk of the win.
    // Item 3 (review): precomputed PVS prefilter. m_chunkPvsMask[camChunk]
    // is a bitmask of chunks that COULD possibly be visible from the chunk
    // the camera currently stands in (see buildChunkPVS()) — a conservative
    // superset computed once at load time. Checking it is a single
    // shift+and per chunk, and it can only rule chunks OUT that are
    // provably unreachable, never one that's genuinely on screen, so it's
    // combined with (not a replacement for) the frustum+distance test
    // below, exactly like Quake still frustum-culls within a PVS leaf.
    int camChunk = -1;
    if (m_geometry.pvsEnabled() && m_geometry.chunksX() > 0 && m_mapW > 0 && m_mapH > 0)
    {
        int camCellX = std::clamp((int)std::floor(renderCamPos.x), 0, m_mapW - 1);
        int camCellZ = std::clamp((int)std::floor(renderCamPos.z), 0, m_mapH - 1);
        camChunk = (camCellZ / SceneGeometry::kChunkSize) * m_geometry.chunksX() + (camCellX / SceneGeometry::kChunkSize);
        if (camChunk < 0 || camChunk >= (int)m_geometry.pvsMask().size())
            camChunk = -1;
    }
    const bool pvsUsable = (camChunk >= 0);
    const uint64_t camPvsMask = pvsUsable ? m_geometry.pvsMask()[(size_t)camChunk] : ~0ull;

    // Item 1 (review): m_chunkVisible/m_mainCounts/m_mainOffsets/
    // m_particleFirsts/m_particleCounts are persistent fields sized once by
    // reserveRenderScratchBuffers() (called from init(), right after
    // buildGeometry()) — .clear() below drops element count back to 0 but
    // keeps the already-reserved heap block, so none of the push_back()
    // calls in this function allocate in steady state.
    for (size_t i = 0; i < m_geometry.chunks().size(); ++i)
    {
        const GeoChunk& c = m_geometry.chunks()[i];

        if ((c.mainIndexCount <= 0) && (c.particleCount <= 0))
        {
            m_chunkVisible[i] = 0;
            continue;
        }

        if (pvsUsable && !((camPvsMask >> i) & 1ull))
        {
            m_chunkVisible[i] = 0;
            continue;
        }

        if (!chunkWithinRange(c))
        {
            m_chunkVisible[i] = 0;
            continue;
        }

        if (!Culling::AabbInFrustum(c.aabbMin, c.aabbMax, frustumPlanes))
        {
            m_chunkVisible[i] = 0;
            continue;
        }

        m_chunkVisible[i] = 1;
    }

    // --------------------------------------------------
    // Item 3: one glMultiDrawElements call for ALL visible chunks
    // instead of one glDrawElements per chunk — far fewer driver/CPU-side
    // draw-call submissions per frame for the same on-screen result.
    // --------------------------------------------------

    m_mainCounts.clear();
    m_mainOffsets.clear();

    for (size_t i = 0; i < m_geometry.chunks().size(); ++i)
    {
        const GeoChunk& chunk = m_geometry.chunks()[i];
        if (!m_chunkVisible[i] || chunk.mainIndexCount <= 0)
            continue;

        m_mainCounts.push_back(chunk.mainIndexCount);
        m_mainOffsets.push_back(
            (const GLvoid*)(uintptr_t)(chunk.mainIndexFirst * sizeof(GLuint))
        );
    }

    if (!m_mainCounts.empty())
    {
        glBindVertexArray(m_geometry.vao());

        glMultiDrawElements(
            GL_TRIANGLES,
            m_mainCounts.data(),
            GL_UNSIGNED_INT,
            m_mainOffsets.data(),
            (GLsizei)m_mainCounts.size()
        );

        glBindVertexArray(0);
    }

    // ---- Perf diagnostics ----
    // How many triangles/chunks actually got submitted this frame, and
    // how many torches are currently active — read by
    // Application::tick()'s [perf] fps log so an fps dip can be lined up
    // with what the camera happened to be looking at, instead of
    // guessing which subsystem is responsible.
    {
        int totalIndices = 0;
        for (GLsizei c : m_mainCounts)
            totalIndices += (int)c;
        m_lastVisibleTriangles = totalIndices / 3;

        int visibleChunks = 0;
        for (char v : m_chunkVisible)
            if (v) visibleChunks++;
        m_lastVisibleChunkCount = visibleChunks;

        m_lastActiveTorchCount = count; // see m_lighting.update() above
    }

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    m_particleFirsts.clear();
    m_particleCounts.clear();

    for (size_t i = 0; i < m_geometry.chunks().size(); ++i)
    {
        const GeoChunk& chunk = m_geometry.chunks()[i];
        if (!m_chunkVisible[i] || chunk.particleCount <= 0)
            continue;

        m_particleFirsts.push_back(chunk.particleFirst);
        m_particleCounts.push_back(chunk.particleCount);
    }

    // ---- Perf diagnostics (particle count) ----
    {
        int totalParticles = 0;
        for (GLsizei c : m_particleCounts)
            totalParticles += (int)c;
        m_lastVisibleParticles = totalParticles;
    }

    if (!m_particleFirsts.empty())
    {
        glEnable(
            GL_PROGRAM_POINT_SIZE
        );

        glEnable(
            GL_BLEND
        );

        glBlendFunc(
            GL_SRC_ALPHA,
            GL_ONE
        );

        // [comment corrupted in source file - original text lost/unrecoverable]
        glDepthMask(
            GL_FALSE
        );

        glBindVertexArray(
            m_geometry.particleVao()
        );

        glMultiDrawArrays(
            GL_POINTS,
            m_particleFirsts.data(),
            m_particleCounts.data(),
            (GLsizei)m_particleFirsts.size()
        );

        glBindVertexArray(
            0
        );

        glDepthMask(
            GL_TRUE
        );

        glDisable(
            GL_BLEND
        );

        glDisable(
            GL_PROGRAM_POINT_SIZE
        );
    }

    // --------------------------------------------------
    // Покачивание факела ("инерция" при повороте + лёгкое дыхание в
    // покое, см. запрос: "повернул налево, факел слегка отодвинулся
    // направо и вернулся") — см. uViewmodelSway в scene.vert. Считается
    // здесь, ДО отрисовки модели, простой скалярной арифметикой: разница
    // текущего yaw и его же сглаженной ("отстающей") версии.
    // --------------------------------------------------

    {
        const double nowTime = glfwGetTime();
        float dt = 0.0f;
        if (m_playerTorchLagInit)
        {
            // Клампим dt сверху — защита от одного огромного "скачка"
            // после паузы/лага, из-за которого сглаживание иначе могло
            // бы дёрнуться на большой угол за один кадр.
            dt = (float)std::min(nowTime - m_playerTorchLastSwayTime, 0.1);
        }
        m_playerTorchLastSwayTime = nowTime;

        const float currentYaw = m_player.yaw();
        if (!m_playerTorchLagInit)
        {
            m_playerTorchLagYaw = currentYaw;
            m_playerTorchLagInit = true;
        }

        // Кратчайшая разница углов (корректно через переход ±180°).
        float yawDiff = currentYaw - m_playerTorchLagYaw;
        yawDiff = std::fmod(yawDiff + 540.0f, 360.0f) - 180.0f;

        // "Погоня" сглаженного yaw за реальным — чем МЕДЛЕННЕЕ скорость
        // (kLagSpeed), тем заметнее и дольше факел "отстаёт" при резком
        // повороте камеры.
        const float kLagSpeed = 8.0f; // 1/сек
        m_playerTorchLagYaw += yawDiff * std::min(1.0f, dt * kLagSpeed);

        // Знак подобран так: поворот НАЛЕВО увеличивает рассинхрон в
        // сторону, которая после умножения на -kSwayScale двигает факел
        // ВПРАВО (+X в локальных координатах модели, см.
        // PlayerTorchViewmodel.h) — ровно как просили. Если на практике
        // окажется, что качается в другую сторону, достаточно поменять
        // знак этой константы на противоположный.
        const float kSwayScale = -0.010f;
        float rawTargetSway = yawDiff * kSwayScale;

        // Ограничение амплитуды ЦЕЛИ: без этого при очень резком рывке
        // мышью (yawDiff — десятки градусов за один кадр) итоговое
        // смещение стремилось бы к настолько большому числу, что факел
        // фактически пропадал из вида.
        const float kMaxSway = 0.08f;
        rawTargetSway = std::clamp(rawTargetSway, -kMaxSway, kMaxSway);

        // БАГФИКС ("телепортируется" при резком движении мыши): клампинг
        // выше ограничивает МАКСИМУМ цели, но сама цель всё ещё могла
        // измениться скачком за один кадр (yawDiff считается напрямую из
        // текущего yaw, который мышь может дёрнуть на десятки градусов
        // мгновенно) — а раньше именно rawTargetSway шёл в шейдер
        // НАПРЯМУЮ, без какого-либо сглаживания САМОГО ВЫХОДНОГО значения
        // (сглаживалась только опорная m_playerTorchLagYaw, что не мешает
        // РАЗНИЦЕ yawDiff меняться скачком). Второй этап сглаживания —
        // отдельная, ГАРАНТИРОВАННО непрерывная переменная, которая
        // плавно стремится к цели, а не принимает её значение напрямую:
        const float kSwayVisualSpeed = 25.0f; // 1/сек — насколько быстро видимый сдвиг догоняет цель
        m_playerTorchSwayVisual +=
            (rawTargetSway - m_playerTorchSwayVisual) * std::min(1.0f, dt * kSwayVisualSpeed);

        const float swayX = m_playerTorchSwayVisual;

        // Лёгкое "дыхание" в покое — небольшая синусоида по времени, не
        // зависящая от поворота вообще, чтобы факел не выглядел
        // абсолютно неподвижным, даже когда игрок стоит на месте.
        const float idleBobX = std::sin((float)nowTime * 1.3f) * 0.006f;
        const float idleBobY = std::sin((float)nowTime * 1.7f + 1.0f) * 0.005f;

        // Покачивание при ходьбе/беге — "моделька не двигается при
        // ходьбе/беге, должна слегка покачиваться вверх-вниз". Свой
        // накопитель фазы (m_playerTorchBobPhase), а не время напрямую —
        // так частота честно связана со скоростью шагов, а не с
        // абсолютным временем (иначе покачивание не останавливалось бы
        // сразу вместе с игроком). При беге период короче (частота
        // выше) — "уменьши период, чтобы анимация была заметнее" — и
        // амплитуда заметно больше, раньше её не было вообще.
        const bool isMoving = m_player.isMoving();
        const bool isRunning = m_player.isRunning();

        // Было 7.0/11.0 — "при беге слишком быстро поднимается и
        // опускается", уменьшили разницу между ходьбой и бегом заметно.
        const float walkBobFreq = 6.0f;
        const float runBobFreq = 8.0f;
        const float bobFreq = isRunning ? runBobFreq : walkBobFreq;

        if (isMoving)
            m_playerTorchBobPhase += dt * bobFreq;

        // Было 0.020/0.040 — "слишком высоко поднимается и опускается",
        // амплитуду срезали примерно вдвое в обоих режимах.
        const float walkBobAmp = 0.010f;
        const float runBobAmp = 0.016f;
        const float bobAmp = isRunning ? runBobAmp : walkBobAmp;

        // moving-блендинг такой же формы, что и у камеры (плавно
        // нарастает/спадает, не мгновенно включается/выключается).
        const float targetMoveBlend = isMoving ? 1.0f : 0.0f;
        const float moveBlendSpeed = isRunning ? 12.0f : 10.0f; // как у камеры
        m_playerTorchMoveBlend += (targetMoveBlend - m_playerTorchMoveBlend) * std::min(1.0f, dt * moveBlendSpeed);

        const float walkRunBobY = m_playerTorchMoveBlend * std::sin(m_playerTorchBobPhase * 2.0f) * bobAmp;

        // Подъём/опускание по ЛКМ (см. PlayerController::torchBlend(),
        // тот же плавный приём, что и у компаса на V) — сдвигаем модель
        // вниз на величину, которой хватает, чтобы полностью уйти из
        // кадра, когда факел "убран" (torchBlend=0), и плавно поднимаем
        // при torchBlend->1.
        const float torchBlend = m_player.torchBlend();
        const float raiseOffsetY = -(1.0f - torchBlend) * 0.9f;

        glUniform2f(m_uniViewmodelSway, swayX + idleBobX, idleBobY + walkRunBobY + raiseOffsetY);
    }

    // --------------------------------------------------
    // Факел в руке игрока (viewmodel) — сама геометрия статична (см.
    // PlayerTorchViewmodel::init()), но перед отрисовкой нужно включить
    // uIsViewmodelDraw, чтобы scene.vert интерпретировал её координаты
    // как локальные (камеры), а не мировые (см. большой комментарий в
    // scene.vert). Обязательно выключаем обратно сразу после — иначе
    // ВСЯ остальная геометрия следующего кадра тоже пойдёт через
    // inverse(view), что и медленнее, и неверно.
    // --------------------------------------------------

    glUniform1i(m_uniIsViewmodelDraw, 1);
    m_playerTorch.draw();
    glUniform1i(m_uniIsViewmodelDraw, 0);

    // --------------------------------------------------
    // Враг (см. EnemyCharacter.h/EnemyAI.h) — свой шейдер (enemy.vert/
    // enemy.frag), поэтому отдельный glUseProgram и переупаковка тех же
    // значений (камера, факелы, колонны, свет в руке, туман), что уже
    // посчитаны выше для основной программы — сами значения одни и те
    // же, просто нужно залить их в location'ы ДРУГОЙ скомпилированной
    // программы. ЭТАП 1 ИИ (см. EnemyAI.h): восприятие + прямолинейное
    // преследование, без обхода стен/рывка/атаки — добавятся следующим
    // заходом поверх уже двигающегося врага.
    // --------------------------------------------------

    // УЛУЧШЕНИЕ ("добавить в игру 4 врага", позже увеличено до
    // kEnemyCount) — общая проверка на "есть ли вообще что рисовать в
    // этом кадре": любой из настоящих врагов ИЛИ
    // dev-tools манекен. Общие (не per-инстанс) uniform'ы шейдера
    // (камера/освещение/факелы/палитра) выставляются один раз здесь,
    // ДО цикла по врагам — это одна и та же сцена для всех, нет смысла
    // пересылать те же числа 4-5 раз за кадр.
    bool anyEnemyToDraw = m_devDummyEnemyActive && m_testEnemy.isLoaded();
    for (int i = 0; i < kEnemyCount && !anyEnemyToDraw; ++i)
        anyEnemyToDraw = m_enemies[i].isLoaded();

    if (anyEnemyToDraw)
    {
        glUseProgram(m_enemyProgram);

        glUniformMatrix4fv(m_uEnemyView, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(m_uEnemyProjection, 1, GL_FALSE, glm::value_ptr(proj));
        glUniform3fv(m_uEnemyCamPos, 1, glm::value_ptr(renderCamPos));
        glUniform1f(m_uEnemyTime, (float)glfwGetTime());
        glUniform1f(m_uEnemyRenderDistance, m_currentRenderDistance);

        glUniform3fv(m_uEnemyPlayerLightPos, 1, glm::value_ptr(renderCamPos));
        glUniform3fv(m_uEnemyPlayerLightDir, 1, glm::value_ptr(glm::normalize(front)));
        glUniform3f(m_uEnemyPlayerLightColor, 1.0f, 0.60f, 0.15f);
        glUniform1f(m_uEnemyPlayerLightIntensity, 1.0f * m_player.torchBlend());

        // ---- Dev-tools: статичные направленные фонарики (см. scene.frag) ----
        if (!m_devSpotlights.empty())
        {
            std::vector<glm::vec3> devLightPositions;
            std::vector<glm::vec3> devLightDirections;
            devLightPositions.reserve(m_devSpotlights.size());
            devLightDirections.reserve(m_devSpotlights.size());
            for (const DevSpotlight& light : m_devSpotlights)
            {
                devLightPositions.push_back(light.position);
                devLightDirections.push_back(light.direction);
            }
            glUniform3fv(m_uEnemyDevLightPos, (int)devLightPositions.size(), glm::value_ptr(devLightPositions[0]));
            glUniform3fv(m_uEnemyDevLightDir, (int)devLightDirections.size(), glm::value_ptr(devLightDirections[0]));
        }
        glUniform1i(m_uEnemyDevLightCount, (int)m_devSpotlights.size());
        glUniform1i(m_uEnemyDevPaletteOverride, m_devPaletteOverride);

        if (count > 0)
        {
            glUniform3fv(m_uEnemyTorchPos, count, glm::value_ptr(activeTorchPos[0]));
            glUniform3fv(m_uEnemyTorchColor, count, glm::value_ptr(activeTorchColor[0]));
            glUniform1fv(m_uEnemyTorchIntensity, count, activeTorchIntensity.data());
        }
        glUniform1i(m_uEnemyTorchCount, count);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_minimapFog.mapTexture());
        glUniform1i(m_uEnemyMapTex, 0);

        // Диффузная текстура модели (см. SkinnedModel::hasDiffuseTexture()) —
        // отдельный texture unit (1), unit 0 уже занят mapTex выше.
        // ОТКЛЮЧЕНО по запросу: с текстурой вид модели понравился меньше,
        // чем без неё — вершинный цвет читается лучше на этой конкретной
        // модели. Сама загрузка текстуры в SkinnedModel НЕ убрана (может
        // понадобиться для другой модели позже) — здесь просто всегда
        // передаём hasDiffuseTex=0, форсируя фрагментный шейдер обратно
        // на чистый вершинный цвет, как было раньше.
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1i(m_uEnemyDiffuseTex, 1);
        glUniform1f(m_uEnemyHasDiffuseTex, 0.0f);

        const int enemyColumnCount = std::min((int)m_columnCentersXZ.size(), 16);
        if (enemyColumnCount > 0)
            glUniform2fv(m_uEnemyColumnPos, enemyColumnCount, glm::value_ptr(m_columnCentersXZ[0]));
        glUniform1i(m_uEnemyColumnCount, enemyColumnCount);
        glUniform1f(m_uEnemyColumnRadius, Columns::kColumnRadius);

        double nowT = glfwGetTime();
        float enemyDt = m_enemyUpdateInit ? (float)std::min(nowT - m_lastEnemyUpdateTime, 0.1) : 0.0f;
        m_lastEnemyUpdateTime = nowT;
        m_enemyUpdateInit = true;

        // УЛУЧШЕНИЕ ("добавить в игру 4 врага") — настоящий ИИ снова
        // включён, теперь для массива из kEnemyCount независимых
        // экземпляров. playerCanSeeThisEnemy считается ОТДЕЛЬНО для
        // каждого (см. isEnemyVisibleToPlayer()) — у каждого врага своя
        // видимость с точки зрения игрока, влияет только на мини-карту
        // (см. большой комментарий у playerCanSeeThisEnemy в EnemyAI.h).
        if (gameplayActive)
        {
            // Тот же интервал, что и восприятие самого ИИ
            // (kPerceptionInterval в EnemyAI.cpp) — см. большой
            // комментарий у isEnemyVisibleToPlayer() в .h про то, зачем
            // это вообще нужно.
            const float kPlayerVisibilityCheckInterval = 0.2f;

            for (int i = 0; i < kEnemyCount; ++i)
            {
                if (!m_enemies[i].isLoaded())
                    continue;

                m_playerVisibilityCheckTimers[i] += enemyDt;
                if (m_playerVisibilityCheckTimers[i] >= kPlayerVisibilityCheckInterval)
                {
                    m_playerVisibilityCheckTimers[i] = 0.0f;
                    m_cachedPlayerCanSeeEnemy[i] = isEnemyVisibleToPlayer(m_enemyAIs[i].position());
                }
                const bool playerSeesThis = m_cachedPlayerCanSeeEnemy[i];

                m_enemyAIs[i].update(
                    enemyDt, renderCamPos,
                    m_player.isRunning(), m_player.isMoving(),
                    m_player.invisibleToEnemy(), playerSeesThis,
                    m_enemies[i]);

                if (m_enemyAIs[i].consumeJustCaughtPlayer())
                {
                    m_player.applyCaughtDebuff();
                    // Фиксированный урон за одно попадание Attack_Lunge — 4
                    // попадания до смерти при максимальном здоровье (100).
                    m_player.applyDamage(25.0f);
                }
            }
        }

        for (int i = 0; i < kEnemyCount; ++i)
        {
            if (m_enemies[i].isLoaded())
                m_enemies[i].draw(m_uEnemyModel, m_uEnemyBoneMatrices);
        }

        // Dev-tools манекен (клавиша K) — независим от настоящих
        // врагов выше: не двигается и не думает, но чтобы Idle-анимация
        // ("оглядывается") продолжала проигрываться, часы клипа всё
        // равно нужно тикать каждый кадр. EnemyAI::update() обычно
        // делает это сам последней строкой (character.update()) — раз
        // он для манекена не вызывается, зовём напрямую.
        if (m_devDummyEnemyActive && gameplayActive)
        {
            m_testEnemy.update(enemyDt);
        }

        if (m_devDummyEnemyActive)
        {
            m_testEnemy.draw(m_uEnemyModel, m_uEnemyBoneMatrices);
        }

        glUseProgram(m_program);
    }

    // --------------------------------------------------
    // Unbind texture
    // --------------------------------------------------

    glActiveTexture(
        GL_TEXTURE0
    );

    glBindTexture(
        GL_TEXTURE_2D,
        0
    );
}

// Called AFTER ascii.end(), directly onto the window framebuffer, so the
// compass never gets re-processed by the ASCII post-effect (which would
// turn its crisp glyphs back into fuzzy luminance-block noise).
void DungeonScene::renderCompassOverlay(int viewportWidth, int viewportHeight)
{
    glViewport(0, 0, viewportWidth, viewportHeight);

    // УЛУЧШЕНИЕ ("на мини-карте отображать врага, если игрок его уже
    // видел", "4 врага по всей карте") — тот же offset, что использует
    // сама сетка мини-карты (см. большой комментарий в compass.frag):
    // целая клетка врага минус целая клетка игрока по X, и наоборот по Z
    // (тот же "перевёрнутый" Z, что и у всей остальной мини-карты — она
    // не вращается с игроком, всегда "север сверху", см. MinimapFog::
    // updateMinimap()). Считается для КАЖДОГО из kEnemyCount настоящих
    // врагов — dev-tools манекен сюда сознательно не включён (декоративный
    // инструмент тестирования, ему нечего "спалиться").
    const glm::vec3 playerWorldPos = m_player.camPos();

    std::vector<float> enemySpottedAlphas;
    std::vector<glm::vec2> enemyMinimapOffsets;
    enemySpottedAlphas.reserve(kEnemyCount);
    enemyMinimapOffsets.reserve(kEnemyCount);
    for (int i = 0; i < kEnemyCount; ++i)
    {
        const glm::vec3 enemyWorldPos = m_enemyAIs[i].position();
        enemySpottedAlphas.push_back(m_enemyAIs[i].spottedMarkerAlpha());
        enemyMinimapOffsets.emplace_back(
            std::floor(enemyWorldPos.x) - std::floor(playerWorldPos.x),
            std::floor(playerWorldPos.z) - std::floor(enemyWorldPos.z)
        );
    }

    m_compass.render(
        m_player.poseBlend(),
        m_player.yaw(),
        m_minimapFog.minimapTexture(),
        m_colorEnabled,
        enemySpottedAlphas,
        enemyMinimapOffsets
    );
}

void DungeonScene::renderDebugMap(int viewportWidth, int viewportHeight)
{
    // УЛУЧШЕНИЕ ("на карте M показать врагов и где они сейчас") —
    // собираем позиции всех kEnemyCount настоящих врагов (+ dev-tools
    // манекен, если он сейчас заспавнен — тоже полезно видеть на отладочной
    // карте, откуда его поставили).
    std::vector<glm::vec3> enemyPositions;
    enemyPositions.reserve(kEnemyCount + 1);
    for (int i = 0; i < kEnemyCount; ++i)
        if (m_enemies[i].isLoaded())
            enemyPositions.push_back(m_enemyAIs[i].position());
    if (m_devDummyEnemyActive && m_testEnemy.isLoaded())
        enemyPositions.push_back(m_enemyAI.position());

    m_debugMapOverlay.render(
        viewportWidth, viewportHeight, m_player.debugMapVisible(),
        m_minimapFog.mapTexture(), m_mapW, m_mapH,
        m_player.camPos(), m_player.yaw(),
        m_columnCentersXZ, m_zoneGrid,
        enemyPositions
    );
}
