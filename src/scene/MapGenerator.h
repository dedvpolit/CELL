#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include "Diaries.h"

// ============================================================================
// MapGenerator — процедурная генерация лабиринта (DFS/"recursive backtracker"
// по узлам сетки 2 клетки = 1 шаг) и расстановка факелов по готовой карте.
// Вынесено из DungeonScene при разбиении монолита на модули.
//
// В отличие от WallTexture/Compass (которые ВЛАДЕЮТ своими ресурсами),
// MapGenerator — это генератор одноразового действия (вызывается один раз
// в DungeonScene::init()): он не хранит состояние между вызовами, а
// заполняет переданные ему (Generate()) или возвращённые (PlaceTorches())
// контейнеры. DungeonScene по-прежнему владеет самими данными (m_map,
// m_torchWallBase и т.д.) — так остальные подсистемы (геометрия, свет,
// коллизии), которые читают эти данные на каждый кадр, не должны знать
// про MapGenerator вообще.
// ============================================================================
namespace MapGenerator {

// Результат генерации лабиринта — DungeonScene::generateMap() распаковывает
// это в свои поля (m_map/m_mapW/m_mapH/m_endSafeX0.../m_winButtonPos/
// m_smallSafeZoneCenters/m_smallSafeZoneRadius).
struct GenerateResult {
    int mapW = 0, mapH = 0;
    std::vector<int> map; // 1 = стена, 2 = пол (см. DungeonScene::isWall/isFloor)

    // Seed, которым в итоге была построена эта карта (см. Generate(seed)
    // ниже) — сохраняется в результат, чтобы вызывающий код (DungeonScene)
    // мог записать его в файл сохранения: тот же seed, переданный в
    // Generate()+PlaceTorches(), детерминированно воспроизводит СТРОГО
    // тот же лабиринт и ту же расстановку факелов, так что "Продолжить"
    // не обязано хранить саму геометрию — достаточно одного числа.
    unsigned int seed = 0;

    // Финишная safe-zone (противоположный угол от спавна) — сюда нужно
    // добежать через весь лабиринт, внутри стоит кнопка победы.
    int endSafeX0 = 0, endSafeZ0 = 0, endSafeX1 = 0, endSafeZ1 = 0;
    glm::vec3 winButtonPos{0.0f};

    // Маленькие safe-зоны внутри лабиринта — используются PlaceTorches()
    // ниже (на каждую гарантированно ставится по 3 факела).
    std::vector<glm::ivec2> smallSafeZoneCenters;
    int smallSafeZoneRadius = 0;

    // Дневники, разложенные 1:1 по smallSafeZoneCenters (см. Diaries.h::
    // SelectForSeed) — тем же seed выше, поэтому "Продолжить" всегда
    // возвращает тот же набор из общего пула в тех же карманах.
    std::vector<Diaries::PlacedDiary> diaries;
};

// Детерминированная генерация: тот же seed ВСЕГДА даёт тот же лабиринт
// (тот же DFS-обход, те же карманы) — на этом строится "Продолжить"
// (см. MapGenerator.h GenerateResult::seed / DungeonScene::loadSlot()).
GenerateResult Generate(unsigned int seed);

// Случайный seed на каждый вызов (std::random_device) — используется
// кнопкой "NEW GAME"; тонкая обёртка над Generate(seed) выше, оставлена
// ради обратной совместимости вызывающего кода, который ещё не хочет
// сам выбирать seed.
GenerateResult Generate();

// Расстановка факелов: 6 в стартовой safe-zone, несколько в финишной, по 3
// на каждый "карман" (smallSafeZoneCenters), и остаток по стенам основного
// лабиринта — все параллельными массивами (wallBase/normal/flamePos/color/
// intensity), тот же индекс i относится к одному и тому же факелу.
// isWall/isFloor передаются предикатами, т.к. сами данные карты остаются
// в DungeonScene (см. комментарий выше).
struct TorchPlacement {
    std::vector<glm::vec3> wallBase;
    std::vector<glm::vec3> normal;
    std::vector<glm::vec3> flamePos;
    std::vector<glm::vec3> color;
    std::vector<float> intensity;

    // См. BuildTorchCellLookup() ниже — считается автоматически в конце
    // PlaceTorches(), т.к. таблица строится из wallBase/normal, которые
    // к этому моменту уже финальны (как и в оригинальном коде, где
    // placeTorches() сама вызывала buildTorchCellLookup() последним шагом).
    std::vector<unsigned char> torchCellLookup; // size mapW*mapH, 0/1
};

// seed — как и у Generate(seed) выше: тот же seed всегда даёт ту же
// расстановку факелов (см. TryAddTorch()/std::shuffle() ниже). "Продолжить"
// передаёт СОХРАНЁННЫЙ seed карты (см. GenerateResult::seed) — не обязан
// быть каким-то отдельным "факельным" seed, достаточно одного числа.
//
// isChamfered — есть ли у клетки срез угла (WallShapes::CornerCut != None,
// см. DungeonScene::wallCornerCut()). Факел ставится в ЦЕНТРЕ грани
// клетки (см. TryAddTorch в .cpp) — это предположение верно только для
// обычной прямоугольной клетки; у срезанной грань укорочена (особенно
// сильно у диагональных "лестниц", см. WallShapes::kChamferSizeChain), и
// центр грани может оказаться уже в вырезанном клине, а не на стене —
// отсюда и был баг "факел висит в воздухе". Проще и надёжнее исключить
// такие клетки из кандидатов на крепление факела вообще, чем чинить
// вычисление центра под каждый тип/размер среза.
TorchPlacement PlaceTorches(int mapW, int mapH,
                             int endSafeX0, int endSafeZ0, int endSafeX1, int endSafeZ1,
                             const std::vector<glm::ivec2>& smallSafeZoneCenters,
                             int smallSafeZoneRadius,
                             const std::function<bool(int, int)>& isWall,
                             const std::function<bool(int, int)>& isFloor,
                             const std::function<bool(int, int)>& isChamfered,
                             int maxTorches,
                             unsigned int seed);

// Случайный seed на каждый вызов — используется при генерации новой игры
// (см. Generate() выше, тот же принцип).
TorchPlacement PlaceTorches(int mapW, int mapH,
                             int endSafeX0, int endSafeZ0, int endSafeX1, int endSafeZ1,
                             const std::vector<glm::ivec2>& smallSafeZoneCenters,
                             int smallSafeZoneRadius,
                             const std::function<bool(int, int)>& isWall,
                             const std::function<bool(int, int)>& isFloor,
                             const std::function<bool(int, int)>& isChamfered,
                             int maxTorches);

// Отдельно вынесена на случай, если понадобится пересчитать lookup без
// полной переросстановки факелов (например, в будущем при runtime-правках).
std::vector<unsigned char> BuildTorchCellLookup(
    int mapW, int mapH,
    const std::vector<glm::vec3>& torchWallBase,
    const std::vector<glm::vec3>& torchNormal);

} // namespace MapGenerator
