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
    // Nothing is freed on the GPU: the model is not ours; DungeonScene owns and frees it via
    // m_enemySharedModel.destroy(), once for everyone.
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

    // For Walk/Run the clip advances by the distance actually travelled (clipDeltaTime = distance /
    // referenceSpeed) instead of time, so a mismatch between movement speed and the baked stride
    // does not read as skating, and a blocked body stops its legs. At the reference speed this
    // equals deltaTime. Other states do not move and use time. kWalk/RunReferenceSpeed must match
    // kEnemyWalk/RunSpeed in EnemyAI.cpp.
    const float kWalkReferenceSpeed = 0.55f; // = kEnemyWalkSpeed in EnemyAI.cpp
    const float kRunReferenceSpeed = 2.0f;  // = kEnemyRunSpeed in EnemyAI.cpp

    float clipDeltaTime = deltaTime; // default: time-based (Idle/Attack/WallSlam/Scream)

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

            // When pinned against a wall the travelled distance is near zero, which would freeze
            // the body like a statue while the AI keeps trying to move; a floor of 12% of the
            // time-based rate keeps the legs shuffling ("stuck but trying").
            const float kMinAnimRateFraction = 0.12f;
            clipDeltaTime = std::max(clipDeltaTime, deltaTime * kMinAnimRateFraction);
        }
        // else: the very first frame: there is no previous position to compare with, so it stays on
        // the time-based step this once.
    }

    m_lastUpdatePosition = m_position;
    m_lastUpdatePositionInit = true;

    m_currentClipTime += clipDeltaTime;
    // The previous pose (during the short crossfade below) is already on its way out, fading by
    // blend; it is not tied to distance, which would add complexity (remembering the previous
    // state's reference speed too) for a barely noticeable 0.25 s tail.
    m_previousClipTime += deltaTime;

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

    // The mesh's real bind-pose height, measured from the .glb (raw vertices, no animation), is
    // 1.75 units, while the player's eye height in this game is only 0.5 (the player is ~0.54
    // tall). kModelScale = 0.6 gives ~1.05: noticeably taller than the player (for threat) but
    // proportionate to the maze's 1-unit-wide corridors.
    const float kModelScale = 0.6f;

    const glm::mat4 model =
        glm::translate(glm::mat4(1.0f), m_position)
        * glm::rotate(glm::mat4(1.0f), glm::radians(m_yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f))
        * glm::scale(glm::mat4(1.0f), glm::vec3(kModelScale));

    glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, glm::value_ptr(model));

    if (!m_boneMatrices.empty())
        glUniformMatrix4fv(uBoneMatricesLoc, (GLsizei)m_boneMatrices.size(), GL_FALSE, glm::value_ptr(m_boneMatrices[0]));

    m_model->draw();
}
