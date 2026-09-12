#include "EnemyAI.h"
#include "LightBaking.h"
#include "GridPathfinding.h"
#include "PlayerController.h"
#include <algorithm>
#include <cmath>

namespace {
// Кратчайшая угловая разница target-current в диапазоне (-180, 180] —
// нужна, чтобы поворот всегда шёл в короткую сторону (не через 350°,
// если можно через -10°) и чтобы не было скачка на границе -180/180.
float ShortestAngleDeltaDeg(float currentDeg, float targetDeg)
{
    float delta = std::fmod(targetDeg - currentDeg, 360.0f);
    if (delta > 180.0f) delta -= 360.0f;
    else if (delta < -180.0f) delta += 360.0f;
    return delta;
}

// Загоняет угол в (-180, 180] — чтобы m_yawDegrees не рос неограниченно
// после многих оборотов (не влияет на sin/cos, но грязно для отладки/
// сериализации).
float NormalizeAngleDeg(float deg)
{
    deg = std::fmod(deg, 360.0f);
    if (deg > 180.0f) deg -= 360.0f;
    else if (deg <= -180.0f) deg += 360.0f;
    return deg;
}

// ---- Гипотезы побега (см. EnemyAI::appendGuessedFleeWaypoints) ----
// Сколько клеток максимум враг готов "предположить" по прямой вдоль
// направления, в котором в последний раз двигался игрок, прежде чем
// решить, что дальше там стена/тупик.
constexpr int kFleeGuessMaxCells = 5;
// БАГФИКС: было "порог длины m_playerHeading" — та величина всегда ~1.0
// (нормализованный вектор), проверка была мертва (см. большой
// комментарий у m_timeSinceMeaningfulMovement в EnemyAI.h). Теперь
// реальный порог скорости (юнита/сек) для "игрок действительно
// двигался" и запас времени, в течение которого одиночный неподвижный
// кадр не считается ещё "стоянием на месте".
constexpr float kFleeGuessMeaningfulSpeed = 0.3f; // юнита/сек — ниже похоже на дрожание/шум, не реальную ходьбу
constexpr float kFleeGuessMovementMemory = 0.4f;  // сек — такой запас неподвижности ещё не значит "стоял"
// На сколько клеток вглубь бокового поворота заглядывать, если он
// попался по пути прямой гипотезы (1 клетка — это уже сам поворот,
// дальше — на случай, если игрок не просто завернул за угол, а успел
// отбежать в сторону по этому же ответвлению).
constexpr int kFleeGuessBranchCells = 2;

// ---- Динамическая громкость по дистанции до игрока (см. большой
// комментарий у соответствующих play*() вызовов ниже и в EnemyAudio.h) —
// "враг далеко — звука нет, ближе — чуть слышно, близко — хорошо
// слышно". Полная громкость на nearRadius и ближе, тишина на farRadius и
// дальше, между ними — плавный, чуть более резкий к дальнему краю спад
// (ease-out по квадрату — на слух ближе к тому, как реально гаснет звук
// с расстоянием, чем линейная интерполяция).
float DistanceVolume(float distance, float nearRadius, float farRadius)
{
    if (distance <= nearRadius) return 1.0f;
    if (distance >= farRadius) return 0.0f;
    const float t = (distance - nearRadius) / (farRadius - nearRadius);
    return 1.0f - t * t;
}

// Радиусы слышимости — отдельные под каждый тип звука. Крик обнаружения
// нарочно "разносится" дальше остальных: это событие-предупреждение
// ("что-то тебя заметило"), должно быть слышно даже не у самого врага.
constexpr float kFootstepAudibleNear = 2.0f;
constexpr float kFootstepAudibleFar = 14.0f;
constexpr float kMoanAudibleNear = 2.0f;
constexpr float kMoanAudibleFar = 16.0f;
constexpr float kDetectedAudibleNear = 3.0f;
constexpr float kDetectedAudibleFar = 22.0f;
}

void EnemyAI::init(
    int mapW, int mapH,
    const std::vector<int>* map,
    const std::vector<WallShapes::CornerCut>* cornerCuts,
    const std::vector<unsigned char>* diagonalChainMask,
    const std::vector<glm::vec2>* columnCentersXZ,
    float columnRadius)
{
    m_mapW = mapW;
    m_mapH = mapH;
    m_map = map;
    m_cornerCuts = cornerCuts;
    m_diagonalChainMask = diagonalChainMask;
    m_columnCentersXZ = columnCentersXZ;
    m_columnRadius = columnRadius;

    // БАГФИКС ("ИИ застревает в столбах") — см. большой комментарий у
    // m_pathfindingMap в .h. Строится ОДИН раз здесь (карта/колонны не
    // меняются после генерации уровня) — копия m_map с клетками колонн
    // дополнительно помеченными как стена, только для path-поиска.
    m_pathfindingMap.clear();
    if (m_map)
    {
        m_pathfindingMap = *m_map;
        if (m_columnCentersXZ)
        {
            for (const glm::vec2& col : *m_columnCentersXZ)
            {
                const int cx = (int)std::floor(col.x);
                const int cz = (int)std::floor(col.y);
                if (cx < 0 || cz < 0 || cx >= m_mapW || cz >= m_mapH)
                    continue; // на всякий случай — не должно происходить, но не падать же
                m_pathfindingMap[(size_t)cz * m_mapW + cx] = 1;
            }
        }
    }

    m_audio.init();

    // Стоны — свежий случайный интервал до первого стона на каждом
    // новом уровне (иначе он бы всегда случался через ровно то же самое
    // время после старта — см. m_nextMoanInterval в .h).
    m_moanTimer = 0.0f;
    std::uniform_real_distribution<float> initialMoanDist(9.0f, 18.0f);
    m_nextMoanInterval = initialMoanDist(m_rng);

    // Патруль начинает выбор цели с чистого листа на каждом новом уровне.
    m_hasPatrolTarget = false;
    m_patrolPaused = false;
    m_patrolPauseTimer = 0.0f;

    // УЛУЧШЕНИЕ ("на мини-карте отображать врага, если игрок его уже
    // видел") — m_enemyAI переиспользуется между партиями (см.
    // DungeonScene::m_enemyAI — обычное поле, не пересоздаётся на
    // "Новая игра"), init() же вызывается заново на каждой новой карте
    // — без явного сброса тут флаг "спалён" утёк бы из предыдущего
    // прохождения, и враг оказался бы на мини-карте сразу, ещё до
    // первой настоящей встречи в новой игре.
    m_hasBeenSpotted = false;
    m_timeSinceLastSpotted = 0.0f;
}

bool EnemyAI::isWalkableCell(int x, int z) const
{
    if (m_pathfindingMap.empty()) return false;
    if (x < 0 || z < 0 || x >= m_mapW || z >= m_mapH) return false;
    return m_pathfindingMap[(size_t)z * m_mapW + x] != 1;
}

glm::vec3 EnemyAI::pickRandomPatrolPoint()
{
    if (m_pathfindingMap.empty() || m_mapW <= 0 || m_mapH <= 0)
        return m_position;

    std::uniform_int_distribution<int> distX(0, m_mapW - 1);
    std::uniform_int_distribution<int> distZ(0, m_mapH - 1);

    // Несколько попыток найти случайную проходимую клетку — карта в
    // основном стены (лабиринт), так что "в лоб" может не попасть с
    // первого раза; после разумного числа попыток просто остаёмся на
    // месте (следующий вызов popробует снова).
    for (int attempt = 0; attempt < 30; ++attempt)
    {
        const int x = distX(m_rng);
        const int z = distZ(m_rng);
        if (isWalkableCell(x, z))
            return glm::vec3(x + 0.5f, 0.0f, z + 0.5f);
    }
    return m_position;
}

void EnemyAI::appendGuessedFleeWaypoints(const glm::vec3& lastSeenPos, const glm::vec3& headingAtLoss, bool headingWasFresh, std::vector<glm::vec3>& outWaypoints) const
{
    // БАГФИКС: раньше здесь проверялась glm::length(heading2D) < порог —
    // headingAtLoss (== m_playerHeading) всегда нормализован до длины
    // ровно 1.0, так что это условие практически никогда не срабатывало
    // (см. большой комментарий у m_timeSinceMeaningfulMovement в .h).
    // Свежесть теперь решает вызывающий код по РЕАЛЬНОЙ скорости игрока,
    // передаётся готовым флагом.
    if (!headingWasFresh)
        return; // игрок не двигался достаточно недавно — додумывать нечего

    // Диагональное "направление" почти всегда шум оценки по одному
    // кадру (см. её накопление в update()) — коридоры на сетке
    // прямоугольные, поэтому додумываем строго по ОДНОЙ доминирующей
    // оси, как и сама сетка путей, а не по диагонали сквозь стену.
    const glm::vec2 heading2D(headingAtLoss.x, headingAtLoss.z);
    if (glm::length(heading2D) < 0.001f)
        return; // защита от вырожденного вектора — на практике почти невозможно (heading всегда нормализован)

    glm::ivec2 primaryDir;
    if (std::abs(heading2D.x) >= std::abs(heading2D.y))
        primaryDir = glm::ivec2(heading2D.x > 0.0f ? 1 : -1, 0);
    else
        primaryDir = glm::ivec2(0, heading2D.y > 0.0f ? 1 : -1);

    const glm::ivec2 startCell((int)std::floor(lastSeenPos.x), (int)std::floor(lastSeenPos.z));
    glm::ivec2 furthestStraight = startCell;
    bool branchFound = false;
    glm::ivec2 branchCell;

    for (int step = 1; step <= kFleeGuessMaxCells; ++step)
    {
        const glm::ivec2 next = startCell + primaryDir * step;
        if (!isWalkableCell(next.x, next.y))
            break; // упёрлись в стену/тупик по прямой — дальше в эту сторону не додумываем
        furthestStraight = next;

        // По пути заодно проверяем боковые ответвления (перпендикулярно
        // направлению бега) — реальные коридоры разветвляются, игрок
        // вполне мог свернуть в ближайший поворот, а не бежать строго по
        // прямой до конца. Хватает первого найденного — не превращаем
        // Search в полный обход всех веток лабиринта.
        if (!branchFound)
        {
            const glm::ivec2 perp = (primaryDir.x != 0) ? glm::ivec2(0, 1) : glm::ivec2(1, 0);
            for (int side = -1; side <= 1; side += 2)
            {
                const glm::ivec2 branch = next + perp * side;
                if (isWalkableCell(branch.x, branch.y))
                {
                    branchFound = true;
                    branchCell = branch;
                    // Заглядываем на пару клеток вглубь поворота, а не
                    // только в его первую клетку — иначе точка гипотезы
                    // почти совпадает с прямой линией движения.
                    for (int bstep = 2; bstep <= kFleeGuessBranchCells; ++bstep)
                    {
                        const glm::ivec2 deeper = next + perp * side * bstep;
                        if (!isWalkableCell(deeper.x, deeper.y))
                            break;
                        branchCell = deeper;
                    }
                    break;
                }
            }
        }
    }

    // "Добежал по прямой" — гипотеза, только если реально продвинулись
    // хотя бы на клетку от места, где его видели/слышали (иначе это та
    // же самая точка, не новая информация).
    if (furthestStraight != startCell)
        outWaypoints.push_back(glm::vec3(furthestStraight.x + 0.5f, 0.0f, furthestStraight.y + 0.5f));

    if (branchFound)
        outWaypoints.push_back(glm::vec3(branchCell.x + 0.5f, 0.0f, branchCell.y + 0.5f));
}

bool EnemyAI::followPath(const glm::vec3& target, float speed, float deltaTime)
{
    const float kPathRecomputeInterval = 0.4f;
    m_pathRecomputeTimer += deltaTime;

    if (m_pathRecomputeTimer >= kPathRecomputeInterval || m_path.empty())
    {
        m_pathRecomputeTimer = 0.0f;

        const glm::ivec2 startCell((int)std::floor(m_position.x), (int)std::floor(m_position.z));
        const glm::ivec2 goalCell((int)std::floor(target.x), (int)std::floor(target.z));

        std::vector<glm::ivec2> cellPath;
        // БАГФИКС ("ИИ застревает в столбах") — m_pathfindingMap, не
        // m_map: у последнего клетки колонн — пол, из-за чего путь мог
        // прокладываться ПРЯМО СКВОЗЬ колонну как через обычную открытую
        // клетку (см. большой комментарий у m_pathfindingMap в .h).
        if (!m_pathfindingMap.empty())
            cellPath = GridPathfinding::FindPath(m_mapW, m_mapH, m_pathfindingMap, startCell, goalCell);

        // БАГФИКС ("ходит зигзагом по клеткам вместо диагонали на
        // открытых пространствах") — FindPath() это 4-directional BFS,
        // он даёт кратчайший путь ПО ЧИСЛУ КЛЕТОК, но таких путей с
        // одинаковой длиной обычно много, и BFS детерминированно берёт
        // "лестницу" вместо диагонали (см. большой комментарий у
        // GridPathfinding::SmoothPath в .h — там же почему это не
        // переделано в честный 8-directional BFS/A*).
        if (!m_pathfindingMap.empty())
            cellPath = GridPathfinding::SmoothPath(m_mapW, m_mapH, m_pathfindingMap, cellPath);

        m_path.clear();
        for (const glm::ivec2& c : cellPath)
            m_path.push_back(glm::vec2(c.x + 0.5f, c.y + 0.5f));
        m_pathWaypointIndex = 0;

        // БАГФИКС ("топчется на месте, не продвигается"): первая точка
        // свежепостроенного пути — это центр ТЕКУЩЕЙ клетки (см.
        // GridPathfinding::FindPath — она всегда включает стартовую
        // клетку). Если позиция уже НЕ в самом центре этой клетки (а
        // так почти всегда и есть — движение непрерывное, а пересчёт
        // происходит раз в 0.4с по таймеру, а не строго в момент входа
        // в новую клетку), эта "точка" может оказаться чуть ПОЗАДИ
        // текущего положения — враг делает маленький шаг назад, чтобы
        // "дойти" до нее, и так на каждом пересчёте, без сети net-
        // продвижения вперёд. Пропускаем эту точку, если она и так уже
        // рядом — начинаем сразу со следующей.
        if (m_path.size() > 1)
        {
            const float distToFirst = glm::length(m_path[0] - glm::vec2(m_position.x, m_position.z));
            if (distToFirst < 0.5f)
                m_pathWaypointIndex = 1;
        }
    }

    if (m_pathWaypointIndex >= m_path.size())
        return m_path.empty() ? false : true; // пустой путь (не нашёлся) — не "достигли", просто некуда идти

    const glm::vec2 targetXZ = m_path[m_pathWaypointIndex];
    glm::vec3 toTarget(targetXZ.x - m_position.x, 0.0f, targetXZ.y - m_position.z);
    float dist = glm::length(toTarget);

    const float kWaypointReachedRadius = 0.25f;
    if (dist < kWaypointReachedRadius)
    {
        if (m_pathWaypointIndex + 1 < m_path.size())
        {
            m_pathWaypointIndex++;
            const glm::vec2 nextXZ = m_path[m_pathWaypointIndex];
            toTarget = glm::vec3(nextXZ.x - m_position.x, 0.0f, nextXZ.y - m_position.z);
            dist = glm::length(toTarget);
        }
        else
        {
            return true; // дошли до последней waypoint'ы пути — цель достигнута
        }
    }

    if (dist > 0.01f)
    {
        const glm::vec3 dir = toTarget / dist;
        const float moveDist = std::min(dist, speed * deltaTime);
        m_position += dir * moveDist;

        // Тот же принцип, что и у камеры игрока (getFront(): x=cos(yaw),
        // z=sin(yaw)). ПРЕДУПРЕЖДЕНИЕ: в какую сторону при yaw=0
        // фактически смотрит САМА модель (rest-поза) — не проверено
        // визуально, возможно потребуется фиксированный офсет в градусах.
        // ИСПРАВЛЕНО: было atan2(dir.z, dir.x) — предполагало, что
        // "вперёд" у модели это локальный +X. Реальный факт (проверено
        // эмпирически — топ-даун/3-четверти рендеры с осями не дали
        // однозначного визуального ответа, силуэт слишком симметричен
        // под этими углами), плюс известная, хорошо задокументированная
        // особенность экспорта Blender -> glTF: Blender использует Z-up
        // с "вперёд" = -Y; стандартная конвертация в Y-up (поворот на
        // -90° вокруг X при экспорте) переводит это в "вперёд" = -Z в
        // итоговом файле — почти всегда для моделей, экспортированных
        // из Blender без ручной перестройки осей. Формула ниже вращает
        // так, чтобы именно локальный -Z указывал на dir (не +X).
        const float desiredYawDegrees = glm::degrees(std::atan2(dir.x, dir.z));

        // БАГФИКС ("резко разворачивается лицом к игроку, видно, что
        // ищет путь") — раньше yaw мгновенно ЩЁЛКАЛ на направление к
        // следующей waypoint'е каждый кадр. Это почти незаметно, пока
        // waypoint'ы часто и направление почти не меняется — но на
        // пересчёте пути (раз в 0.4с, см. выше) или на смене waypoint'ы
        // направление может измениться сразу на десятки градусов за
        // один кадр, и монстр буквально "телепортирует" свой поворот —
        // видно, что это алгоритм ищет путь, а не живое существо
        // поворачивается. Теперь yaw плавно ДОГОНЯЕТ desiredYawDegrees с
        // ограниченной скоростью поворота (кратчайшей стороной, см.
        // ShortestAngleDeltaDeg выше), а не телепортируется на неё.
        const float kTurnRateDegPerSec = 225.0f; // было 300 (0.6с на 180°) — по запросу увеличено время разворота до ~0.8с
        const float maxTurnDelta = kTurnRateDegPerSec * deltaTime;
        float turnDelta = ShortestAngleDeltaDeg(m_yawDegrees, desiredYawDegrees);
        turnDelta = glm::clamp(turnDelta, -maxTurnDelta, maxTurnDelta);
        m_yawDegrees = NormalizeAngleDeg(m_yawDegrees + turnDelta);
    }

    // БАГФИКС ("всё равно иногда застревает у столбов") — на момент
    // написания этого куска колонны позже были ОТКЛЮЧЕНЫ ПОЛНОСТЬЮ (см.
    // DungeonScene.cpp — по отдельному запросу, тот класс проблем убрали
    // в корне, а не только для колонн). Сам предохранитель оставлен —
    // он общий, не завязан на конкретно колонны: клетка 1×1, а любая
    // круглая коллизия (например, врага с игроком, см. их
    // kCollisionRadius в EnemyAI.h/PlayerController.h) в принципе может
    // задевать соседние клетки, через которые путь всё ещё легально
    // проходит впритык к границе. Гоняться за каждым таким
    // геометрическим случаем отдельно — бесконечная игра в догонялки.
    // Вместо этого: если тело почти не продвинулось за разумное время,
    // толкаем его в сторону и форсируем пересчёт пути — гарантированно
    // вырывается из ЛЮБОЙ локальной геометрической ловушки, известной
    // или ещё не найденной.
    {
        // БАГФИКС ("враг иногда застывает на пару секунд") — было 0.75с;
        // с тех пор как kEnemyWalkSpeed успел заметно понизиться (сейчас
        // 0.55, было 0.8/1.2), 0.75с реального ожидания перед толчком
        // стало ощущаться слишком долгим "зависанием" — сократили до
        // 0.5с, чтобы сократить худший случай видимой заморозки, не
        // жертвуя надёжностью самой проверки (см. запас ниже).
        const float kStuckCheckInterval = 0.5f;
        // За kStuckCheckInterval на скорости даже медленнее обычной
        // ходьбы (kEnemyWalkSpeed=0.55 в этом файле) тело физически
        // прошло бы ~0.275 юнита без всяких помех — 0.15 всё ещё даёт
        // разумный запас (почти 2x) на нормальные затормаживания (пауза
        // на waypoint'е, начало пути и т.п.), не путая их с настоящим
        // застреванием.
        const float kStuckMinProgress = 0.15f;

        m_stuckCheckTimer += deltaTime;
        if (!m_stuckCheckRefInit)
        {
            m_stuckCheckRefPos = m_position;
            m_stuckCheckRefInit = true;
        }
        else if (m_stuckCheckTimer >= kStuckCheckInterval)
        {
            const float progressed = glm::length(m_position - m_stuckCheckRefPos);
            if (progressed < kStuckMinProgress)
            {
                // Случайное направление, скромное расстояние — не
                // телепорт через полкарты, просто достаточно, чтобы
                // выйти из тесного зазора; сразу прогоняем через
                // resolveWallCollision(), чтобы толчок не занёс прямо в
                // другую стену/колонну.
                std::uniform_real_distribution<float> angleDist(0.0f, 6.2831853f);
                const float angle = angleDist(m_rng);
                const float kEscapeDistance = 0.3f;
                const glm::vec3 nudged = m_position +
                    glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * kEscapeDistance;
                m_position = resolveWallCollision(nudged);

                m_path.clear();
                m_pathRecomputeTimer = kPathRecomputeInterval; // форсировать пересчёт на следующем вызове
            }
            m_stuckCheckTimer = 0.0f;
            m_stuckCheckRefPos = m_position;
        }
    }

    return false;
}

void EnemyAI::update(float deltaTime, const glm::vec3& playerPos, bool playerIsRunning, bool playerIsMoving, bool playerInvisible, bool playerCanSeeThisEnemy, EnemyCharacter& character)
{
    // БАГФИКС ("враг скользит, а не идёт") — 1.2 юнита/сек оказалось
    // слишком быстро относительно длины шага, "зашитой" в саму анимацию
    // ходьбы: клип проигрывается в реальном времени независимо от
    // скорости перемещения (см. EnemyCharacter::update() —
    // m_currentClipTime += deltaTime, без привязки к пройденному
    // расстоянию), так что при слишком быстром перемещении ноги не
    // успевают "отработать" цикл шага за то расстояние, которое тело
    // успевает пройти — визуально это читается как скольжение/катание
    // вместо ходьбы. Понижение скорости — самый простой фикс без
    // переделки самого проигрывания анимации (полное решение — привязать
    // скорость клипа к пройденному расстоянию, т.е. distance-based a не
    // time-based playback, но это отдельная, более крупная переделка).
    const float kEnemyWalkSpeed = 0.55f; // было 0.8 (и 1.2 до того) — "всё ещё скользит", дальше понижаем
    const float kEnemyRunSpeed = 2.0f;
    const float kDashSpeedMultiplier = 1.5f; // "увеличить скорость в 1.5 раза"
    const float kCatchRadius = 0.6f;
    const float kLoseTrackGrace = 2.5f; // "продолжать преследовать ещё 2-3 секунды"

    // ---- Оценённая скорость игрока (для упреждения рывка ниже) — те
    // же числа, что в PlayerController.cpp (walkSpeed/runSpeed): мы не
    // видим реальную скорость игрока отсюда, только позицию, так что
    // это оценка "как быстро он вообще может двигаться", не факт.
    const float kEstimatedPlayerSpeed = playerIsRunning ? 3.0f : 1.5f;

    // ---- Шаги врага — снимок позиции ДО любого движения этого кадра
    // (см. footstep-блок ближе к концу функции). Тот же приём, что
    // footstepStartPos в PlayerController.cpp::processInput(): считаем
    // не по таймеру, а по факту, насколько тело реально сдвинулось за
    // кадр, ПОСЛЕ пути/коллайдера. Attack/WallSlam/Scream возвращаются
    // из функции раньше, чем это используется ниже, — для них шагов и
    // не должно быть (тело не движется), так что там это просто
    // неиспользуемый снимок.
    const glm::vec3 footstepStartPos = m_position;

    // ---- Оценка направления движения игрока (для рывка) ----
    if (m_lastPlayerPosInit)
    {
        glm::vec3 delta = playerPos - m_lastPlayerPos;
        delta.y = 0.0f;
        const float len = glm::length(delta);
        if (len > 0.001f)
            m_playerHeading = delta / len;

        // БАГФИКС ("проверка 'игрок стоял на месте' в
        // appendGuessedFleeWaypoints никогда не срабатывала") — считаем
        // РЕАЛЬНУЮ скорость этого кадра (расстояние/deltaTime), а не
        // длину уже нормализованного m_playerHeading (та всегда ~1.0).
        // Свежий сброс в 0 при настоящем движении, копится, пока
        // скорость ниже порога — короткий запас kFleeGuessMovementMemory
        // (см. .cpp выше) не даёт одиночному "неподвижному" кадру
        // (дрожание позиции и т.п.) стереть только что реальное движение.
        const float speed = (deltaTime > 0.0001f) ? (len / deltaTime) : 0.0f;
        if (speed >= kFleeGuessMeaningfulSpeed)
            m_timeSinceMeaningfulMovement = 0.0f;
        else
            m_timeSinceMeaningfulMovement += deltaTime;
    }
    m_lastPlayerPos = playerPos;
    m_lastPlayerPosInit = true;

    // УЛУЧШЕНИЕ ("на мини-карте — если ИГРОК увидел врага, а не
    // наоборот") — раньше здесь ошибочно использовалось восприятие
    // самого ИИ (canSee ниже — враг увидел игрока), хотя запрос был про
    // обратное. Это полностью отдельная, независимая система: считается
    // КАЖДЫЙ кадр (не только на тике восприятия ИИ ниже), потому что
    // решение "может ли игрок СЕЙЧАС видеть эту точку" вычисляется
    // СНАРУЖИ (см. playerCanSeeThisEnemy — DungeonScene::
    // isEnemyVisibleToPlayer(), FOV+LOS от камеры игрока), а не
    // восприятием самого EnemyAI.
    if (playerCanSeeThisEnemy)
    {
        m_hasBeenSpotted = true;
        m_timeSinceLastSpotted = 0.0f; // свежий контакт — обнуляем "память"
    }
    else if (m_hasBeenSpotted)
    {
        // УЛУЧШЕНИЕ ("сброс флага 'спалён' через какое-то время без
        // контакта") — та же логика, что и раньше, просто теперь
        // приводится в действие видимостью СО СТОРОНЫ ИГРОКА, а не
        // восприятием ИИ.
        m_timeSinceLastSpotted += deltaTime;
        if (m_timeSinceLastSpotted >= kSpottedMemoryDuration)
            m_hasBeenSpotted = false;
    }

    // ---- Attack_Lunge — таймер, ждём, пока анимация доиграет ----
    if (m_state == AIState::Attack)
    {
        m_stateTimer -= deltaTime;
        if (m_stateTimer <= 0.0f)
        {
            m_state = AIState::Run;
            m_path.clear();
            m_attackCooldownTimer = 1.4f; // было 1.0 — "кулдаун между атаками сделать немного больше", см. большой комментарий у m_attackCooldownTimer в EnemyAI.h
        }

        m_position = resolveWallCollision(m_position);
        character.setPosition(m_position);
        character.setYawDegrees(m_yawDegrees);
        character.setState(EnemyCharacter::State::Attack);
        character.update(deltaTime);
        return;
    }

    // ---- Wall_slam — таймер, потом назад в Walk_Nervous (окно для побега) ----
    if (m_state == AIState::WallSlam)
    {
        m_stateTimer -= deltaTime;
        if (m_stateTimer <= 0.0f)
        {
            m_state = AIState::Walk;
            m_path.clear();
        }

        // Именно тут чаще всего и застревало ("когда врезался") — Dash
        // останавливается вплотную к стене на кадре столкновения, и всю
        // длительность WallSlam (2.67с) позиция не двигалась, а
        // проверки на пересечение с геометрией не было вообще. Теперь
        // resolveWallCollision() выталкивает наружу и здесь тоже — если
        // Dash всё же успел на долю кадра "нырнуть" внутрь стены до
        // проверки блокировки, WallSlam это больше не увековечивает.
        m_position = resolveWallCollision(m_position);
        character.setPosition(m_position);
        character.setYawDegrees(m_yawDegrees);
        character.setState(EnemyCharacter::State::WallSlam);
        character.update(deltaTime);
        return;
    }

    // ---- Scream — разовый триггер при СВЕЖЕМ обнаружении, не двигается ----
    if (m_state == AIState::Scream)
    {
        m_stateTimer -= deltaTime;
        if (m_stateTimer <= 0.0f)
        {
            m_state = AIState::Run;
            m_path.clear();
        }

        m_position = resolveWallCollision(m_position);
        character.setPosition(m_position);
        character.setYawDegrees(m_yawDegrees);
        character.setState(EnemyCharacter::State::Scream);
        character.update(deltaTime);
        return;
    }

    // ---- Восприятие (не каждый кадр) — ПРОПУСКАЕТСЯ во время рывка ----
    if (m_state != AIState::Dash)
    {
        const float kPerceptionInterval = 0.2f; // 5 раз/сек
        m_perceptionTimer += deltaTime;

        if (m_perceptionTimer >= kPerceptionInterval)
        {
            const float elapsed = m_perceptionTimer;
            m_perceptionTimer = 0.0f;

            const glm::vec2 fromXZ(m_position.x, m_position.z);
            const glm::vec2 toXZ(playerPos.x, playerPos.z);
            const float dist = glm::length(toXZ - fromXZ);

            const float kSightRadiusWalk = 8.0f;
            const float kSightRadiusRun = 14.0f;
            const float sightRadius = playerIsRunning ? kSightRadiusRun : kSightRadiusWalk;

            bool canSee = dist <= sightRadius;

            // ---- Угол обзора (FOV) — раньше зрение было всенаправленным
            // (только дистанция + LOS, без направления взгляда), враг
            // "видел" игрока прямо у себя за спиной. Теперь ограничено
            // конусом ±kFOVHalfAngleDeg от текущего yaw. Вплотную
            // (kPeripheralRadius) — замечает в любом случае, "натолкнулся"/
            // периферийное зрение, иначе игрок мог бы стоять буквально
            // рядом сбоку и оставаться незамеченным, что уже не страшно, а
            // абсурдно.
            const float kFOVHalfAngleDeg = 55.0f; // ~110° полный конус
            const float kPeripheralRadius = 1.2f;
            if (canSee && dist > kPeripheralRadius)
            {
                const float yawRad = glm::radians(m_yawDegrees);
                // Тот же принцип осей, что и в followPath()/Dash ниже:
                // "вперёд" = (sin(yaw), cos(yaw)).
                const glm::vec2 forward(std::sin(yawRad), std::cos(yawRad));
                const glm::vec2 toPlayerDir = (toXZ - fromXZ) / dist;
                const float cosHalfFOV = std::cos(glm::radians(kFOVHalfAngleDeg));
                canSee = glm::dot(forward, toPlayerDir) >= cosHalfFOV;
            }

            if (canSee && m_map)
            {
                canSee = LightBaking::HasLineOfSight(
                    fromXZ, toXZ,
                    m_mapW, m_mapH,
                    *m_map, *m_cornerCuts, *m_diagonalChainMask,
                    *m_columnCentersXZ, m_columnRadius);
            }

            // dev-tools ("сделать игрока невидимым для врага", клавиша
            // I) — форсируется ПОСЛЕ честного FOV+LOS+радиус, а не
            // вместо него: так все остальные системы (m_hasBeenSpotted,
            // "период памяти" Walk/Run, m_timeSinceLastSpotted) видят
            // ровно ту же картину, что и при обычной потере видимости
            // (спрятался за стену) — специального пути для invisible не
            // потребовалось, он просто "притворяется", что LOS/FOV не
            // прошли. Слух проверяется НИЖЕ по файлу, отдельным блоком
            // (не зависит от canSee вообще) — этот же playerInvisible
            // гасит и его тоже, см. там (по отдельному запросу I стал
            // означать полную неприметность, не только зрительную).
            if (playerInvisible)
                canSee = false;

            if (canSee)
            {
                m_lostTrackTimer = 0.0f;
                m_lastKnownPlayerPos = playerPos;

                const bool closeEnough = dist < sightRadius * 0.6f;
                if (closeEnough)
                {
                    if (m_state != AIState::Run) // свежее обнаружение
                    {
                        m_state = AIState::Scream;
                        m_stateTimer = 1.25f;
                        // dist уже посчитан выше (расстояние до РЕАЛЬНОЙ
                        // playerPos в момент обнаружения) — громкость
                        // крика от него же, отдельная (более широкая)
                        // граница слышимости, чем у шагов/стонов, см.
                        // kDetectedAudibleNear/Far выше.
                        m_audio.playDetected(DistanceVolume(dist, kDetectedAudibleNear, kDetectedAudibleFar));
                    }
                }
                else if (m_state != AIState::Run)
                {
                    m_state = AIState::Walk;
                }
            }
            else
            {
                // Не видит СЕЙЧАС — если только что преследовал (Walk/Run),
                // даём "период памяти" вместо мгновенного сброса (см.
                // большой комментарий класса в EnemyAI.h).
                if (m_state == AIState::Run || m_state == AIState::Walk)
                {
                    m_lostTrackTimer += elapsed;
                    if (m_lostTrackTimer >= kLoseTrackGrace)
                    {
                        // БЫЛО: мгновенно в Idle (патруль) — враг будто
                        // резко "забывал" про погоню. Теперь сначала SEARCH:
                        // идёт к месту, где видел игрока в последний раз, и
                        // недолго "оглядывается", прежде чем вернуться к
                        // обычному патрулю (см. движение ниже и большой
                        // комментарий класса в EnemyAI.h).
                        m_state = AIState::Search;
                        m_lostTrackTimer = 0.0f;
                        m_searchPaused = false;
                        m_searchPauseTimer = 0.0f;
                        m_path.clear();

                        // Гипотезы побега (см. большой комментарий класса
                        // в .h) — сперва честная последняя видимая
                        // позиция, затем, если направление движения
                        // игрока было известно, додуманное продолжение
                        // маршрута (+ боковой поворот, если попался).
                        m_searchWaypoints.clear();
                        m_searchWaypoints.push_back(m_lastKnownPlayerPos);
                        const bool headingWasFresh = m_timeSinceMeaningfulMovement <= kFleeGuessMovementMemory;
                        appendGuessedFleeWaypoints(m_lastKnownPlayerPos, m_playerHeading, headingWasFresh, m_searchWaypoints);
                        m_searchWaypointIndex = 0;
                    }
                    // иначе остаёмся в том же состоянии — движение ниже
                    // пойдёт к m_lastKnownPlayerPos, не к живой playerPos.
                }
                // Idle и Search сюда тоже попадают, когда не видят игрока —
                // Idle и так не преследует (ничего не меняем), а Search сама
                // решает, когда завершиться (см. движение ниже); раньше
                // здесь было unconditional "else { m_state = Idle; }",
                // которое перезаписывало бы Search на каждом тике
                // восприятия, не дав ему вообще доиграть.

                // ---- Слух — независимо от зрения/FOV выше (звук идёт
                // сквозь стены). Только пока враг НИЧЕГО не преследует —
                // иначе (Walk/Run/Search) это не добавляет новой
                // информации. Не мгновенная погоня — идёт ПРОВЕРИТЬ шум
                // (SEARCH), не срывается сразу в Run/Scream (см. большой
                // комментарий класса).
                //
                // !playerInvisible — dev-tools ("чтобы на I враг ещё и
                // не слышал игрока", клавиша I, см. PlayerController::
                // invisibleToEnemy()). Изначально этот флаг гасил только
                // зрение (см. выше по файлу, где форсируется
                // canSee=false) — по отдельному запросу расширили и на
                // слух, теперь I значит полную неприметность, а не
                // только невидимость в узком смысле слова.
                if (!playerInvisible &&
                    m_state == AIState::Idle && (playerIsMoving || playerIsRunning))
                {
                    const float kHearingRadiusWalk = 4.0f;
                    const float kHearingRadiusRun = 7.0f;
                    const float hearingRadius = playerIsRunning ? kHearingRadiusRun : kHearingRadiusWalk;
                    if (dist <= hearingRadius)
                    {
                        m_state = AIState::Search;
                        m_lastKnownPlayerPos = playerPos; // "звук донёсся оттуда"
                        m_searchPaused = false;
                        m_searchPauseTimer = 0.0f;
                        m_path.clear();

                        // Та же цепочка гипотез, что и после потери
                        // погони (см. выше) — если игрок в момент шума
                        // двигался, враг заодно прикинет, куда тот мог
                        // побежать дальше, а не только сходит к точке
                        // самого звука.
                        m_searchWaypoints.clear();
                        m_searchWaypoints.push_back(m_lastKnownPlayerPos);
                        const bool headingWasFresh = m_timeSinceMeaningfulMovement <= kFleeGuessMovementMemory;
                        appendGuessedFleeWaypoints(m_lastKnownPlayerPos, m_playerHeading, headingWasFresh, m_searchWaypoints);
                        m_searchWaypointIndex = 0;
                    }
                }
            }
        }
    }

    // ---- Попытка сорваться в рывок — только находясь в Run ----
    if (m_state == AIState::Run)
    {
        m_dashRollTimer += deltaTime;
        if (m_dashRollTimer >= 1.0f)
        {
            m_dashRollTimer = 0.0f;

            // Направление — смесь "куда игрок движется" (упреждение по
            // оценённой скорости, kEstimatedPlayerSpeed выше) и "где
            // игрок СЕЙЧАС" (единый вектор из predictedPlayerPos, не два
            // отдельных слагаемых). Раньше было чисто m_playerHeading —
            // если игрок стоял на месте или резко свернул за мгновение
            // до броска, рывок улетал мимо, "как будто наугад" (см.
            // большой комментарий класса в EnemyAI.h).
            const float kLeadTime = 0.35f;
            const glm::vec3 predictedPlayerPos = playerPos + m_playerHeading * kEstimatedPlayerSpeed * kLeadTime;
            glm::vec3 toPredicted = predictedPlayerPos - m_position;
            toPredicted.y = 0.0f;
            const float toPredictedLen = glm::length(toPredicted);
            const glm::vec3 candidateDir = toPredictedLen > 0.001f
                ? toPredicted / toPredictedLen
                : m_playerHeading;

            glm::vec3 toPlayerNow = playerPos - m_position;
            toPlayerNow.y = 0.0f;
            const float distToPlayerNow = glm::length(toPlayerNow);

            // БАГФИКС ("рывок вслепую выглядит тупо"): раньше шанс не
            // зависел от того, есть ли вообще куда бежать — рывок мог
            // сорваться, даже когда игрок стоял вплотную к стене по
            // выбранному курсу, и на первом же кадре превращался в
            // Wall_slam — смотрелось как баг, а не как риск. Проверяем
            // клетку в паре юнитов впереди по курсу; если там сразу
            // стена, в этот раз не срываемся. Курс по-прежнему НЕ
            // пересчитывается на лету (в этом суть Dash), так что риск
            // влететь в стену НА ХОДУ никуда не делся — отсеивается
            // только заведомо мгновенный, "у порога" промах.
            bool hasRoomAhead = true;
            if (m_map)
            {
                const float kMinClearDistance = 1.5f;
                const glm::vec3 probePos = m_position + candidateDir * kMinClearDistance;
                const glm::ivec2 probeCell((int)std::floor(probePos.x), (int)std::floor(probePos.z));
                hasRoomAhead = isWalkableCell(probeCell.x, probeCell.y);
            }

            if (hasRoomAhead)
            {
                // Шанс растёт с приближением к игроку (было фиксировано
                // 15% независимо от дистанции) — рывок вплотную оправдан
                // тактически, рывок издалека почти всегда впустую.
                const float kBaseChance = 0.10f;
                const float kMaxChance = 0.25f;
                const float kDashChanceRange = 8.0f; // дистанция, начиная с которой — базовый шанс
                const float t = glm::clamp(1.0f - distToPlayerNow / kDashChanceRange, 0.0f, 1.0f);
                const float chance = kBaseChance + (kMaxChance - kBaseChance) * t;

                std::uniform_real_distribution<float> roll(0.0f, 1.0f);
                if (roll(m_rng) < chance)
                {
                    m_state = AIState::Dash;
                    m_dashDirection = candidateDir;
                    m_dashElapsed = 0.0f;
                }
            }
        }
    }
    else
    {
        m_dashRollTimer = 0.0f;
    }

    // ---- Движение ----
    if (m_state == AIState::Dash)
    {
        m_dashElapsed += deltaTime;

        const float dashSpeed = kEnemyRunSpeed * kDashSpeedMultiplier;
        const glm::vec3 nextPos = m_position + m_dashDirection * dashSpeed * deltaTime;

        // БАГФИКС ("анимация падения/Wall_slam при рывке пропала") —
        // раньше "врезался" определялось ТОЛЬКО тем, стала ли КЛЕТКА под
        // nextPos стеной (nextCell==1). После появления общего
        // цилиндрического коллайдера (resolveWallCollision(), см. общий
        // "хвост" update() и большой комментарий класса выше) позиция
        // выталкивается наружу, как только оказывается ближе
        // kCollisionRadius к границе стены — то есть враг застревал
        // прямо ПЕРЕД клеткой стены, так и не пересекая её границу, и
        // старый критерий "клетка стала стеной" никогда не срабатывал:
        // Dash тихо утыкался в невидимую оболочку коллайдера и просто
        // ждал 3 секунды до автоматического возврата в Run — БЕЗ
        // Wall_slam вообще, врагу как будто "нечем было упасть".
        //
        // Теперь "врезался" определяется через сам коллайдер: резолвим
        // nextPos и смотрим, оттолкнуло ли НАЗАД, ПРОТИВ направления
        // рывка — если да, враг реально во что-то упёрся (а не просто
        // чуть поджался, скользя вдоль стены по касательной, что тоже
        // может слегка триггерить коллайдер, но не должно засчитываться
        // как лобовое столкновение).
        const glm::vec3 resolvedNextPos = resolveWallCollision(nextPos);
        glm::vec3 pushback = resolvedNextPos - nextPos;
        pushback.y = 0.0f;
        const float kWallSlamPushbackThreshold = 0.02f; // люфт — не триггерить на касательном скольжении вдоль стены
        bool blocked = -glm::dot(pushback, m_dashDirection) > kWallSlamPushbackThreshold;

        // Коллайдер не проверяет выход за границы карты (клетки вне
        // грида просто пропускаются, см. resolveWallCollision()) — карта
        // в этой игре всегда окружена стенами по периметру, так что на
        // практике сюда дойти невозможно, но оставляем явную проверку на
        // всякий случай (тот же смысл, что раньше нёс общий критерий
        // "клетка вне грида").
        if (!blocked && m_map)
        {
            const glm::ivec2 nextCell((int)std::floor(nextPos.x), (int)std::floor(nextPos.z));
            blocked = nextCell.x < 0 || nextCell.y < 0 || nextCell.x >= m_mapW || nextCell.y >= m_mapH;
        }

        glm::vec3 toPlayer = playerPos - m_position;
        toPlayer.y = 0.0f;
        const float distToPlayer = glm::length(toPlayer);

        if (m_attackCooldownTimer > 0.0f)
            m_attackCooldownTimer -= deltaTime;

        if (m_attackCooldownTimer <= 0.0f && distToPlayer < kCatchRadius)
        {
            m_state = AIState::Attack;
            m_stateTimer = 1.25f;
            m_justCaughtPlayer = true;
            m_audio.playAttack(DistanceVolume(distToPlayer, kFootstepAudibleNear, kFootstepAudibleFar));
        }
        else if (blocked)
        {
            m_state = AIState::WallSlam;
            m_stateTimer = 2.67f;
            // distToPlayer уже посчитан выше (расстояние от врага до
            // РЕАЛЬНОЙ playerPos в момент столкновения) — та же
            // дистанция, что использовалась для проверки поимки чуть
            // выше, годится и для громкости удара.
            m_audio.playWallSlam(DistanceVolume(distToPlayer, kFootstepAudibleNear, kFootstepAudibleFar));
        }
        else if (m_dashElapsed > 3.0f)
        {
            m_state = AIState::Run;
            m_path.clear();
        }
        else
        {
            m_position = nextPos;
            // ЭТОТ снап — намеренный, в отличие от followPath() выше:
            // m_dashDirection ФИКСИРОВАН на весь Dash (см. большой
            // комментарий класса в EnemyAI.h), поэтому это всего один
            // мгновенный разворот лицом в сторону рывка В МОМЕНТ его
            // начала, а не повторяющееся "телепортирование" на каждый
            // пересчёт пути — визуально читается как резкий агрессивный
            // бросок, что тут и нужно, а не как баг поиска пути.
            m_yawDegrees = glm::degrees(std::atan2(m_dashDirection.x, m_dashDirection.z)); // тот же фикс осей, что и в followPath()
        }
    }
    else if (m_state == AIState::Walk || m_state == AIState::Run)
    {
        if (m_attackCooldownTimer > 0.0f)
            m_attackCooldownTimer -= deltaTime;

        // Поимка проверяется по РЕАЛЬНОЙ (не "последней известной")
        // позиции игрока — застать врасплох по памяти нельзя, только
        // если реально рядом. Плюс кулдаун после предыдущей атаки (см.
        // m_attackCooldownTimer в EnemyAI.h) — не бьёт мгновенно ещё раз.
        glm::vec3 toPlayerReal = playerPos - m_position;
        toPlayerReal.y = 0.0f;

        if (m_attackCooldownTimer <= 0.0f && glm::length(toPlayerReal) < kCatchRadius)
        {
            m_state = AIState::Attack;
            m_stateTimer = 1.25f;
            m_justCaughtPlayer = true;
            m_audio.playAttack(DistanceVolume(glm::length(toPlayerReal), kFootstepAudibleNear, kFootstepAudibleFar));
        }
        else
        {
            // Идём к последней ИЗВЕСТНОЙ позиции — совпадает с реальной,
            // когда игрок виден прямо сейчас (см. восприятие выше:
            // m_lastKnownPlayerPos обновляется на живую playerPos при
            // каждом успешном canSee), и остаётся "застывшей" на месте
            // последнего наблюдения всё время действия периода памяти.
            const float speed = (m_state == AIState::Run) ? kEnemyRunSpeed : kEnemyWalkSpeed;
            followPath(m_lastKnownPlayerPos, speed, deltaTime);
        }
    }
    else if (m_state == AIState::Search)
    {
        // Идём проверить точки из m_searchWaypoints по очереди — [0]
        // всегда место шума/последняя видимая позиция игрока, дальше —
        // 0-2 додуманные гипотезы побега (см. "ГИПОТЕЗЫ ПОБЕГА" и
        // appendGuessedFleeWaypoints() в большом комментарии класса в
        // EnemyAI.h). Поимка/повторное обнаружение сюда не долетают —
        // это уже решает восприятие выше (переводит state в Attack/
        // Scream/Walk ДО того, как выполнение доходит до этого блока).
        //
        // Страховка: если по какой-то причине цепочка пуста (не должно
        // происходить — оба места входа в Search всегда кладут хотя бы
        // m_lastKnownPlayerPos первым), не застреваем в этом состоянии
        // навсегда, а сразу возвращаемся к патрулю.
        if (m_searchWaypoints.empty())
        {
            m_state = AIState::Idle;
            m_hasPatrolTarget = false;
            m_path.clear();
        }
        else if (m_searchPaused)
        {
            m_searchPauseTimer -= deltaTime;
            if (m_searchPauseTimer <= 0.0f)
            {
                m_searchPaused = false;
                ++m_searchWaypointIndex;
                if (m_searchWaypointIndex >= m_searchWaypoints.size())
                {
                    // Все гипотезы проверены, игрока нигде не нашёл —
                    // сдаётся и уходит патрулировать.
                    m_state = AIState::Idle;
                    m_hasPatrolTarget = false; // форсируем выбор новой точки патруля
                    m_path.clear();
                }
                else
                {
                    m_path.clear(); // следующая гипотеза — считаем путь заново
                }
            }
        }
        else
        {
            const glm::vec3& target = m_searchWaypoints[m_searchWaypointIndex];
            const bool reached = followPath(target, kEnemyWalkSpeed, deltaTime);
            if (reached)
            {
                m_searchPaused = true;

                // У ПОСЛЕДНЕЙ точки в цепочке — прежняя, более долгая
                // "оглядывается" (это финальная проверка перед тем, как
                // сдаться совсем). У промежуточных додуманных точек —
                // короткий "заглянул сюда, никого — дальше" взгляд,
                // иначе Search растягивается неоправданно долго на
                // каждой гипотезе.
                const bool isLastWaypoint = (m_searchWaypointIndex + 1 >= m_searchWaypoints.size());
                std::uniform_real_distribution<float> pauseDist(
                    isLastWaypoint ? 1.5f : 0.7f,
                    isLastWaypoint ? 3.0f : 1.4f);
                m_searchPauseTimer = pauseDist(m_rng);
            }
        }
    }
    else // Idle — патруль (см. большой комментарий класса в EnemyAI.h)
    {
        if (m_patrolPaused)
        {
            m_patrolPauseTimer -= deltaTime;
            if (m_patrolPauseTimer <= 0.0f)
            {
                m_patrolPaused = false;
                m_hasPatrolTarget = false; // форсируем выбор новой точки ниже
            }
        }
        else
        {
            if (!m_hasPatrolTarget)
            {
                m_patrolTarget = pickRandomPatrolPoint();
                m_hasPatrolTarget = true;
                m_path.clear(); // форсируем пересчёт пути к НОВОЙ точке
            }

            const bool reached = followPath(m_patrolTarget, kEnemyWalkSpeed, deltaTime);
            if (reached)
            {
                // Недолгая пауза у точки (осмотреться), прежде чем
                // выбрать следующую — не мечется без остановки.
                m_patrolPaused = true;
                std::uniform_real_distribution<float> pauseDist(1.0f, 2.5f);
                m_patrolPauseTimer = pauseDist(m_rng);
            }
        }
    }

    // Коллайдер — тут же, в общем "хвосте" update() для Idle/Search/
    // Walk/Run/Dash (Attack/WallSlam/Scream резолвят его сами, см. их
    // ранние return'ы выше) — единая точка, через которую ЛЮБОЕ
    // движение этого кадра проходит перед тем, как попасть в
    // character.setPosition(). См. большой комментарий у
    // resolveWallCollision() в EnemyAI.h/.cpp.
    m_position = resolveWallCollision(m_position);
    character.setPosition(m_position);
    character.setYawDegrees(m_yawDegrees);
    if (m_state == AIState::Idle || m_state == AIState::Search)
    {
        // Стон — ТОЛЬКО пока враг сам ищет/бродит (патруль в Idle или
        // Search — обход последней известной позиции/гипотез побега,
        // см. большой комментарий класса выше), не во время настоящей
        // погони: во время Walk/Run/Dash после обнаружения уже играет
        // Scream в момент срыва (см. восприятие выше), второй голосовой
        // слой поверх был бы лишним и не то, о чём просили ("изредка
        // издаёт эти звуки, когда пытается найти врага" — это именно
        // про поиск, не про саму погоню). Рычание в погоне сознательно
        // НЕ реализовано (см. EnemyAudio::playMoan() — метод playGrowl()
        // всё ещё существует в EnemyAudio.h, но здесь больше не
        // вызывается).
        m_moanTimer += deltaTime;
        if (m_moanTimer >= m_nextMoanInterval)
        {
            m_moanTimer = 0.0f;
            std::uniform_real_distribution<float> intervalDist(9.0f, 18.0f);
            m_nextMoanInterval = intervalDist(m_rng);
            const float distToPlayer = glm::length(glm::vec2(playerPos.x - m_position.x, playerPos.z - m_position.z));
            m_audio.playMoan(DistanceVolume(distToPlayer, kMoanAudibleNear, kMoanAudibleFar));
        }
    }
    else
    {
        // Вышел из поиска (нашёл/поймал/сорвался в погоню) — следующий
        // стон отсчитывается заново, не "доедает" старый накопленный
        // таймер, как только враг вернётся к поиску.
        m_moanTimer = 0.0f;
    }

    EnemyCharacter::State animState = EnemyCharacter::State::Idle;
    // БАГФИКС: Attack/WallSlam здесь раньше не было ветки вообще — на
    // ТОМ САМОМ кадре, когда состояние переключается в Attack/WallSlam
    // (это происходит выше, в блоке движения, ПОСЛЕ ранних return'ов
    // Attack/WallSlam/Scream наверху функции, которые проверяются только
    // ОДИН раз в начале update()), эта ветка молча откатывала анимацию
    // на Idle вместо реального нового состояния — на один кадр враг
    // "дёргался" в чужую позу перед тем, как на следующем кадре ранний
    // return наверху всё же подхватывал правильную анимацию.
    //
    // Патруль (Idle, но реально идущий к точке, не стоящий на паузе) —
    // визуально это тоже "ходьба", просто спокойная, не встревоженная;
    // отдельного клипа для этого нет, переиспользуем Walk_Nervous, пока
    // не поставлен на паузу (тогда — Idle_Watchful, как и раньше).
    if (m_state == AIState::Idle)
        animState = m_patrolPaused ? EnemyCharacter::State::Idle : EnemyCharacter::State::Walk;
    else if (m_state == AIState::Search)
        // Тот же приём переиспользования клипов, что и у патруля выше —
        // отдельного клипа "ищет/оглядывается" у модели нет.
        animState = m_searchPaused ? EnemyCharacter::State::Idle : EnemyCharacter::State::Walk;
    else if (m_state == AIState::Walk)
        animState = EnemyCharacter::State::Walk;
    else if (m_state == AIState::Run || m_state == AIState::Dash)
        animState = EnemyCharacter::State::Run;
    else if (m_state == AIState::Attack)
        animState = EnemyCharacter::State::Attack;
    else if (m_state == AIState::WallSlam)
        animState = EnemyCharacter::State::WallSlam;
    else if (m_state == AIState::Scream)
        animState = EnemyCharacter::State::Scream;

    // ---- Шаги врага (см. audio/EnemyAudio.h::playFootstepWalk()/
    // playFootstepRun()) ----
    // Кадансим по РЕАЛЬНО пройденному за этот кадр расстоянию (после
    // пути/коллайдера выше), точно как FootstepAudio у игрока (см.
    // PlayerController.cpp) — привязано к тому, что АНИМАЦИЯ реально
    // показывает СЕЙЧАС (animState), а не к внутреннему AIState:
    // патруль и Search формально не AIState::Walk, но визуально это та
    // же анимация Walk_Nervous (см. переиспользование клипов выше по
    // файлу) — она тоже должна топать. Attack/WallSlam/Scream сюда не
    // попадают вообще (ранние return'ы этой функции выше), так что
    // отдельно исключать их не нужно.
    const bool footstepIsRun = (animState == EnemyCharacter::State::Run);
    const bool footstepIsWalking = footstepIsRun || (animState == EnemyCharacter::State::Walk);

    if (footstepIsWalking)
    {
        const glm::vec2 deltaXZ(
            m_position.x - footstepStartPos.x,
            m_position.z - footstepStartPos.z
        );
        const float movedDistance = glm::length(deltaXZ);

        if (movedDistance > 0.00001f)
        {
            const bool modeChanged =
                !m_footstepWasMoving ||
                (footstepIsRun != m_footstepWasRunning);

            // Громкость по дистанции ("враг далеко — звука нет, ближе —
            // чуть слышно, близко — хорошо слышно") — считаем один раз
            // за кадр, а не заново на каждый отдельный шаг из while ниже:
            // расстояние игрок-враг не успевает заметно измениться в
            // пределах одного кадра, даже если шаг сработал несколько
            // раз подряд (быстрый рывок при низком FPS).
            const float distToPlayer = glm::length(glm::vec2(playerPos.x - m_position.x, playerPos.z - m_position.z));
            const float footstepVolume = DistanceVolume(distToPlayer, kFootstepAudibleNear, kFootstepAudibleFar);

            // Новый рывок движения (или смена Walk<->Run, например при
            // срыве в погоню или после Wall_slam) — сразу же шаг, ритм
            // считается заново от него, а не от произвольной фазы
            // предыдущего состояния.
            if (modeChanged)
            {
                m_footstepDistance = 0.0f;
                if (footstepIsRun)
                    m_audio.playFootstepRun(footstepVolume);
                else
                    m_audio.playFootstepWalk(footstepVolume);
            }

            m_footstepWasMoving = true;
            m_footstepWasRunning = footstepIsRun;
            m_footstepDistance += movedDistance;

            // Более длинный, тяжёлый шаг, чем у игрока (0.68/0.95) —
            // THE WRAPPED крупнее и тяжелее, шаг реже, но весомее.
            const float kEnemyWalkStepLength = 0.75f;
            const float kEnemyRunStepLength = 1.10f;
            const float stepLength = footstepIsRun ? kEnemyRunStepLength : kEnemyWalkStepLength;
            while (m_footstepDistance >= stepLength)
            {
                m_footstepDistance -= stepLength;
                if (footstepIsRun)
                    m_audio.playFootstepRun(footstepVolume);
                else
                    m_audio.playFootstepWalk(footstepVolume);
            }
        }
        else
        {
            // Стоит на месте, но анимация всё ещё "ходьба" (пауза
            // патруля/Search — "оглядывается", см. animState выше) —
            // не копим фантомное расстояние, но и не считаем это
            // полноценной остановкой движения ради modeChanged: пауза
            // короткая, а следующий шаг сразу после неё не должен
            // звучать как "первый шаг с нуля" каждый раз.
        }
    }
    else
    {
        m_footstepWasMoving = false;
        m_footstepWasRunning = false;
        m_footstepDistance = 0.0f;
    }

    character.setState(animState);

    character.update(deltaTime);
}

bool EnemyAI::consumeJustCaughtPlayer()
{
    const bool result = m_justCaughtPlayer;
    m_justCaughtPlayer = false;
    return result;
}

const char* EnemyAI::debugStateName() const
{
    switch (m_state)
    {
        case AIState::Idle:     return m_patrolPaused ? "Idle(paused)" : "Idle(patrol)";
        case AIState::Search:   return m_searchPaused ? "Search(paused)" : "Search";
        case AIState::Walk:     return "Walk";
        case AIState::Run:      return "Run";
        case AIState::Dash:     return "Dash";
        case AIState::Attack:   return "Attack";
        case AIState::WallSlam: return "WallSlam";
        case AIState::Scream:   return "Scream";
    }
    return "?";
}

glm::vec3 EnemyAI::resolveWallCollision(const glm::vec3& pos) const
{
    if (!m_map)
        return pos;

    // Радиус "тела" врага для коллизии — см. большой комментарий у
    // объявления kCollisionRadius в EnemyAI.h (публичная константа —
    // тот же радиус использует PlayerController для коллизии игрок↔враг,
    // сознательно упрощённый квадрат вместо точной формы срезанных
    // углов; там же обоснование конкретного значения 0.30 — баланс
    // между "не протыкать стены визуально" и "дать игроку хоть какой-то
    // шанс протиснуться в тупике", см. PlayerController::kCollisionRadius
    // тоже уменьшенный, до 0.10, ради того же).

    glm::vec3 result = pos;

    // Несколько итераций — выталкивание из одной стены может занести
    // позицию в зону пересечения с соседней (типичная ситуация во
    // внутренних углах коридора) — 3 прохода достаточно на практике для
    // клеток такого размера относительно радиуса.
    for (int iter = 0; iter < 3; ++iter)
    {
        bool anyPush = false;

        const int cx = (int)std::floor(result.x);
        const int cz = (int)std::floor(result.z);

        // 3x3 клетки вокруг текущей — с запасом (радиус меньше клетки,
        // дальше физически пересечься не может).
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int x = cx + dx;
                const int z = cz + dz;
                if (x < 0 || z < 0 || x >= m_mapW || z >= m_mapH)
                    continue;
                if ((*m_map)[(size_t)z * m_mapW + x] != 1)
                    continue; // не стена — нечего резолвить

                // Ближайшая точка на квадрате клетки [x,x+1]x[z,z+1] к
                // центру окружности — тот же приём closest-point
                // (circle-vs-AABB), что и SquareOverlapsCircle в
                // PlayerController.cpp, просто клетка стены тут в роли
                // препятствия, а не колонна/пьедестал.
                const float closestX = glm::clamp(result.x, (float)x, (float)x + 1.0f);
                const float closestZ = glm::clamp(result.z, (float)z, (float)z + 1.0f);
                const float dxp = result.x - closestX;
                const float dzp = result.z - closestZ;
                const float distSq = dxp * dxp + dzp * dzp;

                if (distSq >= kCollisionRadius * kCollisionRadius)
                    continue;

                float dist = std::sqrt(distSq);
                glm::vec2 pushDir;
                if (dist > 1e-5f)
                {
                    pushDir = glm::vec2(dxp, dzp) / dist;
                }
                else
                {
                    // Центр окружности ровно на границе клетки (или уже
                    // внутри неё, dist==0 — например, что-то поставило
                    // врага прямо в стену ДО этого фикса, сейв со старой
                    // версии и т.п.) — направление выталкивания
                    // геометрически не определено через closest-point,
                    // толкаем от центра клетки-стены как разумный
                    // дефолт (в противном случае pushDir был бы (0,0) и
                    // враг остался бы внутри стены навсегда).
                    const glm::vec2 fromCellCenter(result.x - (x + 0.5f), result.z - (z + 0.5f));
                    const float fromCellCenterLen = glm::length(fromCellCenter);
                    pushDir = fromCellCenterLen > 1e-5f
                        ? fromCellCenter / fromCellCenterLen
                        : glm::vec2(1.0f, 0.0f);
                    dist = 0.0f;
                }

                const float penetration = kCollisionRadius - dist;
                result.x += pushDir.x * penetration;
                result.z += pushDir.y * penetration;
                anyPush = true;
            }
        }

        // ---- Колонны — обе формы круглые, обычный circle-vs-circle ----
        if (m_columnCentersXZ)
        {
            for (const glm::vec2& col : *m_columnCentersXZ)
            {
                const glm::vec2 d(result.x - col.x, result.z - col.y);
                const float dist = glm::length(d);
                const float minDist = kCollisionRadius + m_columnRadius;
                if (dist >= minDist)
                    continue;

                const glm::vec2 pushDir = dist > 1e-5f ? d / dist : glm::vec2(1.0f, 0.0f);
                const float penetration = minDist - dist;
                result.x += pushDir.x * penetration;
                result.z += pushDir.y * penetration;
                anyPush = true;
            }
        }

        // ---- Игрок — тоже круглое препятствие, circle-vs-circle ----
        // БАГФИКС ("игрок может застрять между стеной и врагом") — раньше
        // враг вообще не считал игрока препятствием: PlayerController уже
        // не пускал игрока НАВСТРЕЧУ врагу (см. tryMove()/EnemyAI::
        // kCollisionRadius там), но сам враг мог свободно НАЙТИ игрока
        // (пока преследует — followPath() ведёт ровно на его позицию) и
        // "занять" его место. Если игрока при этом ещё и прижало к
        // стене — ему было физически некуда сдвинуться: ЛЮБАЯ новая
        // позиция игрока всё ещё пересекалась с врагом (он уже стоял
        // прямо там), а отступить назад мешала стена. Теперь враг тоже
        // выталкивается из игрока (symметрично тому, что игрок уже не
        // может влезть во врага) — оба тела взаимно не пересекаются, и
        // "сэндвич" стена+враг больше не может замкнуться наглухо.
        //
        // m_lastPlayerPos — свежая позиция игрока НА ЭТОТ КАДР: она
        // безусловно обновляется в самом начале update() (см. код выше в
        // update(), до всех веток состояний), так что к моменту вызова
        // resolveWallCollision() отсюда — из ЛЮБОЙ ветки update() в этом
        // же кадре — она уже гарантированно актуальна, отдельный параметр
        // не нужен. PlayerController::kCollisionRadius — та же публичная
        // константа, которую уже использует сам PlayerController (см.
        // симметричный БАГФИКС там же, в tryMove()/findSlideNormal()).
        {
            const glm::vec2 playerXZ(m_lastPlayerPos.x, m_lastPlayerPos.z);
            const glm::vec2 d(result.x - playerXZ.x, result.z - playerXZ.y);
            const float dist = glm::length(d);
            const float minDist = kCollisionRadius + PlayerController::kCollisionRadius;
            if (dist < minDist)
            {
                const glm::vec2 pushDir = dist > 1e-5f ? d / dist : glm::vec2(1.0f, 0.0f);
                const float penetration = minDist - dist;
                result.x += pushDir.x * penetration;
                result.z += pushDir.y * penetration;
                anyPush = true;
            }
        }

        if (!anyPush)
            break; // уже нигде не пересекается — дальше крутить итерации незачем
    }

    return result;
}
