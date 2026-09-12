#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <functional>
#include <vector>
#include <cstdio>
#include "audio/FootstepAudio.h"
#include "WallShapes.h"
#include "Columns.h"

// ============================================================================
// PlayerController — камера, движение/коллизии, здоровье/стамина, шаги,
// и связанные с вводом переключатели (компас V, дебаг-карта M, noclip N,
// health-дебаг H/J), которые processInput() переключает по нажатию клавиш.
//
// Вынесено из DungeonScene при разбиении монолита на модули — это
// единственная из 9 подсистем, где полноценное владение состоянием (а не
// "сервис", принимающий данные параметрами, как MapGenerator/MinimapFog)
// было осознанным выбором, несмотря на большое количество полей: это
// действительно одна связная зона ответственности ("игрок"), просто с
// широкой поверхностью — как и было изначально в processInput().
//
// НЕ владеет: картой (m_map/isWall/isFloor остаются в DungeonScene —
// используются геометрией/светом/etc, не только игроком), позицией кнопки
// победы (m_winButtonPos — из MapGenerator), m_currentRenderDistance
// (пересчитывается в render(), не в processInput()).
// ============================================================================
class PlayerController {
public:
    // Радиус коллизии игрока — ПУБЛИЧНАЯ константа (раньше было приватным
    // полем m_collisionRadius), по той же причине, что и
    // EnemyAI::kCollisionRadius публичный: EnemyAI тоже знает этот
    // радиус, чтобы враг физически не проходил сквозь игрока (см.
    // большой комментарий у EnemyAI::resolveWallCollision() — там же
    // обратный случай, игрок сквозь врага, закрыт этой же парой
    // констант).
    //
    // БАГФИКС ("игрок не может протиснуться и выбраться, если зажат с
    // врагом в тупике") — было 0.22. При радиусе врага 0.35 сумма давала
    // 0.57 — больше половины ширины коридора (0.5), то есть протиснуться
    // мимо врага было математически невозможно ни при каком стечении
    // обстоятельств (см. обсуждение в истории правок). У игрока нет
    // видимой 3D-модели (камера от первого лица) — в отличие от радиуса
    // врага, который приближает реальный силуэт модели, этот можно
    // уменьшать почти без визуального риска "протыкания" геометрии.
    // Основная часть уменьшения сознательно сброшена именно сюда, а не
    // на радиус врага (см. его комментарий в EnemyAI.h) — 0.30+0.10=0.40,
    // рабочий зазор ~0.10 от половины коридора: реально можно
    // протиснуться, прижавшись к дальней стене, но не "бесплатно".
    static constexpr float kCollisionRadius = 0.10f; // было 0.22

    void init() { m_footstepAudio.init(); }
    void shutdown() { m_footstepAudio.shutdown(); }

    // isFloor — предикат чтения карты (см. DungeonScene::isFloor), winButtonPos —
    // позиция пьедестала кнопки победы (см. MapGenerator::GenerateResult).
    // window используется и для чтения клавиш, и для glfwSetWindowShouldClose()
    // при нажатии кнопки победы.
    //
    // getCornerCut — WallShapes::CornerCut для клетки стены (см. WallShapes.h,
    // DungeonScene::wallCornerCut()). Для обычной прямоугольной клетки
    // (или клетки без данных, т.е. значение по умолчанию) возвращает
    // CornerCut::None, что даёт ровно прежнее поведение "прямой угол —
    // сплошная стена". Используется только когда isFloor(cx,cz)==false —
    // т.е. только чтобы решить, действительно ли твёрдо ИМЕННО там, где
    // клетка помечена как срезанная (см. isBlocked() в .cpp).
    //
    // getChamferSize — размер этого среза (см. WallShapes::kChamferSize/
    // kChamferSizeChain, DungeonScene::wallChamferSize()) — клетки
    // диагональных "лестниц" (DiagonalCorridors.h) срезаны намного
    // сильнее обычного, и коллизия ОБЯЗАНА читать тот же размер, что и
    // рендер (см. историю правок про 6%-й "срез с царапиной"), иначе
    // силуэт и коллизия разойдутся ровно как их разводило раньше без
    // этого параметра.
    //
    // columnCentersXZ — Шаг 2 (см. Columns.h): свободностоящие колонны,
    // коллизия с ними — простой circle-vs-point тест ПОВЕРХ обычной
    // сеточной проверки (тот же паттерн, что уже был у круговой коллизии
    // пьедестала кнопки победы, см. isBlocked() в .cpp) — не связана с
    // isFloor/getCornerCut вообще, т.к. клетка колонны и так уже пол.
    //
    // enemyPos — БАГФИКС ("игрок может проходить сквозь противника") —
    // текущая позиция врага (см. EnemyAI::position()), обрабатывается
    // тем же способом, что колонны/кнопка победы — ещё одно круглое
    // препятствие, радиус EnemyAI::kCollisionRadius (см. её комментарий).
    // Передаётся значением на этот кадр — PlayerController не хранит
    // ссылку на EnemyAI и не знает о ней ничего, кроме точки в
    // пространстве. Коллизия с ним всегда жёсткая (было экспериментальное
    // смягчение в зависимости от состояния ИИ — откатили, см. историю
    // правок: провоцировало софтлок игрока вплотную к врагу).
    // Минимум дневников, которые нужно прочитать, прежде чем кнопка
    // победы вообще сработает (см. запрос: "минимум 4 дневника чтобы
    // выбраться") — без этого E рядом с пьедесталом просто не срабатывает,
    // запрашивается "заблокировано" сообщение (см. consumeWinBlockedRequest()).
    static constexpr int kMinDiariesToWin = 4;

    void processInput(GLFWwindow* window, float deltaTime,
                       const std::function<bool(int, int)>& isFloor,
                       const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                       const std::function<float(int, int)>& getChamferSize,
                       const std::vector<glm::vec2>& columnCentersXZ,
                       const glm::vec3& winButtonPos,
                       const std::vector<glm::vec3>& enemyPositions,
                       const std::vector<glm::vec3>& diaryPositions,
                       int diariesReadCount);

    // Индекс дневника (в том же порядке, что и diaryPositions выше), в
    // радиусе которого сейчас стоит игрок, -1 если ни один не в радиусе —
    // считается заново каждый вызов processInput(). DungeonScene читает
    // это, чтобы решить, показывать ли подсказку "[E] READ DIARY".
    int nearbyDiaryIndex() const { return m_nearbyDiaryIndex; }

    // Edge-triggered "хочу открыть дневник" — true ровно один кадр сразу
    // после нажатия E, когда nearbyDiaryIndex() != -1. Consume-семантика
    // (как и у остальных запросов ниже): читающий код сам должен сбросить
    // флаг, чтобы не обработать одно нажатие дважды в двух местах.
    bool consumeDiaryOpenRequest() {
        bool r = m_diaryOpenRequested;
        m_diaryOpenRequested = false;
        return r;
    }

    // Edge-triggered Tab — "хочу открыть/закрыть журнал прочитанных
    // дневников" (см. DungeonScene::m_journalOpen). Тот же consume-приём.
    bool consumeJournalToggleRequest() {
        bool r = m_journalToggleRequested;
        m_journalToggleRequested = false;
        return r;
    }

    // Edge-triggered "нажал E у кнопки победы, но дневников прочитано
    // меньше kMinDiariesToWin" — DungeonScene показывает по этому
    // короткое сообщение на экране (см. m_winBlockedMessageTimer).
    bool consumeWinBlockedRequest() {
        bool r = m_winBlockedRequested;
        m_winBlockedRequested = false;
        return r;
    }

    void processMouse(double xpos, double ypos);

    // Сбрасывает только базовую точку mouse-look. Yaw/pitch и позиция камеры
    // не изменяются. Нужен при повторном захвате GLFW_CURSOR_DISABLED
    // после паузы, чтобы первое событие курсора не считалось огромным dx/dy.
    void resetMouseLook() { m_firstMouse = true; }

    void tickMenuCameraSpin(float deltaTime);
    void tickPauseCameraIdle(float deltaTime);

    glm::vec3 getFront() const;
    glm::vec3 getCameraRenderPosition() const;
    glm::vec3 getCameraRenderUp() const;

    // Начальная точка спавна (центр стартовой safe-zone) — вызывается из
    // DungeonScene::init() один раз после generateMap().
    void setSpawnPosition(const glm::vec3& pos) { m_camPos = pos; }

    glm::vec3 camPos() const { return m_camPos; }
    float yaw() const { return m_yaw; }
    float pitch() const { return m_pitch; }
    float poseBlend() const { return m_poseBlend; }
    bool compassVisible() const { return m_compassVisible; }

    // 0 = факел опущен/убран, 1 = поднят и виден — плавно между ними при
    // нажатии ЛКМ (см. update()). Используется и для положения viewmodel-
    // модели, и для того, насколько ярко (если вообще) светит playerLight
    // (см. DungeonScene::render()).
    float torchBlend() const { return m_torchBlend; }
    bool torchRaised() const { return m_torchRaised; }

    // Для покачивания viewmodel-факела при ходьбе/беге (см.
    // DungeonScene::render()) — та же информация о движении, что уже
    // управляет покачиванием головы самой камеры (см. m_bobPhase/
    // m_bobBlend выше в этом файле), просто без готового геттера раньше.
    bool isMoving() const { return m_isMoving; }
    bool isRunning() const { return m_isRunning; }
    bool debugMapVisible() const { return m_debugMapVisible; }

    // Вызывается из DungeonScene::render() ровно в тот кадр, когда враг
    // поймал игрока (см. EnemyAI::consumeJustCaughtPlayer()). Две фазы,
    // обе запрещают бег: сначала на время реальной длины Attack_Lunge
    // (1.25с — "движется секунду просто ходьбой"), потом ещё 2 секунды
    // на скорости ХОДЬБЫ ВРАГА (не своей — "идёт со скоростью ходьбы
    // противника"), и только потом бег снова доступен. См. update():
    // m_caughtTimer убывает и форсирует m_isRunning=false/урезанную
    // скорость, пока не дойдёт до нуля.
    void applyCaughtDebuff()
    {
        m_caughtTimer = kCaughtPhase1Duration + kCaughtPhase2Duration;
    }
    bool noclipEnabled() const { return m_noclipEnabled; }
    // Debug-тумбл ("в dev-tools добавить, чтобы игрок становился
    // невидимым для врага", клавиша I — см. DevTools::
    // ToggleInvisibleToEnemy()). Геттер для EnemyAI::update() (см.
    // DungeonScene::render(), где он передаётся туда каждый кадр).
    bool invisibleToEnemy() const { return m_invisibleToEnemy; }
    bool hasWon() const { return m_gameWon; }
    // Мираж hasWon()/m_gameWon — тот же принцип для проигрыша (здоровье
    // дошло до нуля). См. applyDamage(): при обнулении здоровья
    // выставляет этот флаг, а processInput() (там уже есть доступ к
    // window — как и у кнопки победы) закрывает окно.
    bool hasLost() const { return m_gameOver; }

    // Вызывается из DungeonScene::render() при поимке врагом (вместе с
    // applyCaughtDebuff() — см. там же). Одно попадание Attack_Lunge —
    // фиксированный урон; при обнулении здоровья выставляет hasLost().
    void applyDamage(float amount)
    {
        if (m_gameOver || m_noclipEnabled) // noclip — debug-инструмент, не должен убивать игрока
            return;

        m_health -= amount;
        if (m_health <= 0.0f)
        {
            m_health = 0.0f;
            m_gameOver = true;
            // Раньше здесь мгновенно закрывалось окно (см. историю) —
            // теперь вместо этого запускается последовательность смерти
            // (см. updateDeathSequence() в processInput() и большой
            // комментарий у m_deathTime ниже): камера падает, экран
            // темнеет, и уже ПОСЛЕ этого — переход в меню (через ту же
            // систему фейда, что и обычный выход в меню, см.
            // consumeDeathFadeTrigger()/Application.cpp).
            m_deathSequenceActive = true;
            m_deathTime = 0.0f;
            // БАГФИКС ("факел остаётся в руке при смерти") — выставить
            // m_torchRaised=false тут одно недостаточно: обычный блендинг
            // m_torchBlend живёт в processInput() ПОСЛЕ раннего return для
            // m_deathSequenceActive (см. там), то есть во время смерти
            // никогда не выполняется — факел просто "замирал" в той позе,
            // в которой был на момент смерти. Реальное опускание теперь
            // тоже сделано ВНУТРИ updateDeathSequence() (см. .cpp) —
            // m_torchRaised здесь только выражает намерение ("должен быть
            // опущен"), сам блендинг довершает он же.
            m_torchRaised = false;
            std::fprintf(stderr, "PlayerController: health reached 0 - starting death sequence.\n");
        }
    }

    // true всё то время, пока играет последовательность смерти (камера
    // падает/крутится) — вызывающий код (DungeonScene::render()) должен
    // не давать игроку двигаться/осматриваться, пока это true (хотя
    // processInput() и сам это гарантирует изнутри, см. там).
    bool isDying() const { return m_deathSequenceActive; }

    // true РОВНО ОДИН раз — в тот кадр, когда последовательность смерти
    // (падение+крен) закончилась и пора начинать фейд в меню (см.
    // Application.cpp: тот же RETURN_TO_MENU/FADE_TO_BLACK, что и у
    // обычного выхода в меню). Single-shot pulse, как и
    // consumeJustCaughtPlayer() у EnemyAI.
    bool consumeDeathFadeTrigger()
    {
        const bool result = m_deathFadeTriggered;
        m_deathFadeTriggered = false;
        return result;
    }

    // Обычный processInput() (и вся логика внутри, включая
    // updateDeathSequence()) вызывается только пока идёт реальный
    // геймплей (см. Application.cpp: gameplayActive) — а фейд в меню
    // после смерти (FADE_TO_BLACK) это состояние уже прерывает. Чтобы
    // стамина всё равно продолжала падать ОДНОВРЕМЕННО с затемнением
    // экрана (а не просто останавливалась на месте) — этот отдельный,
    // ничем не гейтованный тик, который Application.cpp вызывает
    // каждый кадр независимо от текущего состояния приложения, пока
    // isDying(). Безопасно вызывать даже пока обычный processInput ещё
    // тоже идёт — просто досчитывает тот же m_deathTime дальше.
    void tickDeathFade(float deltaTime)
    {
        if (m_deathSequenceActive)
            updateDeathSequence(deltaTime);
    }

    float staminaFraction() const { return m_stamina / m_maxStamina; }
    float healthFraction() const { return m_health / m_maxHealth; }

    static constexpr float kMinMouseSensitivity = 0.02f;
    static constexpr float kMaxMouseSensitivity = 0.40f;
    float mouseSensitivity() const { return m_mouseSensitivity; }
    void setMouseSensitivity(float sensitivity) {
        m_mouseSensitivity = glm::clamp(sensitivity, kMinMouseSensitivity, kMaxMouseSensitivity);
    }

    // ---- Сохранение/загрузка (см. save/SaveSystem.h, DungeonScene::newGame()/
    // loadSlot()) ----
    // Полный сброс к состоянию свежего старта: позиция — переданный спавн
    // (центр стартовой safe-zone НОВОЙ карты), взгляд/здоровье/стамина —
    // как при самом первом запуске игры. Не трогает чувствительность мыши
    // (это настройка сессии, а не игрового прогресса, см. SETTINGS) и
    // debug-флаги дев-режима (у обычного игрока и так всегда false).
    void resetForNewGame(const glm::vec3& spawnPos) {
        m_camPos = spawnPos;
        m_yaw = -90.0f;
        m_pitch = 0.0f;
        m_health = m_maxHealth;
        m_stamina = m_maxStamina;
        m_staminaExhausted = false;
        m_gameWon = false;
        m_gameOver = false;
        m_compassVisible = false;
        m_poseBlend = 0.0f;
        m_poseTime = 0.0f;
        m_torchRaised = false;
        m_torchBlend = 0.0f;
        m_caughtTimer = 0.0f;
        // БАГФИКС ("после смерти новая игра не запускается — камера на
        // полу, снова кидает в меню"): эти три поля появились позже,
        // вместе с полной последовательностью смерти (см. большой
        // комментарий у m_deathTime), и не попали в этот сброс —
        // m_deathSequenceActive оставался true НАВСЕГДА, из-за чего:
        // (1) getCameraRenderPosition()/getCameraRenderUp() продолжали
        // держать камеру "упавшей" даже в новой игре, и (2) processInput()
        // продолжал ранним return'ом замораживать управление и почти
        // сразу же взводил m_deathFadeTriggered заново (m_deathTime был
        // далеко за концом таймлайна), из-за чего игру мгновенно кидало
        // обратно в меню при любой попытке сыграть.
        m_deathSequenceActive = false;
        m_deathTime = 0.0f;
        m_deathFadeTriggered = false;
    }

    // Восстанавливает сохранённое состояние игрока (см. SaveSystem::SaveData) —
    // позицию/поворот камеры и доли здоровья/стамины (0..1: в сейве нет
    // смысла хранить физические единицы, максимумы константны —
    // m_maxHealth/m_maxStamina задаются здесь же).
    void restoreState(const glm::vec3& pos, float yaw, float pitch,
                       float healthFraction, float staminaFraction) {
        m_camPos = pos;
        m_yaw = yaw;
        m_pitch = pitch;
        m_health = glm::clamp(healthFraction, 0.0f, 1.0f) * m_maxHealth;
        m_stamina = glm::clamp(staminaFraction, 0.0f, 1.0f) * m_maxStamina;
        m_staminaExhausted = false;
        m_gameWon = false;
        m_gameOver = false;
        m_compassVisible = false;
        m_poseBlend = 0.0f;
        m_poseTime = 0.0f;
        m_torchRaised = false;
        m_torchBlend = 0.0f;
        m_caughtTimer = 0.0f;
        // Тот же фикс, что и в resetForNewGame() (см. комментарий там) —
        // загрузка сейва после смерти должна страдать той же болезнью
        // без этого.
        m_deathSequenceActive = false;
        m_deathTime = 0.0f;
        m_deathFadeTriggered = false;
    }

private:
    bool isBlocked(float x, float z, const std::function<bool(int, int)>& isFloor,
                   const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                   const std::function<float(int, int)>& getChamferSize) const;
    bool tryMove(glm::vec3& pos, glm::vec3 delta,
                 const std::function<bool(int, int)>& isFloor,
                 const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                 const std::function<float(int, int)>& getChamferSize,
                 const std::vector<glm::vec2>& columnCentersXZ,
                 const glm::vec3& winButtonPos,
                 const std::vector<glm::vec3>& enemyPositions) const;

    // Полное разрешение движения по XZ за кадр — заменяет собой "просто X,
    // потом просто Z" (см. историю правок): та схема отлично скользит
    // вдоль ОСЕВЫХ стен (стена всегда параллельна X или Z, так что чистое
    // движение по свободной оси — уже правильный slide), но у диагонали
    // среза угла или у окружности колонны нормаль поверхности не совпадает
    // ни с одной из осей — раздельная X/Z-проверка режет диагональное
    // движение на "либо целиком по X, либо целиком по Z", что на глаз
    // выглядит как удары о вереницу мелких стен вместо гладкого скольжения
    // вдоль среза/колонны. Здесь: полный шаг -> если заблокирован, находим
    // нормаль конкретно диагонали/окружности и скользим вдоль неё -> если
    // и это не прошло, старый резервный вариант (X, потом Z).
    //
    // enemyPos — БАГФИКС ("игрок может проходить сквозь противника") —
    // тело врага теперь ещё одно круглое препятствие в этой же цепочке
    // (тот же приём, что уже применён к колоннам/пьедесталу кнопки
    // победы), с радиусом EnemyAI::kCollisionRadius — ОДНА константа на
    // оба места (см. её большой комментарий в EnemyAI.h), чтобы радиус,
    // которым враг физически блокирует игрока, не мог разъехаться с
    // радиусом его же коллайдера против стен. Передаётся по значению как
    // обычная точка, а не константный указатель на EnemyAI — вызывающему
    // коду (DungeonScene) не нужно давать PlayerController знание о самом
    // классе EnemyAI, только о его текущей позиции на этот кадр.
    //
    // ОТКАТ ("протискивание мимо не преследующего врага" — мягкая/зависящая
    // от состояния коллизия): убрано — при переходе врага из Idle/Search
    // (мягкая коллизия) в Walk/Run (жёсткая) ровно в момент, когда игрок
    // уже стоял вплотную к нему, у игрока не оставалось ни одной легальной
    // позиции для движения (любая соседняя точка всё ещё пересекала круг
    // врага) — игрок намертво замирал на месте. Коллизия снова простая и
    // безусловная, как у стен/колонн — всегда жёсткая, без стейт-машины.
    void resolveMovement(glm::vec3& pos, glm::vec3 delta,
                         const std::function<bool(int, int)>& isFloor,
                         const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                         const std::function<float(int, int)>& getChamferSize,
                         const std::vector<glm::vec2>& columnCentersXZ,
                         const glm::vec3& winButtonPos,
                         const std::vector<glm::vec3>& enemyPositions) const;

    // Если позиция (x,z) (радиус — kCollisionRadius) заблокирована ИМЕННО
    // диагональю среза угла или окружностью (колонна/кнопка победы/враг —
    // см. enemyPos выше), а не осевой стеной — возвращает true и нормаль
    // этой поверхности в outNormal. Для обычной осевой стены возвращает
    // false (для неё раздельный X/Z и так даёт точный slide, см.
    // resolveMovement).
    bool findSlideNormal(float x, float z,
                          const std::function<bool(int, int)>& isFloor,
                          const std::function<WallShapes::CornerCut(int, int)>& getCornerCut,
                          const std::function<float(int, int)>& getChamferSize,
                          const std::vector<glm::vec2>& columnCentersXZ,
                          const glm::vec3& winButtonPos,
                          const std::vector<glm::vec3>& enemyPositions,
                          glm::vec3& outNormal) const;

private:
    // ---- Камера ----
    glm::vec3 m_camPos{ 6.5f, 0.5f, 6.5f };
    float m_yaw = -90.0f;
    float m_pitch = 0.0f;
    bool m_firstMouse = true;
    double m_lastX = 0.0, m_lastY = 0.0;
    float m_mouseSensitivity = 0.1f;

    // ---- Анимация камеры (idle/bob/sway) ----
    float m_cameraAnimTime = 0.0f;
    float m_bobPhase = 0.0f;
    bool m_menuCameraActive = false;
    float m_bobBlend = 0.0f;

    // Smoothed deltaTime used to advance ALL cosmetic camera timers
    // (m_cameraAnimTime idle sway AND m_bobPhase run/walk bob) — never
    // used for actual movement/physics, which still needs the raw,
    // accurate deltaTime to keep speed frame-rate independent.
    // Initialized to a plausible 60fps frame time so the very first
    // frame (before any smoothing history exists) doesn't start from 0.
    //
    // IMPORTANT: updated via advanceSmoothedAnimDt() using a proper
    // exponential-decay formula (alpha depends on dt itself, see .cpp),
    // NOT a fixed per-call blend factor. A fixed blend factor (e.g.
    // "mix(current, target, 0.15)" every call) has a smoothing time
    // window that itself changes with frame rate — at 30fps each call
    // covers twice the real time of a 60fps call, so the same fixed
    // factor ends up averaging over a different real-world time span
    // depending on fps. That was exactly why the camera still visibly
    // shook differently at 30fps than 60fps after the first fix.
    float m_animSmoothedDt = 1.0f / 60.0f;
    void advanceSmoothedAnimDt(float rawDeltaTime);

    // Продвигает m_deathTime и считает по нему текущий сдвиг камеры по
    // Y / крен / дренаж стамины (см. большой комментарий у m_deathTime
    // выше). Вызывается из processInput() ВМЕСТО обычной обработки
    // ввода, пока m_deathSequenceActive.
    void updateDeathSequence(float deltaTime);

    // Текущий вклад последовательности смерти в позицию/крен камеры —
    // 0, если сейчас не играет (см. getCameraRenderPosition()/
    // getCameraRenderUp() — прибавляются к обычному покачиванию, хотя
    // оно фактически не играет роли, раз во время смерти движение/ввод
    // заморожены).
    float deathCameraYOffset() const;
    float deathCameraRollDegrees() const;

    // ---- Компас (клавиша V) ----
    float m_poseTime = 0.0f;
    float m_poseBlend = 0.0f;
    bool m_compassVisible = false;
    bool m_vKeyWasDown = false;

    // ---- Факел в руке игрока (ЛКМ) — тот же приём, что и компас/V ----
    float m_torchBlend = 0.0f;
    bool m_torchRaised = false;
    bool m_lmbWasDown = false;

    bool m_isMoving = false;
    bool m_isRunning = false;

    // ---- Дебафф скорости после поимки врагом (см. applyCaughtDebuff()) ----
    // Обратный отсчёт от (kCaughtPhase1Duration+kCaughtPhase2Duration) до
    // нуля; см. update() за тем, как именно он ограничивает скорость по
    // фазам. Числа — реальная длина Attack_Lunge (1.25с, проверено
    // загрузчиком на настоящем файле) + 2.0с по плану.
    static constexpr float kCaughtPhase1Duration = 1.25f;
    static constexpr float kCaughtPhase2Duration = 2.0f;
    // БАГФИКС ("после укуса игрок не успевает убежать, враг ловит
    // почти сразу") — эта константа раньше БУКВАЛЬНО совпадала с
    // kEnemyWalkSpeed в EnemyAI.cpp ("игрок временно ходит со скоростью
    // врага"). Когда kEnemyWalkSpeed позже понижали отдельным фиксом
    // ("враг скользит") — 1.2 -> 0.8 -> 0.55, эта синхронная константа
    // тихо просела вместе с ней, хотя это два РАЗНЫХ по смыслу числа:
    // kEnemyWalkSpeed — про анимацию ХОДЬБЫ врага, а эта — про то, СМОЖЕТ
    // ли игрок вообще убежать. К тому же враг после укуса переходит в
    // Run (2.0), а не Walk — так что дебафф игрока и не был привязан к
    // тому, что враг реально делает в этот момент. Разрыв
    // 2.0-0.55=1.45 ед/сек на протяжении всех kCaughtPhase2Duration
    // секунд — враг гарантированно догонял снова. Теперь — отдельная,
    // независимая константа, подобранная по факту (даёт реальный, хоть
    // и не гарантированный шанс оторваться, а не гонку, которую нельзя
    // выиграть).
    static constexpr float kCaughtDebuffSpeed = 1.2f;
    float m_caughtTimer = 0.0f;

    // ---- Шаги ----
    FootstepAudio m_footstepAudio;
    float m_footstepDistance = 0.0f;
    bool m_footstepWasMoving = false;
    bool m_footstepWasRunning = false;

    // ---- Debug: noclip (клавиша N) ----
    bool m_noclipEnabled = false;

    // ---- Debug: полная карта (клавиша M) ----
    bool m_debugMapVisible = false;
    bool m_mKeyWasDown = false;
    bool m_nKeyWasDown = false;

    // ---- Debug: невидимость для врага (клавиша I) ----
    bool m_invisibleToEnemy = false;
    bool m_iKeyWasDown = false;

    // ---- Кнопка победы (клавиша E) ----
    bool m_eKeyWasDown = false;
    bool m_gameWon = false;

    // ---- Дневники/журнал (см. Diaries.h, DungeonScene::m_diaries) ----
    bool m_tabKeyWasDown = false;
    int m_nearbyDiaryIndex = -1;
    bool m_diaryOpenRequested = false;
    bool m_journalToggleRequested = false;
    bool m_winBlockedRequested = false;
    bool m_gameOver = false; // здоровье дошло до нуля — см. applyDamage()/hasLost()

    // ---- Последовательность смерти (см. applyDamage()/isDying()) ----
    // Времена вычисляются из ОДНОГО общего таймера (m_deathTime) — тот
    // же стиль, что и у остального покачивания камеры в этом файле
    // (фаза как функция накопленного времени, а не отдельный конечный
    // автомат с явными переключениями между кадрами).
    //
    // Таймлайн (см. updateDeathSequence() в .cpp за формулами):
    //   [0 .. kDeathBounceDuration)                — быстрый рывок вверх
    //   [.. + kDeathFallDuration)                   — падение с разгоном
    //                                                  (не линейно — кубическая
    //                                                  кривая, "как будто
    //                                                  действительно упал
    //                                                  от бессилия")
    //   [.. + kDeathRollDuration)                   — камера довора-
    //                                                  чивается на 90°
    //   [.. + kDeathStaminaDrainDuration)            — стамина падает в 0,
    //                                                  ЗДЕСЬ ЖЕ триггерится
    //                                                  фейд в меню (см.
    //                                                  consumeDeathFadeTrigger())
    static constexpr float kDeathBounceDuration = 0.25f; // было 0.18 — "слишком быстро", чуть растянули рывок вверх
    static constexpr float kDeathBounceHeight = 0.10f;
    static constexpr float kDeathFallDuration = 1.6f; // было 1.1
    static constexpr float kDeathFallDistance = 0.35f; // БАГФИКС: было 1.35 — при реальной высоте глаз игрока 0.5 над полом (см. PlayerController.cpp: m_camPos.y=0.5f) это уводило камеру на -0.85, глубоко под пол. 0.35 оставляет её на ~0.15 над полом — "лежит", но не проваливается сквозь геометрию.
    static constexpr float kDeathRollDuration = 0.85f; // было 0.55
    static constexpr float kDeathRollDegrees = 90.0f;
    static constexpr float kDeathStaminaDrainDuration = 1.2f; // было 0.85 — вместе с новой отдельной kDeathFadeOutSpeed (0.6, вдвое медленнее обычного kFadeOutSpeed=1.2, см. Application.h) фейд стал вдвое дольше, эта длительность увеличена в ту же сторону, чтобы они снова примерно совпадали по ощущению

    bool m_deathSequenceActive = false;
    float m_deathTime = 0.0f;
    bool m_deathFadeTriggered = false; // взводится один раз, когда пора начинать фейд (см. consumeDeathFadeTrigger())

    // ---- Энергия/стамина ----
    const float m_maxStamina = 100.0f;
    const float m_staminaDrainPerSec = 28.0f; // было 24 (небольшое снижение) — вернули обратно, реальной проблемой был баг с порядком операций выше, а не сама цифра
    const float m_staminaRegenPerSec = 16.0f;
    const float m_staminaResumeThreshold = 0.25f;
    float m_stamina = m_maxStamina;
    bool m_staminaExhausted = false;

    // ---- Здоровье ----
    const float m_maxHealth = 100.0f;
    float m_health = m_maxHealth;

    // ---- Debug: клавиши H/J (урон/лечение) ----
    bool m_hKeyWasDown = false;
    bool m_jKeyWasDown = false;
};
