#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "render/SkinnedModel.h"

// Body + animation of one creature: a reference to the shared SkinnedModel
// (DungeonScene::m_enemySharedModel), its own position/rotation and animation state, no AI. All
// creatures look the same, so mesh, clips and GPU buffers are loaded once; the only per-instance
// state is m_boneMatrices. Clip names follow THE WRAPPED (Idle_Watchful, Walk_Nervous, Run_Frantic,
// Attack_Lunge, Wall_slam, Scream); other models only need setClipName().
class EnemyCharacter {
public:
    // Attaches an already loaded shared model: EnemyCharacter loads nothing and does not own the
    // model's GPU resources, it just holds a pointer. sharedModel == nullptr (failed to load, or
    // not ready yet) means the character is simply not drawn (see isLoaded()/draw()).
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

    // Explicit state -> clip name mapping, set once at init, so update()/draw() do not hardcode
    // strings.
    void setClipName(State state, const std::string& clipName);

    void setState(State state); // instant; used with update() below for smooth pose blending
    void update(float deltaTime);

    void setPosition(const glm::vec3& pos) { m_position = pos; }
    void setYawDegrees(float yaw) { m_yawDegrees = yaw; }
    glm::vec3 position() const { return m_position; }

    void draw(GLint uModelLoc, GLint uBoneMatricesLoc) const;

    bool isLoaded() const { return m_loaded; }

    // Passthrough to the model's texture, for DungeonScene to bind before drawing. Guarded against
    // m_model == nullptr (before attachSharedModel() or after a failed load).
    GLuint diffuseTexture() const { return m_model ? m_model->diffuseTexture() : 0; }
    bool hasDiffuseTexture() const { return m_model && m_model->hasDiffuseTexture(); }

private:
    // Pointer to the shared model (see the class comment): non-owning, frees nothing in the
    // destructor/destroy(). It can be nullptr (model not loaded/attached), so every use below must
    // check m_loaded before dereferencing.
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

    // For Walk/Run the clip advances by the distance actually traveled rather than by real time
    // (see update() in the .cpp), which needs the position at the end of the previous frame to
    // compute a delta.
    glm::vec3 m_lastUpdatePosition{ 0.0f };
    bool m_lastUpdatePositionInit = false;

    std::vector<glm::mat4> m_boneMatrices; // reusable buffer — not allocated every frame
};
