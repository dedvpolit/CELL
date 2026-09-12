#include "PlayerController.h"
#include "EnemyAI.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <cstdio>
#include <algorithm>

#if __has_include("dev/DevTools.h")
#include "dev/DevTools.h"
#endif
#ifdef DEV_TOOLS_ACTIVE
#define HAS_DEV_TOOLS 1
#endif

// Правильный (не по 4 точкам) тест пересечения круга и осевого квадрата:
// ближайшая к центру круга точка на квадрате — это центр круга, зажатый
// (clamp) в границы квадрата по каждой оси отдельно; если расстояние от
// центра круга до этой ближайшей точки меньше радиуса — есть пересечение.
// Раньше (см. историю правок) круглые препятствия (пьедестал кнопки
// победы, теперь и колонны) проверялись только в 4 угловых точках
// квадрата игрока — это ТОЧНО для стен (клетки толще игрока, см.
// комментарий в isBlocked), но НЕ точно для круга: если центр круга
// находится напротив середины стороны квадрата, а не угла, ни одна из 4
// угловых проверок его не увидит, хотя реальное пересечение уже есть
// (числовой пример см. в истории правок). Один этот тест на препятствие,
// а не 4 точечных, закрывает дыру полностью.
static bool SquareOverlapsCircle(float squareCenterX, float squareCenterZ, float halfExtent,
                                  const glm::vec2& circleCenter, float radius) {
    const float closestX = std::clamp(circleCenter.x, squareCenterX - halfExtent, squareCenterX + halfExtent);
    const float closestZ = std::clamp(circleCenter.y, squareCenterZ - halfExtent, squareCenterZ + halfExtent);
    const float dx = circleCenter.x - closestX;
    const float dz = circleCenter.y - closestZ;
    return dx * dx + dz * dz < radius * radius;
}

// ~ радиус меша пьедестала (0.35) + небольшой запас — вынесено в
// константу (было дублирующимся магическим числом в tryMove и теперь
// ещё и в findSlideNormal, см. ниже).
static constexpr float kWinButtonCollisionRadius = 0.42f;

// Нормаль диагонали среза угла — та же геометрия, что и в
// SceneGeometry::AddChamferedWallCell (см. историю правок): держать в
// синхроне, если формула среза там изменится.
static glm::vec3 ChamferDiagonalNormal(WallShapes::CornerCut cut) {
    switch (cut) {
        case WallShapes::CornerCut::SW: return glm::normalize(glm::vec3(-1, 0, -1));
        case WallShapes::CornerCut::SE: return glm::normalize(glm::vec3(1, 0, -1));
        case WallShapes::CornerCut::NE: return glm::normalize(glm::vec3(1, 0, 1));
        case WallShapes::CornerCut::NW: return glm::normalize(glm::vec3(-1, 0, 1));
        default: return glm::vec3(0.0f);
    }
}

bool PlayerController::isBlocked(float x, float z, const std::function<bool(int, int)>& isFloor,
                                 const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                                 const std::function<float(int, int)>& getChamferSize) const {
    int cx = (int)std::floor(x);
    int cz = (int)std::floor(z);

    if (!isFloor(cx, cz)) {
        // Клетка не пол — обычно это "сплошная стена, точка заблокирована".
        // НО если у этой клетки срезан угол (WallShapes), часть её площади
        // на самом деле открыта (клин среза) — тест ниже читает РОВНО ТУ
        // ЖЕ математику (WallShapes::IsLocalPointSolid) И ТОТ ЖЕ размер
        // среза, что использует рендер (см. SceneGeometry::
        // AddChamferedWallCell), поэтому коллизия физически не может
        // разойтись с силуэтом стены — даже для клеток из диагональных
        // "лестниц" с намного бОльшим срезом (см. WallShapes::
        // kChamferSizeChain).
        const WallShapes::CornerCut cut = getCornerCut(cx, cz);
        if (cut == WallShapes::CornerCut::None)
            return true;

        const float chamferSize = getChamferSize ? getChamferSize(cx, cz) : WallShapes::kChamferSize;
        const float localX = x - (float)cx;
        const float localZ = z - (float)cz;
        if (WallShapes::IsLocalPointSolid(localX, localZ, cut, chamferSize))
            return true;
        // иначе точка попала в вырезанный клин — не блокируем.
    }

    return false;
}

bool PlayerController::tryMove(glm::vec3& pos, glm::vec3 delta,
                               const std::function<bool(int, int)>& isFloor,
                               const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                               const std::function<float(int, int)>& getChamferSize,
                               const std::vector<glm::vec2>& columnCentersXZ,
                               const glm::vec3& winButtonPos,
                               const std::vector<glm::vec3>& enemyPositions) const {
    glm::vec3 newPos = pos + delta;
    const float r = kCollisionRadius;

    // Стены/срезанные углы — 4 угловые точки квадрата игрока. Это ТОЧНО
    // (не приближённо) для осевых прямоугольных клеток и их клиньев-срезов,
    // т.к. толщина стены (1 юнит) больше диаметра игрока (2r) — см.
    // комментарий в isBlocked() выше.
    if (isBlocked(newPos.x - r, newPos.z - r, isFloor, getCornerCut, getChamferSize)) return false;
    if (isBlocked(newPos.x + r, newPos.z - r, isFloor, getCornerCut, getChamferSize)) return false;
    if (isBlocked(newPos.x - r, newPos.z + r, isFloor, getCornerCut, getChamferSize)) return false;
    if (isBlocked(newPos.x + r, newPos.z + r, isFloor, getCornerCut, getChamferSize)) return false;

    // Круглые препятствия (колонны + пьедестал кнопки победы + враг) —
    // ОДИН правильный circle-vs-square тест на препятствие, а НЕ 4
    // точечных (см. SquareOverlapsCircle выше и историю правок про дыру
    // в старой 4-точечной проверке).
    for (const glm::vec2& col : columnCentersXZ) {
        if (SquareOverlapsCircle(newPos.x, newPos.z, r, col, Columns::kColumnRadius))
            return false;
    }
    // ~ радиус меша пьедестала (0.35) + небольшой запас, как и раньше.
    if (SquareOverlapsCircle(newPos.x, newPos.z, r,
                              glm::vec2(winButtonPos.x, winButtonPos.z), kWinButtonCollisionRadius))
        return false;
    // БАГФИКС ("игрок может проходить сквозь противника") — тело врага,
    // тот же приём, что и колонны/пьедестал выше. EnemyAI::kCollisionRadius
    // — ОДНА константа с самим коллайдером врага против стен (см. её
    // комментарий в EnemyAI.h), не отдельное магическое число здесь.
    //
    // БАГФИКС #2 ("игрок иногда застревает во враге") — раньше блокировка
    // была безусловной: ЛЮБОЕ движение в пересекающуюся с врагом позицию
    // отклонялось, даже если оно вело ПРОЧЬ от врага. Если игрок и враг
    // всё же оказались ближе минимальной дистанции не по вине текущего
    // движения игрока (враг сам зашёл вплотную — его
    // EnemyAI::resolveWallCollision() не всегда может полностью
    // оттолкнуться от игрока, если сразу за игроком стена, — теснит
    // враг+стена с двух сторон, и полного разрешения может не хватить),
    // у игрока не оставалось вообще ни одной "легальной" соседней точки:
    // любое направление всё ещё пересекало круг врага — softlock. Теперь
    // приближаться ближе минимальной дистанции нельзя, а ОТДАЛЯТЬСЯ можно
    // всегда, даже если новая позиция формально ещё внутри круга — так
    // игрок гарантированно может выбраться, а не застрять навсегда на
    // границе.
    //
    // УЛУЧШЕНИЕ ("4 врага по всей карте") — теперь список, а не одна
    // точка: проверяем КАЖДОГО врага независимо, тем же правилом
    // "приближение к КОНКРЕТНО этому врагу запрещено, отдаление всегда
    // разрешено" — так что застрять между двумя РАЗНЫМИ врагами
    // одновременно тоже невозможно (для каждого свой approaching-тест).
    for (const glm::vec3& enemyPos : enemyPositions) {
        const glm::vec2 enemyXZ(enemyPos.x, enemyPos.z);
        const glm::vec2 curXZ(pos.x, pos.z);
        const glm::vec2 newXZ(newPos.x, newPos.z);
        const float curDistSq = glm::dot(curXZ - enemyXZ, curXZ - enemyXZ);
        const float newDistSq = glm::dot(newXZ - enemyXZ, newXZ - enemyXZ);
        // Небольшой допуск (1e-6), чтобы чисто касательное/на месте
        // движение тоже считалось "не приближением", а не блокировалось
        // из-за погрешности округления.
        const bool approaching = newDistSq < curDistSq - 1e-6f;
        if (approaching &&
            SquareOverlapsCircle(newPos.x, newPos.z, r, enemyXZ, EnemyAI::kCollisionRadius))
            return false;
    }

    pos = newPos;
    return true;
}

bool PlayerController::findSlideNormal(float x, float z,
                                        const std::function<bool(int, int)>& isFloor,
                                        const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                                        const std::function<float(int, int)>& getChamferSize,
                                        const std::vector<glm::vec2>& columnCentersXZ,
                                        const glm::vec3& winButtonPos,
                                        const std::vector<glm::vec3>& enemyPositions,
                                        glm::vec3& outNormal) const {
    const float r = kCollisionRadius;

    // Круглые препятствия — нормаль тривиальна: от центра круга к игроку.
    // Проверяем ИХ первыми (не по приоритету, а просто порядок): колонны
    // реже, чем стены, поэтому дешевле отсеять их первыми на типичном
    // кадре, где никакого круглого препятствия рядом нет вовсе.
    for (const glm::vec2& col : columnCentersXZ) {
        if (SquareOverlapsCircle(x, z, r, col, Columns::kColumnRadius)) {
            const glm::vec2 d(x - col.x, z - col.y);
            if (glm::dot(d, d) > 1e-8f) {
                const glm::vec2 n = glm::normalize(d);
                outNormal = glm::vec3(n.x, 0.0f, n.y);
                return true;
            }
        }
    }
    {
        const glm::vec2 winXZ(winButtonPos.x, winButtonPos.z);
        if (SquareOverlapsCircle(x, z, r, winXZ, kWinButtonCollisionRadius)) {
            const glm::vec2 d(x - winXZ.x, z - winXZ.y);
            if (glm::dot(d, d) > 1e-8f) {
                const glm::vec2 n = glm::normalize(d);
                outNormal = glm::vec3(n.x, 0.0f, n.y);
                return true;
            }
        }
    }
    {
        // Враги — тот же приём, что и колонны/кнопка выше (см. БАГФИКС в
        // tryMove()) — без этого игрок мог бы гладко ОБТЕКАТЬ (slide)
        // модель врага только за счёт X/Z-резерва в resolveMovement(),
        // теряя точный slide именно вдоль круглого тела, который дают
        // остальные круглые препятствия. Список из нескольких врагов
        // (см. "4 врага по всей карте") — берём первого попавшегося, чьё
        // тело реально мешает в этой точке; двух врагов вплотную друг к
        // другу в одной точке одновременно не бывает (они не толкают
        // друг друга, но и не сближаются настолько при типичном ИИ).
        for (const glm::vec3& enemyPos : enemyPositions) {
            const glm::vec2 enemyXZ(enemyPos.x, enemyPos.z);
            if (SquareOverlapsCircle(x, z, r, enemyXZ, EnemyAI::kCollisionRadius)) {
                const glm::vec2 d(x - enemyXZ.x, z - enemyXZ.y);
                if (glm::dot(d, d) > 1e-8f) {
                    const glm::vec2 n = glm::normalize(d);
                    outNormal = glm::vec3(n.x, 0.0f, n.y);
                    return true;
                }
            }
        }
    }

    // Диагональ среза угла — проверяем те же 4 угловые точки, что и
    // tryMove(), но теперь не просто "заблокировано да/нет", а какая
    // ИМЕННО клетка (если срезанная) блокирует, чтобы взять именно её
    // нормаль диагонали.
    const float px[4] = { x - r, x + r, x - r, x + r };
    const float pz[4] = { z - r, z - r, z + r, z + r };
    for (int i = 0; i < 4; ++i) {
        const int cx = (int)std::floor(px[i]);
        const int cz = (int)std::floor(pz[i]);
        if (isFloor(cx, cz)) continue; // эта угловая точка не в стене вовсе

        const WallShapes::CornerCut cut = getCornerCut(cx, cz);
        if (cut == WallShapes::CornerCut::None) continue; // осевая стена — не наш случай

        const float localX = px[i] - (float)cx;
        const float localZ = pz[i] - (float)cz;
        const float chamferSize = getChamferSize ? getChamferSize(cx, cz) : WallShapes::kChamferSize;
        if (!WallShapes::IsLocalPointSolid(localX, localZ, cut, chamferSize))
            continue; // эта точка попала в открытый клин — не блокирует

        outNormal = ChamferDiagonalNormal(cut);
        return true;
    }

    return false;
}

void PlayerController::advanceSmoothedAnimDt(float rawDeltaTime)
{
    // Proper frame-rate-independent exponential smoothing: alpha is
    // derived from dt itself (not a fixed constant), so the REAL-TIME
    // smoothing window (tau) stays the same no matter how often this
    // is called. Standard "exponential decay" formula — see e.g.
    // Simon Dev / Filmic Games' write-ups on frame-independent lerp.
    //
    // tau = how long (seconds) it takes the smoothed value to settle
    // near a new steady-state deltaTime after a change (e.g. fps drop).
    // ~0.15s is short enough that a lasting fps change (like the 30fps
    // Performance Mode toggle, see WindowManager) is reflected quickly,
    // but long enough to iron out single-frame jitter/stutter spikes.
    const float tau = 0.15f;
    const float alpha = 1.0f - std::exp(-rawDeltaTime / tau);
    m_animSmoothedDt = m_animSmoothedDt + (rawDeltaTime - m_animSmoothedDt) * alpha;
}

void PlayerController::updateDeathSequence(float deltaTime)
{
    m_deathTime += deltaTime;

    // БАГФИКС ("факел остаётся в руке при смерти") — обычный блендинг
    // m_torchBlend (см. processInput()) во время смерти НЕ выполняется —
    // там ранний return сразу после updateDeathSequence() (см.
    // applyDamage()/большой комментарий у m_torchRaised там). Раз
    // единственное место, где m_torchBlend вообще двигается во время
    // смерти — здесь, дублируем тот же экспоненциальный блендинг, но
    // быстрее (kDeathTorchDropResponse > обычного torchResponse=2.5 в
    // processInput()) — факел не спокойно опускается, а именно ВЫРОНЕН,
    // читается как "выпал из руки", а не как обычное намеренное
    // опускание по ЛКМ.
    const float kDeathTorchDropResponse = 8.0f;
    const float torchAlpha = 1.0f - std::exp(-kDeathTorchDropResponse * deltaTime);
    m_torchBlend += (0.0f - m_torchBlend) * torchAlpha;
    if (m_torchBlend < 0.0005f)
        m_torchBlend = 0.0f;

    const float staminaStart = kDeathBounceDuration + kDeathFallDuration + kDeathRollDuration;
    const float staminaEnd = staminaStart + kDeathStaminaDrainDuration;

    if (m_deathTime >= staminaStart)
    {
        // "стамина игрока начнётся падать" — линейно до нуля за
        // kDeathStaminaDrainDuration, синхронно с фейдом экрана
        // (см. consumeDeathFadeTrigger() ниже — оба стартуют в один
        // момент, staminaStart).
        const float drainT = glm::clamp((m_deathTime - staminaStart) / kDeathStaminaDrainDuration, 0.0f, 1.0f);
        m_stamina = m_maxStamina * (1.0f - drainT);
    }

    if (m_deathTime >= staminaStart && !m_deathFadeTriggered)
    {
        // Взводится РОВНО один раз, в первый кадр, когда крен уже
        // доиграл — см. consumeDeathFadeTrigger(). Не ждём конца
        // дренажа стамины: фейд и падение стамины идут ОДНОВРЕМЕННО, а
        // не один после другого (см. запрос: "пока вместе с этим").
        m_deathFadeTriggered = true;
    }
}

float PlayerController::deathCameraYOffset() const
{
    if (!m_deathSequenceActive)
        return 0.0f;

    const float t = m_deathTime;

    if (t < kDeathBounceDuration)
    {
        // Быстрый рывок вверх — простая синусоидная ease-out кривая (доля
        // четверти периода синуса: быстрый старт, мягкая остановка на
        // пике), совсем небольшое расстояние.
        const float bt = t / kDeathBounceDuration;
        return std::sin(bt * 1.5707963f) * kDeathBounceHeight; // 1.5707963 = pi/2
    }

    const float fallStart = kDeathBounceDuration;
    if (t < fallStart + kDeathFallDuration)
    {
        // Падение С РАЗГОНОМ, не линейно — кубическая ease-in кривая:
        // медленный старт (как будто тело ещё пытается держаться),
        // резко ускоряется ближе к концу (как будто силы кончились
        // разом) — именно то самое "не линейно, а как будто действительно
        // упал от бессилия".
        const float ft = (t - fallStart) / kDeathFallDuration;
        const float eased = ft * ft * ft;
        return glm::mix(kDeathBounceHeight, -kDeathFallDistance, eased);
    }

    // Дошли до пола (роль/дренаж стамины ниже не двигают саму высоту).
    return -kDeathFallDistance;
}

float PlayerController::deathCameraRollDegrees() const
{
    if (!m_deathSequenceActive)
        return 0.0f;

    const float rollStart = kDeathBounceDuration + kDeathFallDuration;
    if (m_deathTime < rollStart)
        return 0.0f;

    // Крен начинается ТОЛЬКО после того, как падение (по высоте)
    // завершилось — "камера после этого повернётся на 90°", не
    // одновременно с падением.
    const float rt = glm::clamp((m_deathTime - rollStart) / kDeathRollDuration, 0.0f, 1.0f);
    // Ease-out (быстрый старт, мягкая остановка) — голова "укладывается"
    // на щеку, а не резко щёлкает на месте в конце.
    const float eased = 1.0f - (1.0f - rt) * (1.0f - rt);
    return eased * kDeathRollDegrees;
}

void PlayerController::resolveMovement(glm::vec3& pos, glm::vec3 delta,
                                       const std::function<bool(int, int)>& isFloor,
                                       const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                                       const std::function<float(int, int)>& getChamferSize,
                                       const std::vector<glm::vec2>& columnCentersXZ,
                                       const glm::vec3& winButtonPos,
                                       const std::vector<glm::vec3>& enemyPositions) const {
    if (glm::dot(glm::vec2(delta.x, delta.z), glm::vec2(delta.x, delta.z)) < 1e-12f)
        return; // нулевое перемещение — нечего разрешать

    // 1. Полный диагональный шаг сразу — самый гладкий случай: открытое
    //    пространство, или движение вдоль/от препятствия без пересечения.
    if (tryMove(pos, delta, isFloor, getCornerCut, getChamferSize, columnCentersXZ, winButtonPos, enemyPositions))
        return;

    // 2. Заблокировано диагональю среза угла или окружностью (колонна/
    //    кнопка победы/враг) — скользим вдоль поверхности (убираем
    //    составляющую delta вдоль нормали), вместо грубой "либо X целиком,
    //    либо Z целиком" (см. комментарий в PlayerController.h у
    //    resolveMovement про "лестницу" из мелких столкновений).
    glm::vec3 slideNormal;
    if (findSlideNormal(pos.x + delta.x, pos.z + delta.z, isFloor, getCornerCut, getChamferSize,
                         columnCentersXZ, winButtonPos, enemyPositions, slideNormal)) {
        const glm::vec3 slideDelta = delta - slideNormal * glm::dot(delta, slideNormal);
        if (tryMove(pos, slideDelta, isFloor, getCornerCut, getChamferSize, columnCentersXZ, winButtonPos, enemyPositions))
            return;
    }

    // 3. Резерв — прежнее поведение (независимые X и Z). Для обычной
    //    осевой стены это и так точный slide (см. isBlocked); здесь же —
    //    подстраховка на случаи, которые (2) не разрулил (например, острый
    //    вогнутый угол между двумя разными препятствиями).
    tryMove(pos, glm::vec3(delta.x, 0.0f, 0.0f), isFloor, getCornerCut, getChamferSize, columnCentersXZ, winButtonPos, enemyPositions);
    tryMove(pos, glm::vec3(0.0f, 0.0f, delta.z), isFloor, getCornerCut, getChamferSize, columnCentersXZ, winButtonPos, enemyPositions);
}

glm::vec3 PlayerController::getFront() const {
    const float headDropDegrees = -58.0f * m_poseBlend;
    const float effectivePitch = m_pitch + headDropDegrees;

    glm::vec3 f;
    f.x = std::cos(glm::radians(m_yaw)) * std::cos(glm::radians(effectivePitch));
    f.y = std::sin(glm::radians(effectivePitch));
    f.z = std::sin(glm::radians(m_yaw)) * std::cos(glm::radians(effectivePitch));
    return glm::normalize(f);
}


void PlayerController::processInput(GLFWwindow* window, float deltaTime,
                                     const std::function<bool(int, int)>& isFloor,
                                     const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                                     const std::function<float(int, int)>& getChamferSize,
                                     const std::vector<glm::vec2>& columnCentersXZ,
                                     const glm::vec3& winButtonPos,
                                     const std::vector<glm::vec3>& enemyPositions,
                                     const std::vector<glm::vec3>& diaryPositions,
                                     int diariesReadCount)
{
    m_menuCameraActive = false;

    // [comment corrupted in source file - original text lost/unrecoverable]
    deltaTime = glm::clamp(deltaTime, 0.0f, 0.05f);

    advanceSmoothedAnimDt(deltaTime);
    m_cameraAnimTime += m_animSmoothedDt;

    // V is an edge-triggered toggle: the compass is only taken out / put
    // away when the key is pressed, not while it is held.
    const bool vKeyDown = glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS;
    if (vKeyDown && !m_vKeyWasDown)
    {
        m_compassVisible = !m_compassVisible;
        m_poseTime = 0.0f;
    }
    m_vKeyWasDown = vKeyDown;

    // ЛКМ — тот же приём, что и V для компаса: факел в руке поднимается/
    // опускается по нажатию, не держится. Плавность (m_torchBlend) — см.
    // ниже, тем же экспоненциальным сближением, что и m_poseBlend.
    const bool lmbDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (lmbDown && !m_lmbWasDown)
    {
        m_torchRaised = !m_torchRaised;
    }
    m_lmbWasDown = lmbDown;

    // ---- Дев-инструменты (M/N/+/-/H/J/I), см. DevTools.h ----
    // Вся раскладка этих клавиш живёт в одном отдельном файле; здесь мы
    // только применяем результат к состоянию сцены. Если DevTools.h
    // удалён или DEV_TOOLS_ENABLED=0 — HAS_DEV_TOOLS не определён, и весь
    // блок ниже просто не компилируется: m_debugMapVisible/m_noclipEnabled
    // остаются в false навсегда, обычный игрок не получает дев-возможностей.
#ifdef HAS_DEV_TOOLS
    DevTools::ToggleDebugMap(window, m_debugMapVisible, m_mKeyWasDown);
    DevTools::ToggleInvisibleToEnemy(window, m_invisibleToEnemy, m_iKeyWasDown);

    const bool noclipWasEnabled = m_noclipEnabled;
    DevTools::ToggleNoclip(window, m_noclipEnabled, m_nKeyWasDown);
    if (noclipWasEnabled && !m_noclipEnabled)
    {
        // При выключении noclip возвращаем игрока на обычную высоту
        // глаз — иначе после полёта он мог бы остаться висеть в
        // воздухе, а обычный режим движения по Y ничего не корректирует
        // (в игре нет гравитации/вертикальной коллизии).
        m_camPos.y = 0.5f;
    }

    // +/- регулируют дальность обзора, но только пока активен noclip —
    // см. DevTools::UpdateViewDistance().
    DevTools::UpdateViewDistance(window, m_noclipEnabled, deltaTime);

    // C включает/выключает кинематографичный буст разрешения/детализации
    // ASCII в noclip — независимый тумблер, см. DevTools.h.
    DevTools::ToggleCinematicResolution(window);

    DevTools::ApplyHealthDebugKeys(window, m_health, m_maxHealth, m_hKeyWasDown, m_jKeyWasDown);
#endif

    // ---- Дневники: близость (нужна ДО блока E ниже — он читает
    // m_nearbyDiaryIndex, чтобы решить, что делать по нажатию) + журнал
    // (Tab) ----
    {
        const float diaryInteractRadius = 1.2f; // чуть теснее win-кнопки — дневник мельче
        float bestDistSq = diaryInteractRadius * diaryInteractRadius;
        m_nearbyDiaryIndex = -1;
        for (size_t i = 0; i < diaryPositions.size(); ++i)
        {
            const glm::vec2 to(
                diaryPositions[i].x - m_camPos.x,
                diaryPositions[i].z - m_camPos.z
            );
            const float distSq = glm::dot(to, to);
            if (distSq <= bestDistSq)
            {
                bestDistSq = distSq;
                m_nearbyDiaryIndex = (int)i;
            }
        }
    }
    {
        // Tab — edge-triggered, тот же приём, что и V для компаса: не
        // держать, а переключать по нажатию. Работает независимо от
        // близости к дневнику — журнал открывает уже НАЙДЕННЫЕ записи,
        // не требует стоять рядом с конкретным карманом (см. обсуждение:
        // "повторное чтение — TAB").
        const bool tabKeyDown = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
        if (tabKeyDown && !m_tabKeyWasDown)
        {
            m_journalToggleRequested = true;
        }
        m_tabKeyWasDown = tabKeyDown;
    }

    // ---- Кнопка победы (E рядом с пьедесталом в финишной safe-zone) ----
    // Edge-triggered, как V/H/J выше. При нажатии рядом с кнопкой игра
    // считается пройденной и окно закрывается. Тот же E, если не рядом
    // с кнопкой, но рядом с дневником (m_nearbyDiaryIndex — см. блок
    // выше, уже посчитан ЭТИМ кадром) — запрашивает открытие дневника.
    if (!m_gameWon)
    {
        const bool eKeyDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
        if (eKeyDown && !m_eKeyWasDown)
        {
            glm::vec2 toButton(
                winButtonPos.x - m_camPos.x,
                winButtonPos.z - m_camPos.z
            );

            const float winInteractRadius = 1.4f;
            if (glm::dot(toButton, toButton) <= winInteractRadius * winInteractRadius)
            {
                if (diariesReadCount >= kMinDiariesToWin)
                {
                    m_gameWon = true;
                    std::fprintf(stderr, "DungeonScene: win button pressed - closing game.\n");
                    glfwSetWindowShouldClose(window, true);
                }
                else
                {
                    // Недостаточно прочитанных дневников — не запускаем
                    // победу, вместо этого просим DungeonScene показать
                    // короткое сообщение (см. consumeWinBlockedRequest()).
                    m_winBlockedRequested = true;
                }
            }
            else if (m_nearbyDiaryIndex != -1)
            {
                // Не рядом с кнопкой победы, но рядом с дневником — тот же
                // E, другое действие. DungeonScene решает, что с этим
                // делать (открыть экран чтения), см. consumeDiaryOpenRequest().
                m_diaryOpenRequested = true;
            }
        }
        m_eKeyWasDown = eKeyDown;
    }

    // ---- Проигрыш (здоровье дошло до нуля, см. applyDamage()) ----
    // Раньше здесь мгновенно закрывалось окно. Теперь вместо этого —
    // последовательность смерти (см. updateDeathSequence()): пока она
    // играет, движение/осмотр камеры мышью полностью заморожены (return
    // сразу после — весь обычный ввод ниже по функции просто не
    // выполняется), а собственно закрытие (переход в меню через
    // существующий фейд) запускается из Application.cpp по
    // consumeDeathFadeTrigger() — см. PlayerController.h.
    if (m_deathSequenceActive)
    {
        updateDeathSequence(deltaTime);
        return;
    }

    // Non-linear pose animation. Exponential convergence keeps the motion
    // smooth in both directions instead of moving linearly frame-by-frame.
    const float targetPose = m_compassVisible ? 1.0f : 0.0f;
    const float poseResponse = 5.5f;
    const float poseAlpha = 1.0f - std::exp(-poseResponse * deltaTime);
    m_poseBlend += (targetPose - m_poseBlend) * poseAlpha;
    if (std::abs(targetPose - m_poseBlend) < 0.0005f)
        m_poseBlend = targetPose;

    if (m_poseBlend > 0.0001f || m_compassVisible)
        m_poseTime += deltaTime;

    // Тот же приём для факела в руке (ЛКМ) — своя переменная, своя
    // скорость (можно другую "тяжесть" подъёма/опускания, чем у компаса).
    const float targetTorchBlend = m_torchRaised ? 1.0f : 0.0f;
    // Было 5.5 (как у компаса) — "слишком быстро убирается"; ниже —
    // subjectively медленнее, спокойнее опускается/поднимается.
    const float torchResponse = 2.5f;
    const float torchAlpha = 1.0f - std::exp(-torchResponse * deltaTime);
    m_torchBlend += (targetTorchBlend - m_torchBlend) * torchAlpha;
    if (std::abs(targetTorchBlend - m_torchBlend) < 0.0005f)
        m_torchBlend = targetTorchBlend;

    glm::vec3 front = getFront();

    // [comment corrupted in source file - original text lost/unrecoverable]
    glm::vec3 flatFront(
        front.x,
        0.0f,
        front.z
    );

    if (glm::dot(flatFront, flatFront) < 0.000001f)
    {
        flatFront = glm::vec3(
            0.0f,
            0.0f,
            -1.0f
        );
    }
    else
    {
        flatFront = glm::normalize(flatFront);
    }

    glm::vec3 right =
        glm::normalize(
            glm::cross(
                flatFront,
                glm::vec3(
                    0.0f,
                    1.0f,
                    0.0f
                )
            )
        );

    glm::vec3 move(0.0f);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        move += flatFront;

    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        move -= flatFront;

    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        move += right;

    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        move -= right;

    m_isMoving =
        glm::dot(move, move) > 0.000001f;

    // Игрок ХОЧЕТ бежать (зажат Shift и есть движение). Реально бежать
    // разрешено только если стамина не истощена — см. блок обновления
    // стамины ниже, который также может принудительно снять m_isRunning
    // в кадре, где она обнуляется.
    const bool wantsToRun =
        m_isMoving &&
        !m_compassVisible &&
        (
            glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS
        );

    m_isRunning =
        wantsToRun &&
        (m_noclipEnabled || !m_staminaExhausted);

    // БАГФИКС ("получил урон, зажал Shift чтобы убежать — стамина
    // тратится, хотя игрок не бежит") — этот блок раньше стоял ПОСЛЕ
    // блока "Энергия/стамина" ниже. Стамина списывалась по значению
    // m_isRunning, посчитанному ВЫШЕ (просто "зажат Shift + есть
    // движение"), а уже ПОТОМ этот блок принудительно выставлял
    // m_isRunning=false на время дебаффа — на СКОРОСТЬ движения это
    // влияло верно (см. speed ниже, использует уже актуальный
    // m_isRunning), а вот стамина к тому моменту уже была списана по
    // старому, ещё не поправленному значению — на один кадр раньше,
    // чем нужно, но происходило это КАЖДЫЙ кадр дебаффа, так что
    // фактически стамина тратилась всё время удержания Shift во время
    // дебаффа, будто игрок реально бежит. Перенесли сюда, ДО блока
    // стамины — теперь m_isRunning уже false к тому моменту, когда
    // стамина проверяет его.
    if (!m_noclipEnabled && m_caughtTimer > 0.0f)
    {
        m_caughtTimer -= deltaTime;
        if (m_caughtTimer < 0.0f)
            m_caughtTimer = 0.0f;

        // Бег запрещён всё время действия дебаффа, вне зависимости от
        // стамины/Shift — то же принудительное отключение, что уже
        // делает m_staminaExhausted ниже.
        m_isRunning = false;
    }

    // ---------------- Энергия/стамина ----------------
    // При 0% здоровья стамина тратится сама по себе, с той же
    // скоростью, что и при беге — заглушка на будущее ("игрок лежит
    // без сил"), пока нет отдельной анимации. Если игрок при этом ещё
    // и бежит, скорость расхода не удваивается — обе причины делят
    // один и тот же расход.
    // В noclip-режиме стамина заморожена — это debug-инструмент для
    // облёта карты, а не часть обычного геймплея.
    if (!m_noclipEnabled)
    {
        const bool zeroHealthDrain = (m_health <= 0.0f);

        if (m_isRunning || zeroHealthDrain)
        {
            m_stamina -= m_staminaDrainPerSec * deltaTime;
            if (m_stamina <= 0.0f)
            {
                m_stamina = 0.0f;
                m_staminaExhausted = true;
                // Бег обрывается немедленно, как только стамина кончилась —
                // остаток этого кадра игрок уже идёт, а не бежит.
                m_isRunning = false;
            }
        }
        else
        {
            m_stamina += m_staminaRegenPerSec * deltaTime;
            if (m_stamina > m_maxStamina)
                m_stamina = m_maxStamina;

            // Пока не накопится хотя бы m_staminaResumeThreshold от максимума,
            // бег остаётся заблокирован, даже если игрок продолжает жать Shift.
            if (m_staminaExhausted && m_stamina >= m_maxStamina * m_staminaResumeThreshold)
                m_staminaExhausted = false;
        }
    }

    const float walkSpeed = 1.5f;
    const float runSpeed  = 3.0f;

    // В noclip-режиме летаем ощутимо быстрее, чтобы можно было
    // оперативно облететь всю карту при тестировании.
    const float noclipWalkSpeed = 4.5f;
    const float noclipRunSpeed  = 10.0f;

    float speed =
        m_noclipEnabled
        ? (m_isRunning ? noclipRunSpeed : noclipWalkSpeed)
        : (m_isRunning ? runSpeed : walkSpeed);

    if (!m_noclipEnabled && m_caughtTimer > 0.0f)
    {
        // Фаза 2 (последние kCaughtPhase2Duration секунд обратного
        // отсчёта) — замедленное бегство, но НЕЗАВИСИМОЕ от скорости
        // самого врага (kCaughtDebuffSpeed=1.2 против walkSpeed=1.5,
        // см. большой комментарий у kCaughtDebuffSpeed в .h про то,
        // почему раньше эта связь была багом). Фаза 1 (пока играет
        // Attack_Lunge) — обычная ходьба игрока, её трогать не нужно,
        // m_isRunning=false выше уже само по себе даёт walkSpeed через
        // обычную ветку.
        if (m_caughtTimer <= kCaughtPhase2Duration)
            speed = kCaughtDebuffSpeed;
    }

    // [comment corrupted in source file - original text lost/unrecoverable]

    const glm::vec3 footstepStartPos = m_camPos;

    if (m_isMoving)
    {
        move =
            glm::normalize(move)
            * speed
            * deltaTime;

        if (m_noclipEnabled)
        {
            // Полёт без коллизий: просто сдвигаем позицию, минуя
            // tryMove()/isBlocked() — сквозь стены и т.д.
            m_camPos.x += move.x;
            m_camPos.z += move.z;
        }
        else
        {
            glm::vec3 pos =
                m_camPos;

            resolveMovement(
                pos,
                glm::vec3(move.x, 0.0f, move.z),
                isFloor,
                getCornerCut,
                getChamferSize,
                columnCentersXZ,
                winButtonPos,
                enemyPositions
            );

            m_camPos = pos;
        }
    }

    // ---------------- Footsteps ----------------
    // Count actual horizontal displacement after collision resolution.
    // This keeps the sound cadence tied to the player's real motion.
    if (!m_noclipEnabled && m_isMoving)
    {
        const glm::vec2 deltaXZ(
            m_camPos.x - footstepStartPos.x,
            m_camPos.z - footstepStartPos.z
        );
        const float movedDistance = glm::length(deltaXZ);

        if (movedDistance > 0.00001f)
        {
            const bool runNow = m_isRunning;
            const bool modeChanged =
                !m_footstepWasMoving ||
                (runNow != m_footstepWasRunning);

            // A new movement burst starts with an immediate footstep.
            // When switching walk <-> run, restart the rhythm cleanly.
            if (modeChanged)
            {
                m_footstepDistance = 0.0f;
                if (runNow)
                    m_footstepAudio.playRun();
                else
                    m_footstepAudio.playWalk();
            }

            m_footstepWasMoving = true;
            m_footstepWasRunning = runNow;
            m_footstepDistance += movedDistance;

            const float stepLength = runNow ? 0.95f : 0.68f;
            while (m_footstepDistance >= stepLength)
            {
                m_footstepDistance -= stepLength;
                if (runNow)
                    m_footstepAudio.playRun();
                else
                    m_footstepAudio.playWalk();
            }
        }
    }
    else
    {
        m_footstepWasMoving = false;
        m_footstepWasRunning = false;
        m_footstepDistance = 0.0f;
    }

    // ---------------- Noclip: вертикальный полёт ----------------
    // Space — вверх, Left Ctrl — вниз. Работает независимо от
    // горизонтального движения (можно просто зависнуть на месте и
    // подниматься/опускаться).
    if (m_noclipEnabled)
    {
        const float flySpeed =
            m_isRunning
            ? noclipRunSpeed
            : noclipWalkSpeed;

        float vertical = 0.0f;

        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            vertical += 1.0f;

        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
            vertical -= 1.0f;

        if (vertical != 0.0f)
            m_camPos.y += vertical * flySpeed * deltaTime;
    }


    // ---------------- Head bob phase ----------------
    //
    // [comment corrupted in source file - original text lost/unrecoverable]
    //

    if (m_isMoving)
    {
        const float bobFrequency = 7.0f;

        // Use the SAME smoothed deltaTime as m_cameraAnimTime (advanced
        // once per call at the top of this function, see
        // advanceSmoothedAnimDt()) — a plain per-call blend factor (the
        // previous approach here) has a real-time smoothing window that
        // itself changes with frame rate, so the old fix still visibly
        // shook differently at 30fps than 60fps. advanceSmoothedAnimDt()
        // uses a proper exponential-decay formula instead, so the
        // smoothing behaves the same regardless of fps. Actual player
        // movement is NOT touched — it still uses the raw deltaTime, so
        // movement speed stays correct and frame-rate independent; only
        // this cosmetic sway is smoothed.
        m_bobPhase +=
            m_animSmoothedDt * bobFrequency;

        // [comment corrupted in source file - original text lost/unrecoverable]
        if (m_bobPhase >
            glm::two_pi<float>() * 100.0f)
        {
            m_bobPhase =
                std::fmod(
                    m_bobPhase,
                    glm::two_pi<float>()
                );
        }
    }

    // [comment corrupted in source file - original text lost/unrecoverable]

    const float targetBlend =
        m_isMoving
        ? 1.0f
        : 0.0f;

    const float blendSpeed =
        m_isRunning
        ? 12.0f
        : 10.0f;

    if (m_bobBlend < targetBlend)
    {
        m_bobBlend =
            glm::min(
                m_bobBlend
                    + deltaTime * blendSpeed,
                1.0f
            );
    }
    else
    {
        m_bobBlend =
            glm::max(
                m_bobBlend
                    - deltaTime * blendSpeed,
                0.0f
            );
    }
}


void PlayerController::processMouse(double xpos, double ypos) {
    if (m_firstMouse) {
        m_lastX = xpos; m_lastY = ypos;
        m_firstMouse = false;
    }

    // Чувствительность теперь настраивается игроком (экран SETTINGS, см.
    // DungeonScene.h: getMouseSensitivity()/setMouseSensitivity(), main.cpp
    // и MainMenu::BuildSettingsMenu()) — раньше здесь был захардкожен
    // константный множитель 0.1f.
    const float sensitivity = m_mouseSensitivity;
    const float dx = (float)(xpos - m_lastX) * sensitivity;
    const float dy = (float)(m_lastY - ypos) * sensitivity;
    m_lastX = xpos;
    m_lastY = ypos;

    m_yaw += dx;
    m_pitch += dy;

    // Ограничение угла обзора действует, только пока миникарта реально
    // видна (m_compassVisible). Раньше сюда добавлялось ещё и условие
    // m_poseBlend > 0.001f — из-за плавной анимации опускания руки это
    // держало ограничение ещё пару секунд ПОСЛЕ повторного нажатия V,
    // хотя миникарта уже была скрыта. Теперь ограничение снимается
    // сразу в момент отжатия V.
    if (m_compassVisible)
    {
        m_pitch =
            glm::clamp(
                m_pitch,
                -8.0f,
                20.0f
            );
    }
    else
    {
        m_pitch =
            glm::clamp(
                m_pitch,
                -89.0f,
                89.0f
            );
    }
}

void PlayerController::tickMenuCameraSpin(float deltaTime)
{
    deltaTime = glm::clamp(deltaTime, 0.0f, 0.05f);

    m_menuCameraActive = true;
    advanceSmoothedAnimDt(deltaTime);
    m_cameraAnimTime += m_animSmoothedDt;

    // Камера в фоне меню больше НЕ вращается — просто стоит на месте и
    // смотрит в фиксированном направлении (yaw не трогаем: остаётся
    // тем же значением, что и обычный спавн игрока, m_yaw = -90 — той
    // же стеной, которую видит настоящий игрок в первый момент игры,
    // с факелами startовой safe-zone на ней). Небольшой наклон вниз —
    // чтобы факелы на стенах (они выше уровня глаз) попадали в кадр
    // вместе с полом, а не смотреть строго вперёд в потолок.
    const float kMenuPitchDeg = -6.0f;
    m_pitch = kMenuPitchDeg;
}

void PlayerController::tickPauseCameraIdle(float deltaTime)
{
    deltaTime = glm::clamp(deltaTime, 0.0f, 0.05f);

    // Пауза замораживает игровой ввод и положение игрока, но сама камера
    // продолжает очень мягко двигаться так же, как при полном бездействии
    // в игре. m_menuCameraActive остаётся false, поэтому используются
    // именно обычные игровые idle-амплитуды, а не более заметные меню.
    m_menuCameraActive = false;
    advanceSmoothedAnimDt(deltaTime);
    m_cameraAnimTime += m_animSmoothedDt;
}

glm::vec3 PlayerController::getCameraRenderPosition() const
{
    glm::vec3 front =
        getFront();

    glm::vec3 flatFront(
        front.x,
        0.0f,
        front.z
    );

    if (glm::dot(flatFront, flatFront) < 0.000001f)
    {
        flatFront =
            glm::vec3(
                0.0f,
                0.0f,
                -1.0f
            );
    }
    else
    {
        flatFront =
            glm::normalize(flatFront);
    }

    glm::vec3 right =
        glm::normalize(
            glm::cross(
                flatFront,
                glm::vec3(
                    0.0f,
                    1.0f,
                    0.0f
                )
            )
        );

    const float t =
        m_cameraAnimTime;

    const float phase =
        m_bobPhase;

    const float moving =
        m_bobBlend;

    const float running =
        m_isRunning
        ? 1.0f
        : 0.0f;

    // --------------------------------------------------
    // Idle camera motion
    // --------------------------------------------------
    //
    // [comment corrupted in source file - original text lost/unrecoverable]
    //

    const float idleVertical =
        std::sin(t * 3.0f)
        * (m_menuCameraActive ? 0.011f : 0.007f)
        +
        std::sin(t * 4.0f)
        * (m_menuCameraActive ? 0.0045f : 0.003f);

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    const float walkVerticalAmp =
        0.014f;

    const float runVerticalAmp =
        0.030f;

    const float verticalAmp =
        glm::mix(
            walkVerticalAmp,
            runVerticalAmp,
            running
        );

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    const float walkSideAmp =
        0.007f;

    const float runSideAmp =
        0.030f;

    const float sideAmp =
        glm::mix(
            walkSideAmp,
            runSideAmp,
            running
        );

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    const float walkForwardAmp =
        0.003f;

    const float runForwardAmp =
        0.010f;

    const float forwardAmp =
        glm::mix(
            walkForwardAmp,
            runForwardAmp,
            running
        );

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    const float verticalBob =
        idleVertical
        +
        moving
        * std::sin(
            phase * 2.0f
        )
        * verticalAmp;

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    const float sideSway =
        std::sin(
            t * 0.90f
        )
        * 0.004f
        +
        moving
        * std::sin(phase)
        * sideAmp;

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    const float forwardBob =
        moving
        * std::sin(
            phase * 2.0f
            + 0.5f
        )
        * forwardAmp;

    return
        m_camPos
        +
        right * sideSway
        +
        flatFront * forwardBob
        +
        glm::vec3(
            0.0f,
            verticalBob + deathCameraYOffset(),
            0.0f
        );
}

glm::vec3 PlayerController::getCameraRenderUp() const
{
    glm::vec3 front =
        getFront();

    const float t =
        m_cameraAnimTime;

    const float phase =
        m_bobPhase;

    const float moving =
        m_bobBlend;

    const float running =
        m_isRunning
        ? 1.0f
        : 0.0f;

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    const float idleRoll =
        std::sin(t * 0.82f)
        * 0.28f
        +
        std::sin(t * 1.37f)
        * 0.10f;

    // --------------------------------------------------
    // [comment corrupted in source file - original text lost/unrecoverable]
    // --------------------------------------------------

    const float walkRollAmp =
        0.65f;

    const float runRollAmp =
        2.20f;

    const float activeRoll =
        glm::mix(
            walkRollAmp,
            runRollAmp,
            running
        );

    const float rollDegrees =
        idleRoll
        +
        moving
        * std::sin(phase)
        * activeRoll
        +
        deathCameraRollDegrees();

    // [comment corrupted in source file - original text lost/unrecoverable]
    // [comment corrupted in source file - original text lost/unrecoverable]
    glm::mat4 rollMatrix =
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(rollDegrees),
            front
        );

    return
        glm::normalize(
            glm::vec3(
                rollMatrix
                *
                glm::vec4(
                    0.0f,
                    1.0f,
                    0.0f,
                    0.0f
                )
            )
        );
}


