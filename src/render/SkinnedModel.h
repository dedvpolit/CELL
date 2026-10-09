#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <unordered_map>

// glTF loader (cgltf) for skinned models such as THE WRAPPED (Codyanka, CC0):
// one mesh, one skin, all clips kept in memory
class SkinnedModel {
public:
    // path is relative to assets/
    // The diffuse texture is skipped by default: the enemy uses vertex colors
    bool load(const std::string& path, bool loadDiffuseTexture = false);
    void destroy();

    int findClipIndex(const std::string& name) const;

    // Material diffuse texture;
    // false means draw with vertex colors
    GLuint diffuseTexture() const { return m_diffuseTexture; }
    bool hasDiffuseTexture() const { return m_hasDiffuseTexture; }

    // Skinning matrices (inverse bind included) for a clip at a time in seconds;
    // loops by clip length
    // outMatrices is resized to the joint count and can be reused
    void sampleAnimation(int clipIndex, float timeSeconds, std::vector<glm::mat4>& outMatrices) const;

    // Linear blend of two clips (0 = clipA, 1 = clipB) for smooth state transitions
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
        // Rest pose (T*R*S from the glTF node itself):
        // the fallback for channels missing in a clip (not every clip animates every bone)
        glm::vec3 restTranslation{ 0.0f };
        glm::quat restRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 restScale{ 1.0f };
    };

    // One animated bone within a single clip:
    // separate T/R/S tracks (in glTF they are separate channels;
    // a bone does not have to animate all three)
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
        // joint index -> its track in this clip
        // (not every joint necessarily has an entry: see the JointTrack fallback)
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

    // Scratch for computeGlobalTransforms(); not thread-safe
    mutable std::vector<glm::mat4> m_globalTransformScratch;
};
