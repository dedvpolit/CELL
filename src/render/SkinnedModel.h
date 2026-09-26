#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <unordered_map>

// glTF loader (cgltf) for animated models such as THE WRAPPED (Codyanka, CC0): one mesh with one
// skin, all named clips kept in memory (look one up with findClipIndex()). glTF instead of FBX
// because it is open and easy to parse.
class SkinnedModel {
public:
    // path is relative to assets/. loadDiffuseTexture = false (default) skips decoding the embedded
    // texture: the enemy is drawn with vertex color only (see the note in DungeonScene.cpp), so it
    // would be wasted VRAM. Enabling it changes the enemy's look, so check it visually first.
    bool load(const std::string& path, bool loadDiffuseTexture = false);
    void destroy();

    int findClipIndex(const std::string& name) const;

    // Diffuse texture from the model's material (see SkinnedModel.cpp: read directly from the .glb
    // if embedded, as with THE WRAPPED). hasDiffuseTexture() == false means draw with vertex color
    // (the fallback).
    GLuint diffuseTexture() const { return m_diffuseTexture; }
    bool hasDiffuseTexture() const { return m_hasDiffuseTexture; }

    // Computes skinning matrices (already including the inverse bind pose) for a given clip at a
    // given time (seconds; it loops by clip length on its own). outMatrices is resized to
    // the number of joints; the same vector can be passed in every frame.
    void sampleAnimation(int clipIndex, float timeSeconds, std::vector<glm::mat4>& outMatrices) const;

    // The same, but linearly blends two clips (for smooth transitions between AI states, the same
    // idea as the torch sway/raise but as pose blending). blend = 0 is pure clipA, blend = 1 pure
    // clipB.
    void sampleAnimationBlended(
        int clipIndexA, float timeA,
        int clipIndexB, float timeB,
        float blend,
        std::vector<glm::mat4>& outMatrices) const;

    void draw() const;

private:
    struct Joint {
        std::string name;
        int parentIndex = -1; // -1 = skeleton root
        glm::mat4 inverseBindMatrix{ 1.0f };
        // Rest pose (T*R*S from the glTF node itself): the fallback for channels missing in a clip
        // (not every clip animates every bone).
        glm::vec3 restTranslation{ 0.0f };
        glm::quat restRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 restScale{ 1.0f };
    };

    // One animated bone within a single clip: separate T/R/S tracks (in glTF they are separate
    // channels; a bone does not have to animate all three).
    struct JointTrack {
        std::vector<float> tTimes;
        std::vector<glm::vec3> tValues;
        std::vector<float> rTimes;
        std::vector<glm::quat> rValues;
        std::vector<float> sTimes;
        std::vector<glm::vec3> sValues;
    };

    struct AnimationClip {
        std::string name;
        float duration = 0.0f;
        // joint index -> its track in this clip (not every joint necessarily has an entry: see the
        // JointTrack fallback).
        std::unordered_map<int, JointTrack> tracks;
    };

    glm::mat4 localJointTransform(const AnimationClip& clip, int jointIndex, float time) const;
    void computeGlobalTransforms(const AnimationClip& clip, float time, std::vector<glm::mat4>& outGlobal) const;

    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    GLsizei m_indexCount = 0;

    GLuint m_diffuseTexture = 0;
    bool m_hasDiffuseTexture = false;

    std::vector<Joint> m_joints;
    std::vector<AnimationClip> m_clips;

    // Reusable scratch buffer for computeGlobalTransforms(), instead of a local std::vector
    // allocated on every call. mutable: it is a method's working memory, not part of the model's
    // logical state. Not thread-safe by design: SkinnedModel is shared by all enemies
    // (DungeonScene::m_enemySharedModel), and the calls happen sequentially on one thread.
    mutable std::vector<glm::mat4> m_globalTransformScratch;
};
