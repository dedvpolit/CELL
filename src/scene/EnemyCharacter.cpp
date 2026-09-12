#include "EnemyCharacter.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

void EnemyCharacter::attachSharedModel(const SkinnedModel* sharedModel)
{
    m_model = sharedModel;
    m_loaded = (sharedModel != nullptr);
}

void EnemyCharacter::destroy()
{
    // Ничего не освобождаем на GPU — модель НЕ наша (см. большой
    // комментарий в .h): владеет и освобождает её DungeonScene через
    // m_enemySharedModel.destroy(), один раз на всех.
    m_model = nullptr;
    m_loaded = false;
}

void EnemyCharacter::setClipName(State state, const std::string& clipName)
{
    m_clipNames[(int)state] = clipName;
}

void EnemyCharacter::setState(State state)
{
    if (state == m_currentState)
        return;

    m_previousState = m_currentState;
    m_previousClipTime = m_currentClipTime;

    m_currentState = state;
    m_currentClipTime = 0.0f;
    m_stateTime = 0.0f;
}

void EnemyCharacter::update(float deltaTime)
{
    if (!m_loaded)
        return;

    m_stateTime += deltaTime;

    // БАГФИКС ("враг скользит, а не идёт") — раньше клип двигался СТРОГО
    // в реальном времени (m_currentClipTime += deltaTime) независимо от
    // того, сколько мир-юнитов персонаж реально прошёл за этот кадр. При
    // любой рассинхронизации между скоростью перемещения (EnemyAI) и
    // длиной шага, "зашитой" в саму анимацию, это читалось как
    // скольжение/катание — а если движение вообще было прервано
    // столкновением (стена/игрок, см. EnemyAI::resolveWallCollision()) и
    // персонаж в итоге прошёл МЕНЬШЕ положенного, ноги всё равно
    // доигрывали полный "временной" шаг вникуда, хотя тело стояло почти
    // на месте.
    //
    // Для локомоционных состояний (Walk/Run) клип теперь двигается
    // ПРОПОРЦИОНАЛЬНО ФАКТИЧЕСКИ ПРОЙДЕННОМУ РАССТОЯНИЮ за кадр, а не
    // времени: clipDeltaTime = distance / referenceSpeed. При движении
    // РОВНО с референсной скоростью (нет столкновений/торможения)
    // формула алгебраически сводится обратно к deltaTime — клип идёт
    // ровно как раньше, 1:1. При частичной/полной блокировке движения
    // (упёрся в стену/игрока) — пропорционально меньше вплоть до полной
    // остановки анимации, что и должно быть у стоящей на месте фигуры.
    // Idle/Attack/WallSlam/Scream не двигают тело вообще — им distance
    // всегда ~0, поэтому они намеренно ОСТАВЛЕНЫ на time-based ниже, а
    // не просто "случайно получили бы то же самое".
    //
    // kWalkReferenceSpeed/kRunReferenceSpeed — ДОЛЖНЫ совпадать с
    // kEnemyWalkSpeed/kEnemyRunSpeed в EnemyAI.cpp (это "на какой
    // скорости клип задуман идти с темпом 1:1"; поменяются там —
    // обновить и здесь).
    const float kWalkReferenceSpeed = 0.55f; // = kEnemyWalkSpeed в EnemyAI.cpp
    const float kRunReferenceSpeed = 2.0f;  // = kEnemyRunSpeed в EnemyAI.cpp

    float clipDeltaTime = deltaTime; // дефолт — старое поведение (Idle/Attack/WallSlam/Scream)

    if (m_currentState == State::Walk || m_currentState == State::Run)
    {
        if (m_lastUpdatePositionInit)
        {
            glm::vec3 moved = m_position - m_lastUpdatePosition;
            moved.y = 0.0f;
            const float distance = glm::length(moved);
            const float referenceSpeed =
                (m_currentState == State::Run) ? kRunReferenceSpeed : kWalkReferenceSpeed;
            clipDeltaTime = distance / referenceSpeed;

            // БАГФИКС ("враг иногда просто застывает на месте на пару
            // секунд") — при полной блокировке движения (тесно прижало
            // к стене/углу, resolveWallCollision() гасит почти весь шаг)
            // clipDeltaTime уходил в ноль или очень близко к нему — тело
            // выглядело буквально ЗАСТЫВШИМ статуем, а не "идёт, но с
            // трудом", хотя AI всё ещё активно пытается двигаться и
            // обычно выправляется за доли секунды — пока сдвиг
            // не начнётся, легко читается как "сломался". Нижний порог
            // — минимум 12% от обычной time-based скорости — ноги
            // продолжают слабо перебирать, читается как "застрял,
            // но старается", а не как заморозка/баг. Верхнего предела
            // это не задаёт (при обычном движении clipDeltaTime всё ещё
            // просто distance/referenceSpeed, без изменений) — только
            // подхватывает случаи, где иначе было бы околонулевое
            // значение.
            const float kMinAnimRateFraction = 0.12f;
            clipDeltaTime = std::max(clipDeltaTime, deltaTime * kMinAnimRateFraction);
        }
        // else: самый первый кадр вообще — нет предыдущей позиции для
        // сравнения, остаёмся на дефолтном time-based шаге на этот раз.
    }

    m_lastUpdatePosition = m_position;
    m_lastUpdatePositionInit = true;

    m_currentClipTime += clipDeltaTime;
    // Предыдущая поза (во время короткого кроссфейда ниже) — она уже
    // "уходящая", просто плавно гаснет по blend; не привязываем её к
    // дистанции — усложнило бы (нужно было бы помнить референсную
    // скорость ПРЕДЫДУЩЕГО состояния тоже) ради 0.25с почти незаметного
    // хвоста.
    m_previousClipTime += deltaTime;

    // Короткий кроссфейд между позами при смене состояния — та же идея,
    // что уже используется для покачивания/подъёма факела
    // (PlayerController::m_torchBlend): плавное схождение, а не
    // мгновенный щелчок между позами.
    const float kBlendDuration = 0.25f;
    const float blend = kBlendDuration > 0.0f
        ? std::min(1.0f, m_stateTime / kBlendDuration)
        : 1.0f;

    const int curClip = m_model->findClipIndex(m_clipNames[(int)m_currentState]);
    const int prevClip = m_model->findClipIndex(m_clipNames[(int)m_previousState]);

    if (blend >= 1.0f || prevClip < 0 || curClip < 0)
        m_model->sampleAnimation(curClip, m_currentClipTime, m_boneMatrices);
    else
        m_model->sampleAnimationBlended(
            prevClip, m_previousClipTime,
            curClip, m_currentClipTime,
            blend, m_boneMatrices);
}

void EnemyCharacter::draw(GLint uModelLoc, GLint uBoneMatricesLoc) const
{
    if (!m_loaded)
        return;

    // БАГФИКС ("враг слишком большой"): измерил реальный рост меша в
    // бинд-позе напрямую по .glb (без анимации, чистые вершины) —
    // 1.75 юнита. Высота глаз игрока в этой игре — всего 0.5 (то есть
    // сам игрок ростом ~0.54) — модель была примерно в 3 РАЗА выше
    // игрока. 0.4 приводит её к ~0.70 юнита — заметно выше игрока (для
    // угрозы), но соразмерно с 1-юнитовыми коридорами лабиринта.
    const float kModelScale = 0.6f; // было 0.4 — увеличено в 1.5 раза по запросу

    const glm::mat4 model =
        glm::translate(glm::mat4(1.0f), m_position)
        * glm::rotate(glm::mat4(1.0f), glm::radians(m_yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f))
        * glm::scale(glm::mat4(1.0f), glm::vec3(kModelScale));

    glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, glm::value_ptr(model));

    if (!m_boneMatrices.empty())
        glUniformMatrix4fv(uBoneMatricesLoc, (GLsizei)m_boneMatrices.size(), GL_FALSE, glm::value_ptr(m_boneMatrices[0]));

    m_model->draw();
}
