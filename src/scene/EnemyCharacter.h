#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "render/SkinnedModel.h"

// Body and animation of one creature, without AI
// References the shared SkinnedModel
class EnemyCharacter {
public:
    // Non-owning; nullptr means the character is not drawn
    void attachSharedModel(const SkinnedModel* sharedModel);
    void destroy(); // resets the pointer/state; the shared model's GPU resources stay untouched

    enum class State {
        Idle,
        Walk,
        Run,
        Attack,
        WallSlam,
        Scream
    };

    // Explicit state -> clip name mapping, set once at init
    // update()/draw() do not hardcode strings
    void setClipName(State state, const std::string& clipName);

    void setState(State state); // instant; used with update() below for smooth pose blending
    void update(float deltaTime);

    void setPosition(const glm::vec3& pos) { m_position = pos; }
    void setYawDegrees(float yaw) { m_yawDegrees = yaw; }
    glm::vec3 position() const { return m_position; }

    void draw(GLint uModelLoc, GLint uBoneMatricesLoc) const;

    bool isLoaded() const { return m_loaded; }

    // nullptr-safe passthrough to the model's texture
    GLuint diffuseTexture() const { return m_model ? m_model->diffuseTexture() : 0; }
    bool hasDiffuseTexture() const { return m_model && m_model->hasDiffuseTexture(); }

private:
    // Non-owning; check m_loaded before use
    const SkinnedModel* m_model = nullptr;
    bool m_loaded = false;

    std::string m_clipNames[6]; // index matches the State enum order

    State m_currentState = State::Idle;
    State m_previousState = State::Idle;
    float m_stateTime = 0.0f;      // time within the current state, for blending
    float m_currentClipTime = 0.0f;
    float m_previousClipTime = 0.0f;

    glm::vec3 m_position{ 0.0f };
    float m_yawDegrees = 0.0f;

    // Previous position, for distance-based clip playback
    glm::vec3 m_lastUpdatePosition{ 0.0f };
    bool m_lastUpdatePositionInit = false;

    std::vector<glm::mat4> m_boneMatrices; // reusable buffer: not allocated every frame
};
