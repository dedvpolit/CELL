#include "MapGenerator.h"
#include <random>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace MapGenerator {

GenerateResult Generate() {
    std::random_device rd;
    return Generate(rd());
}

GenerateResult Generate(unsigned int seed) {
    GenerateResult result;
    result.seed = seed;
    result.mapW = 128;
    result.mapH = 128;
    result.map.assign((size_t)result.mapW * result.mapH, 1);  // [comment corrupted in source file - original text lost/unrecoverable]

    // [comment corrupted in source file - original text lost/unrecoverable]
    // [comment corrupted in source file - original text lost/unrecoverable]

    // [comment corrupted in source file - original text lost/unrecoverable]
    int nodesX = (result.mapW - 1) / 2;
    int nodesZ = (result.mapH - 1) / 2;

    std::vector<std::vector<bool>> visited(nodesZ, std::vector<bool>(nodesX, false));

    auto cellAt = [](int nx, int nz) { return glm::ivec2(nx * 2 + 1, nz * 2 + 1); };
    auto setFloor = [&](int x, int z) {
        if (x >= 0 && x < result.mapW && z >= 0 && z < result.mapH)
            result.map[z * result.mapW + x] = 2;
    };

    // seed передан вызывающим кодом (см. Generate(unsigned int) выше):
    // NEW GAME каждый раз генерирует новый случайный seed (см. перегрузку
    // Generate() без аргументов), а "Продолжить" передаёт seed, сохранённый
    // в файле сейва, — тот же seed здесь ВСЕГДА разворачивается в тот же
    // лабиринт (детерминированный mt19937), так что сохранять саму
    // геометрию не нужно.
    std::mt19937 rng(seed);

    std::vector<glm::ivec2> stack;
    stack.push_back({0, 0});
    visited[0][0] = true;
    glm::ivec2 startCell = cellAt(0, 0);
    setFloor(startCell.x, startCell.y);

    const int dx[4] = { 1, -1, 0, 0 };
    const int dz[4] = { 0, 0, 1, -1 };

    while (!stack.empty()) {
        glm::ivec2 cur = stack.back();
        std::vector<int> dirs = { 0, 1, 2, 3 };
        std::shuffle(dirs.begin(), dirs.end(), rng);

        bool moved = false;
        for (int d : dirs) {
            int nx = cur.x + dx[d];
            int nz = cur.y + dz[d];
            if (nx < 0 || nx >= nodesX || nz < 0 || nz >= nodesZ) continue;
            if (visited[nz][nx]) continue;

            visited[nz][nx] = true;
            glm::ivec2 curCell  = cellAt(cur.x, cur.y);
            glm::ivec2 nextCell = cellAt(nx, nz);
            glm::ivec2 wallCell = (curCell + nextCell) / 2;

            setFloor(nextCell.x, nextCell.y);
            setFloor(wallCell.x, wallCell.y);

            stack.push_back({nx, nz});
            moved = true;
            break;
        }
        if (!moved) stack.pop_back();
    }

    // ---------------- Стартовая safe-zone (спавн игрока) ----------------
    // Квадрат 10x10 клеток возле угла (0,0), как и раньше.
    const int safeX0 = 2, safeZ0 = 2, safeX1 = 11, safeZ1 = 11;
    for (int z = safeZ0; z <= safeZ1; z++)
        for (int x = safeX0; x <= safeX1; x++)
            setFloor(x, z);

    // [comment corrupted in source file - original text lost/unrecoverable]
    for (int z = safeZ0; z <= safeZ1; z++)
        setFloor(safeX1 + 1, z);

    // ---------------- Финишная safe-zone (противоположный угол) ----------------
    // Того же размера, что и стартовая: сюда нужно добежать через
    // весь лабиринт. Внутри неё стоит кнопка победы (см. result.winButtonPos
    // и addWinButtonMesh()).
    result.endSafeX1 = result.mapW - 1 - safeX0;             // зеркально от safeX0 у дальнего края
    result.endSafeZ1 = result.mapH - 1 - safeZ0;
    result.endSafeX0 = result.endSafeX1 - (safeX1 - safeX0); // та же ширина, что у стартовой зоны
    result.endSafeZ0 = result.endSafeZ1 - (safeZ1 - safeZ0);

    for (int z = result.endSafeZ0; z <= result.endSafeZ1; z++)
        for (int x = result.endSafeX0; x <= result.endSafeX1; x++)
            setFloor(x, z);

    // Двери из финишной зоны в лабиринт с двух сторон, обращённых
    // к центру карты, чтобы комната гарантированно была связана
    // с лабиринтом (она перекрывает как минимум один узел DFS-обхода,
    // но дверь ещё и делает проход визуально явным, как у стартовой зоны).
    for (int z = result.endSafeZ0; z <= result.endSafeZ1; z++)
        setFloor(result.endSafeX0 - 1, z);
    for (int x = result.endSafeX0; x <= result.endSafeX1; x++)
        setFloor(x, result.endSafeZ0 - 1);

    // Кнопка победы стоит в центре финишной комнаты.
    result.winButtonPos = glm::vec3(
        (result.endSafeX0 + result.endSafeX1) * 0.5f + 0.5f,
        0.0f,
        (result.endSafeZ0 + result.endSafeZ1) * 0.5f + 0.5f
    );

    // ---------------- Маленькие safe-зоны внутри лабиринта ----------------
    // В 3 раза меньше стартовой (сторона стартовой = 10 клеток -> сторона
    // маленькой ~= 3 клетки), разбросаны случайно по лабиринту. Так как
    // каждый карман центрируется на уже посещённом узле DFS-обхода, он
    // всегда остаётся связан с остальным лабиринтом.
    const int startSide = safeX1 - safeX0 + 1;             // 10
    const int smallSafeSide = std::max(1, startSide / 3);  // 3
    const int smallSafeRadius = smallSafeSide / 2;         // 1
    // 12 карманов — под дневники (см. Diaries.h): один дневник на карман,
    // без отдельного отбора подмножества. Раньше было 18 (в 3 раза больше
    // исходных 6) — уменьшено, чтобы карманы совпадали 1:1 со списком
    // лора и не были слишком частыми/мелкими относительно размера карты.
    const int smallSafeCount = 12;
    const int minPocketSpacing = 8; // минимальное расстояние между карманами/зонами, в клетках

    std::vector<glm::ivec2> pocketCenters;
    std::uniform_int_distribution<int> nxDist(1, std::max(1, nodesX - 2));
    std::uniform_int_distribution<int> nzDist(1, std::max(1, nodesZ - 2));

    int pocketAttempts = 0;
    while ((int)pocketCenters.size() < smallSafeCount && pocketAttempts < 3000) {
        pocketAttempts++;
        glm::ivec2 cell = cellAt(nxDist(rng), nzDist(rng));

        // подальше от стартовой зоны
        if (cell.x <= safeX1 + minPocketSpacing && cell.y <= safeZ1 + minPocketSpacing)
            continue;
        // подальше от финишной зоны
        if (cell.x >= result.endSafeX0 - minPocketSpacing && cell.y >= result.endSafeZ0 - minPocketSpacing)
            continue;

        bool tooClose = false;
        for (const glm::ivec2& p : pocketCenters) {
            if (std::abs(p.x - cell.x) < minPocketSpacing &&
                std::abs(p.y - cell.y) < minPocketSpacing) {
                tooClose = true;
                break;
            }
        }
        if (tooClose)
            continue;

        pocketCenters.push_back(cell);
    }

    for (const glm::ivec2& c : pocketCenters) {
        for (int z = c.y - smallSafeRadius; z <= c.y + smallSafeRadius; z++)
            for (int x = c.x - smallSafeRadius; x <= c.x + smallSafeRadius; x++)
                setFloor(x, z);
    }

    // Сохраняем карманы для placeTorches() — там на каждый гарантированно
    // ставится по 3 факела (см. блок SMALL SAFE ZONES).
    result.smallSafeZoneCenters = pocketCenters;
    result.smallSafeZoneRadius = smallSafeRadius;

    // Дневники — тем же rng, что и весь остальной лабиринт выше (не новый
    // std::mt19937(seed), а продолжение той же последовательности): важно
    // не то, каким konkретно вызовом rng они выбраны, а то, что вызов
    // детерминирован по seed — на "Продолжить" получаем тот же набор.
    result.diaries = Diaries::SelectForSeed(pocketCenters, rng);

    return result;
}
static bool TryAddTorch(int x, int z,
                        const std::function<bool(int, int)>& isWall,
                        const std::function<bool(int, int)>& isFloor,
                        const std::function<bool(int, int)>& isChamfered,
                        TorchPlacement& out)
{
    if (!isWall(x, z))
        return false;

    // Срезанная клетка — грань у неё укорочена (у диагональных
    // "лестниц" — сильно, см. WallShapes::kChamferSizeChain), и центр
    // грани, где ниже встаёт факел, может оказаться уже не на стене, а
    // в вырезанном клине ("факел висит в воздухе", см. комментарий у
    // MapGenerator::PlaceTorches в .h). Проще исключить такую клетку из
    // кандидатов совсем, чем аккуратно вычислять безопасную точку на
    // укороченной грани для каждого типа/размера среза.
    if (isChamfered && isChamfered(x, z))
        return false;

    static const glm::ivec2 dirs[4] =
    {
        { 1, 0 },
        {-1, 0 },
        { 0, 1 },
        { 0,-1 }
    };

    for (auto d : dirs)
    {
        if (isFloor(x + d.x, z + d.y))
        {
            glm::vec3 normal(
                (float)d.x,
                0.0f,
                (float)d.y
            );

            glm::vec3 wallCenter(
                x + 0.5f,
                0.0f,
                z + 0.5f
            );

            glm::vec3 wallBase =
                wallCenter + normal * 0.5f;

            // [comment corrupted in source file - original text lost/unrecoverable]
            // [comment corrupted in source file - original text lost/unrecoverable]
            bool duplicate = false;

            for (const glm::vec3& p :
                 out.wallBase)
            {
                glm::vec3 diff =
                    wallBase - p;

                if (glm::dot(diff, diff) < 0.15f * 0.15f)
                {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate)
                return false;

            // Высота крепления факела считается от уровня пола (y=0), а не
            // от глаз игрока (m_camPos.y = 0.5, см. DungeonScene::init()).
            // Раньше факел висел на y ~0.86-1.15 — ВЫШЕ уровня глаз, да ещё
            // и с сильным диагональным "вылетом" от стены (0.30-0.34
            // единицы наружу) — из-за чего при взгляде снизу-вверх (игрок
            // стоит прямо под факелом) он визуально казался оторванным от
            // стены, будто висит в воздухе: тёмная (почти невидимая на
            // фоне тёмной стены при ASCII-пороге яркости) ручка-держатель
            // терялась, а видно было только яркое пламя далеко над головой.
            // Теперь факел висит чуть ниже уровня глаз (держатель почти
            // вплотную к стене), как настенный факел на высоте груди/плеч.
            //
            // Смещение по нормали и высота ЗДЕСЬ повторяют tip + смещение
            // сферы пламени из addTorchMesh() (0.14 наружу, 0.62+0.045
            // по высоте) — иначе точка света/частиц (flamePos) окажется
            // не там, где реально нарисован сам огонёк.
            glm::vec3 flamePos =
                wallBase
                + normal * 0.14f
                + glm::vec3(
                    0.0f,
                    0.665f,
                    0.0f
                );

            out.wallBase.push_back(
                wallBase
            );

            out.normal.push_back(
                normal
            );

            out.flamePos.push_back(
                flamePos
            );

            return true;
        }
    }

    return false;
}


TorchPlacement PlaceTorches(
    int mapW, int mapH,
    int endSafeX0, int endSafeZ0, int endSafeX1, int endSafeZ1,
    const std::vector<glm::ivec2>& smallSafeZoneCenters,
    int smallSafeZoneRadius,
    const std::function<bool(int, int)>& isWall,
    const std::function<bool(int, int)>& isFloor,
    const std::function<bool(int, int)>& isChamfered,
    int maxTorches)
{
    std::random_device rd;
    return PlaceTorches(mapW, mapH, endSafeX0, endSafeZ0, endSafeX1, endSafeZ1,
                         smallSafeZoneCenters, smallSafeZoneRadius,
                         isWall, isFloor, isChamfered, maxTorches, rd());
}

TorchPlacement PlaceTorches(
    int mapW, int mapH,
    int endSafeX0, int endSafeZ0, int endSafeX1, int endSafeZ1,
    const std::vector<glm::ivec2>& smallSafeZoneCenters,
    int smallSafeZoneRadius,
    const std::function<bool(int, int)>& isWall,
    const std::function<bool(int, int)>& isFloor,
    const std::function<bool(int, int)>& isChamfered,
    int maxTorches,
    unsigned int seed)
{
    TorchPlacement out;

    // Same name/arg-count as the original member function tryAddTorch(x, z)
    // -- lets every call site below stay textually unchanged.
    auto tryAddTorch = [&](int x, int z) {
        return TryAddTorch(x, z, isWall, isFloor, isChamfered, out);
    };

    // seed передан вызывающим кодом — тот же принцип, что и у Generate()
    // выше: детерминированная расстановка факелов нужна, чтобы
    // "Продолжить" воспроизводило ТОЧНО ту же картину, что была сохранена
    // (тот же seed карты используется и здесь, см. DungeonScene::loadSlot()).
    std::mt19937 rng(seed);

    struct TorchCandidate
    {
        int x;
        int z;
    };

    // ==================================================
    // SAFE ZONE
    // ==================================================

    std::vector<TorchCandidate>
        safeCandidates;

    for (int z = 1;
         z <= 12;
         ++z)
    {
        for (int x = 1;
             x <= 12;
             ++x)
        {
            if (!isWall(x, z))
                continue;

            // [comment corrupted in source file - original text lost/unrecoverable]
            // [comment corrupted in source file - original text lost/unrecoverable]
            if (isFloor(x + 1, z) ||
                isFloor(x - 1, z) ||
                isFloor(x, z + 1) ||
                isFloor(x, z - 1))
            {
                safeCandidates.push_back(
                    {
                        x,
                        z
                    }
                );
            }
        }
    }

    std::shuffle(
        safeCandidates.begin(),
        safeCandidates.end(),
        rng
    );

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    const float safeSpacing = 3.0f;

    for (const TorchCandidate& c :
         safeCandidates)
    {
        if ((int)out.wallBase.size() >= 6)
            break;

        glm::vec3 center(
            c.x + 0.5f,
            0.0f,
            c.z + 0.5f
        );

        bool farEnough = true;

        for (const glm::vec3& p :
             out.wallBase)
        {
            glm::vec3 diff =
                center - p;

            diff.y = 0.0f;

            if (glm::dot(diff, diff) <
                safeSpacing * safeSpacing)
            {
                farEnough = false;
                break;
            }
        }

        if (!farEnough)
            continue;

        tryAddTorch(
            c.x,
            c.z
        );
    }

    // ==================================================
    // END SAFE ZONE (финишная комната)
    // ==================================================

    std::vector<TorchCandidate>
        endSafeCandidates;

    for (int z = endSafeZ0 - 1; z <= endSafeZ1 + 1; ++z)
    {
        for (int x = endSafeX0 - 1; x <= endSafeX1 + 1; ++x)
        {
            if (!isWall(x, z))
                continue;

            if (isFloor(x + 1, z) ||
                isFloor(x - 1, z) ||
                isFloor(x, z + 1) ||
                isFloor(x, z - 1))
            {
                endSafeCandidates.push_back({ x, z });
            }
        }
    }

    std::shuffle(
        endSafeCandidates.begin(),
        endSafeCandidates.end(),
        rng
    );

    const int torchesBeforeEndZone = (int)out.wallBase.size();

    for (const TorchCandidate& c : endSafeCandidates)
    {
        if ((int)out.wallBase.size() >= torchesBeforeEndZone + 6)
            break;

        glm::vec3 center(
            c.x + 0.5f,
            0.0f,
            c.z + 0.5f
        );

        bool farEnough = true;

        for (const glm::vec3& p : out.wallBase)
        {
            glm::vec3 diff = center - p;
            diff.y = 0.0f;

            if (glm::dot(diff, diff) < safeSpacing * safeSpacing)
            {
                farEnough = false;
                break;
            }
        }

        if (!farEnough)
            continue;

        tryAddTorch(c.x, c.z);
    }

    const int endSafeTorchCount = (int)out.wallBase.size() - torchesBeforeEndZone;

    // ==================================================
    // SMALL SAFE ZONES (маленькие карманы в лабиринте)
    // Гарантированно по 3 факела в каждом кармане, а не "как повезёт"
    // при общем проходе по лабиринту.
    // ==================================================

    const int torchesPerPocket = 3;
    const float smallSafeSpacing = 1.3f; // комната всего 3x3 — обычный safeSpacing (3.0) сюда не влезет

    const int torchesBeforePockets = (int)out.wallBase.size();

    for (const glm::ivec2& pocketCenter : smallSafeZoneCenters)
    {
        std::vector<TorchCandidate> pocketCandidates;

        const int px0 = pocketCenter.x - smallSafeZoneRadius - 1;
        const int px1 = pocketCenter.x + smallSafeZoneRadius + 1;
        const int pz0 = pocketCenter.y - smallSafeZoneRadius - 1;
        const int pz1 = pocketCenter.y + smallSafeZoneRadius + 1;

        for (int z = pz0; z <= pz1; ++z)
        {
            for (int x = px0; x <= px1; ++x)
            {
                if (!isWall(x, z))
                    continue;

                if (isFloor(x + 1, z) ||
                    isFloor(x - 1, z) ||
                    isFloor(x, z + 1) ||
                    isFloor(x, z - 1))
                {
                    pocketCandidates.push_back({ x, z });
                }
            }
        }

        std::shuffle(pocketCandidates.begin(), pocketCandidates.end(), rng);

        int placedInPocket = 0;

        for (const TorchCandidate& c : pocketCandidates)
        {
            if (placedInPocket >= torchesPerPocket)
                break;

            glm::vec3 center(c.x + 0.5f, 0.0f, c.z + 0.5f);

            bool farEnough = true;

            for (const glm::vec3& p : out.wallBase)
            {
                glm::vec3 diff = center - p;
                diff.y = 0.0f;

                if (glm::dot(diff, diff) < smallSafeSpacing * smallSafeSpacing)
                {
                    farEnough = false;
                    break;
                }
            }

            if (!farEnough)
                continue;

            if (tryAddTorch(c.x, c.z))
                placedInPocket++;
        }
    }

    const int pocketTorchCount = (int)out.wallBase.size() - torchesBeforePockets;

    // ==================================================
    // [comment corrupted in source file - original text lost/unrecoverable]
    // ==================================================

    std::vector<TorchCandidate>
        mazeCandidates;

    mazeCandidates.reserve(
        mapW * mapH
    );

    for (int z = 1;
         z < mapH - 1;
         ++z)
    {
        for (int x = 1;
             x < mapW - 1;
             ++x)
        {
            if (!isWall(x, z))
                continue;

            // ------------------------------------------------
            // [comment corrupted in source file - original text lost/unrecoverable]
            // ------------------------------------------------

            if (x >= 1 &&
                x <= 12 &&
                z >= 1 &&
                z <= 12)
            {
                continue;
            }

            // ------------------------------------------------
            // [comment corrupted in source file - original text lost/unrecoverable]
            // [comment corrupted in source file - original text lost/unrecoverable]
            // ------------------------------------------------

            if (x >= endSafeX0 - 1 &&
                x <= endSafeX1 + 1 &&
                z >= endSafeZ0 - 1 &&
                z <= endSafeZ1 + 1)
            {
                continue;
            }

            // ------------------------------------------------
            // [comment corrupted in source file - original text lost/unrecoverable]
            // [comment corrupted in source file - original text lost/unrecoverable]
            // ------------------------------------------------

            {
                bool insidePocket = false;

                for (const glm::ivec2& pc : smallSafeZoneCenters)
                {
                    if (x >= pc.x - smallSafeZoneRadius - 1 &&
                        x <= pc.x + smallSafeZoneRadius + 1 &&
                        z >= pc.y - smallSafeZoneRadius - 1 &&
                        z <= pc.y + smallSafeZoneRadius + 1)
                    {
                        insidePocket = true;
                        break;
                    }
                }

                if (insidePocket)
                    continue;
            }

            // ------------------------------------------------
            // [comment corrupted in source file - original text lost/unrecoverable]
            // ------------------------------------------------

            bool touchesCorridor =
                isFloor(x + 1, z) ||
                isFloor(x - 1, z) ||
                isFloor(x, z + 1) ||
                isFloor(x, z - 1);

            if (!touchesCorridor)
                continue;

            mazeCandidates.push_back(
                {
                    x,
                    z
                }
            );
        }
    }

    std::shuffle(
        mazeCandidates.begin(),
        mazeCandidates.end(),
        rng
    );

    // ==================================================
    // [comment corrupted in source file - original text lost/unrecoverable]
    // ==================================================

    const int MIN_MAZE_TORCHES = 256; // см. комментарий в DungeonScene.h/README о том, как менять общее число факелов
    const float mazeSpacing = 3.5f;

    // Порог считаем от количества факелов, уже расставленных в safe-zone /
    // end safe-zone / карманах, а не от жёстко зашитых "6", как раньше —
    // иначе добавление END SAFE ZONE и SMALL SAFE ZONES сдвигало бы порог.
    const int torchesBeforeMaze = (int)out.wallBase.size();

    for (const TorchCandidate& c :
         mazeCandidates)
    {
        if ((int)out.wallBase.size() >=
            torchesBeforeMaze + MIN_MAZE_TORCHES)
        {
            break;
        }

        glm::vec3 center(
            c.x + 0.5f,
            0.0f,
            c.z + 0.5f
        );

        bool farEnough = true;

        for (const glm::vec3& p :
             out.wallBase)
        {
            glm::vec3 diff =
                center - p;

            diff.y = 0.0f;

            if (glm::dot(diff, diff) <
                mazeSpacing * mazeSpacing)
            {
                farEnough = false;
                break;
            }
        }

        if (!farEnough)
            continue;

        tryAddTorch(
            c.x,
            c.z
        );
    }

    // ==================================================
    // [comment corrupted in source file - original text lost/unrecoverable]
    // [comment corrupted in source file - original text lost/unrecoverable]
    // ==================================================

    if ((int)out.wallBase.size() <
        torchesBeforeMaze + MIN_MAZE_TORCHES)
    {
        const float fallbackSpacing = 2.2f;

        for (const TorchCandidate& c :
             mazeCandidates)
        {
            // БАГФИКС: было `>= maxTorches` (глобальный потолок в 1024) —
            // явная опечатка/копипаста из финального прохода ниже. Этот
            // проход — fallback ДЛЯ ТОЙ ЖЕ цели, что и основной проход
            // выше (torchesBeforeMaze + MIN_MAZE_TORCHES = 64), просто с
            // более плотным шагом (2.2 вместо 3.5), если кандидатов на
            // широком шаге не хватило. Со старым условием этот проход не
            // останавливался вообще, пока не забивал буквально всю карту
            // факелами (см. "total torches = 1024, maze = 966" в логах —
            // это ПРЯМОЕ следствие данной опечатки, особенно заметное
            // после того как smallSafeCount подняли 6 -> 18: больше
            // карманов -> больше кандидатных стенных сегментов -> этому
            // проходу стало из чего заполнять карту почти целиком).
            if ((int)out.wallBase.size()
                >= torchesBeforeMaze + MIN_MAZE_TORCHES)
            {
                break;
            }

            glm::vec3 center(
                c.x + 0.5f,
                0.0f,
                c.z + 0.5f
            );

            bool farEnough = true;

            for (const glm::vec3& p :
                 out.wallBase)
            {
                glm::vec3 diff =
                    center - p;

                diff.y = 0.0f;

                if (glm::dot(diff, diff) <
                    fallbackSpacing *
                    fallbackSpacing)
                {
                    farEnough = false;
                    break;
                }
            }

            if (!farEnough)
                continue;

            tryAddTorch(
                c.x,
                c.z
            );
        }
    }

    // ==================================================
    // Финальный проход-"подчистка": самый плотный шаг (1.5), задуман как
    // ПОСЛЕДНИЙ резерв на случай, если после проходов 1+2 всё ещё не
    // набралось MIN_MAZE_TORCHES (например, совсем маленький/тесный
    // лабиринт, где даже 2.2 было слишком много). Раньше выполнялся
    // БЕЗУСЛОВНО (без проверки, а нужно ли вообще) и был ограничен
    // только глобальным maxTorches — то есть, даже если проходу 1
    // отлично хватило кандидатов на 64 факела, этот проход всё равно
    // допихивал факелы во все оставшиеся позиции с шагом 1.5. Теперь
    // пропускается целиком, если цель уже достигнута.
    // ==================================================

    if ((int)out.wallBase.size() < torchesBeforeMaze + MIN_MAZE_TORCHES)
    {
        for (const TorchCandidate& c :
             mazeCandidates)
        {
            if ((int)out.wallBase.size()
                >= maxTorches)
            {
                break;
            }

            bool duplicate = false;

            glm::vec3 center(
                c.x + 0.5f,
                0.0f,
                c.z + 0.5f
            );

            for (const glm::vec3& p :
                 out.wallBase)
            {
                glm::vec3 diff =
                    center - p;

                diff.y = 0.0f;

                if (glm::dot(diff, diff) <
                    1.5f * 1.5f)
                {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate)
                continue;

            tryAddTorch(
                c.x,
                c.z
            );
        }
    }

    // ==================================================
    // [comment corrupted in source file - original text lost/unrecoverable]
    // ==================================================

    std::uniform_real_distribution<float>
        intensRange(
            2.1f,
            2.9f
        );

    for (size_t i = 0;
         i < out.wallBase.size();
         ++i)
    {
        out.color.push_back(
            glm::vec3(
                1.0f,
                0.55f,
                0.20f
            )
        );

        out.intensity.push_back(
            intensRange(rng)
        );
    }

    // ==================================================
    // DEBUG
    // ==================================================

    int total =
        (int)out.wallBase.size();

    int mazeCount =
        std::max(
            0,
            total - 6 - endSafeTorchCount - pocketTorchCount
        );

    std::fprintf(
        stderr,
        "DungeonScene: total torches = %d, safe = %d, endSafe = %d, pockets = %d, maze = %d\n",
        total,
        std::min(total, 6),
        endSafeTorchCount,
        pocketTorchCount,
        mazeCount
    );

    out.torchCellLookup = BuildTorchCellLookup(mapW, mapH, out.wallBase, out.normal);
    return out;
}

std::vector<unsigned char> BuildTorchCellLookup(
    int mapW, int mapH,
    const std::vector<glm::vec3>& torchWallBase,
    const std::vector<glm::vec3>& torchNormal)
{
    std::vector<unsigned char> lookup((size_t)mapW * mapH, 0);

    // ВАЖНО: torchWallBase хранит позицию на ГРАНИЦЕ клетки стены и
    // соседней клетки пола (см. tryAddTorch(): wallBase = wallCenter +
    // normal*0.5, где normal смотрит в сторону пола). Из-за этого
    // floor(p.x)/floor(p.z) НАПРЯМУЮ давал верную клетку стены только
    // для факелов, чья стена смотрит в сторону -x/-z (там wallBase.x/z
    // ровно x.0/z.0) — а для факелов на стенах, смотрящих в сторону
    // +x/+z, wallBase.x/z оказывался ровно x+1.0/z+1.0 (сама граница!),
    // и floor() возвращал СОСЕДНЮЮ клетку пола вместо клетки стены.
    // Именно поэтому примерно половина факелов на мини-карте пропадала
    // "как будто случайно" — на самом деле зависело от того, в какую
    // сторону конкретный факел "прибит" к стене, а не от освещённости/
    // тумана войны. Чтобы получить именно клетку стены, откатываем
    // wallBase обратно на normal*0.5 (используя уже существующий,
    // параллельный torchWallBase, массив torchNormal) — это
    // восстанавливает исходный wallCenter = (x+0.5, z+0.5), чей floor()
    // уже однозначно даёт нужную клетку стены в любом направлении.
    for (size_t i = 0; i < torchWallBase.size(); ++i)
    {
        const glm::vec3& p = torchWallBase[i];
        const glm::vec3& n = torchNormal[i];

        int mx = (int)std::floor(p.x - n.x * 0.5f);
        int mz = (int)std::floor(p.z - n.z * 0.5f);

        if (mx >= 0 && mx < mapW && mz >= 0 && mz < mapH)
            lookup[(size_t)mz * mapW + mx] = 1;
    }

    return lookup;
}

} // namespace MapGenerator
