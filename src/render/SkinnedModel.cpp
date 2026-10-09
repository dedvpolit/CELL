#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "SkinnedModel.h"
#include "AssetPath.h"

// glm::slerp lives in the experimental gtx header, which needs this opt-in (meooooow)
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cstddef>
#include <cstdio>
#include <cmath>
#include <algorithm>

#include "stb_image.h"

// Actually i didnt write SkinnedMidel so dont know exactly how it works
namespace {

struct RawVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;
    int joints[4];
    float weights[4];
};

glm::vec3 sampleVec3Track(const std::vector<float>& times, const std::vector<glm::vec3>& values,
                           float t, const glm::vec3& fallback)
{
    if (times.empty()) return fallback;
    if (times.size() == 1 || t <= times.front()) return values.front();
    if (t >= times.back()) return values.back();
    for (size_t i = 1; i < times.size(); ++i)
    {
        if (t <= times[i])
        {
            float t0 = times[i - 1], t1 = times[i];
            float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
            return glm::mix(values[i - 1], values[i], alpha);
        }
    }
    return values.back();
}

glm::quat sampleQuatTrack(const std::vector<float>& times, const std::vector<glm::quat>& values,
                           float t, const glm::quat& fallback)
{
    if (times.empty()) return fallback;
    if (times.size() == 1 || t <= times.front()) return values.front();
    if (t >= times.back()) return values.back();
    for (size_t i = 1; i < times.size(); ++i)
    {
        if (t <= times[i])
        {
            float t0 = times[i - 1], t1 = times[i];
            float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
            return glm::slerp(values[i - 1], values[i], alpha);
        }
    }
    return values.back();
}

} // namespace

bool SkinnedModel::load(const std::string& path, bool loadDiffuseTexture)
{
    const std::string fullPath = AssetPath::Resolve(path);
    if (fullPath.empty())
    {
        std::fprintf(stderr, "[SkinnedModel] file not found: %s\n", path.c_str());
        return false;
    }

    cgltf_options options = {};
    cgltf_data* data = nullptr;

    cgltf_result result = cgltf_parse_file(&options, fullPath.c_str(), &data);
    if (result != cgltf_result_success)
    {
        std::fprintf(stderr, "[SkinnedModel] cgltf_parse_file failed (%d): %s\n", (int)result, fullPath.c_str());
        return false;
    }

    result = cgltf_load_buffers(&options, data, fullPath.c_str());
    if (result != cgltf_result_success)
    {
        std::fprintf(stderr, "[SkinnedModel] cgltf_load_buffers failed (%d): %s\n", (int)result, fullPath.c_str());
        cgltf_free(data);
        return false;
    }

    if (data->meshes_count == 0 || data->skins_count == 0)
    {
        std::fprintf(stderr, "[SkinnedModel] no mesh/skin found in: %s (meshes=%zu skins=%zu)\n",
            fullPath.c_str(), data->meshes_count, data->skins_count);
        cgltf_free(data);
        return false;
    }

    const cgltf_mesh& mesh = data->meshes[0];
    if (mesh.primitives_count == 0)
    {
        std::fprintf(stderr, "[SkinnedModel] mesh has no primitives: %s\n", fullPath.c_str());
        cgltf_free(data);
        return false;
    }
    const cgltf_primitive& prim = mesh.primitives[0];

    const cgltf_accessor* posAcc = nullptr;
    const cgltf_accessor* normAcc = nullptr;
    const cgltf_accessor* colorAcc = nullptr;
    const cgltf_accessor* uvAcc = nullptr;
    const cgltf_accessor* jointsAcc = nullptr;
    const cgltf_accessor* weightsAcc = nullptr;

    for (cgltf_size i = 0; i < prim.attributes_count; ++i)
    {
        const cgltf_attribute& attr = prim.attributes[i];
        switch (attr.type)
        {
            case cgltf_attribute_type_position: posAcc = attr.data; break;
            case cgltf_attribute_type_normal:   normAcc = attr.data; break;
            // Takes the last COLOR attribute, not strictly COLOR_0:
            //THE WRAPPED has two and the  last one looks right
            case cgltf_attribute_type_color:
                colorAcc = attr.data;
                break;
            case cgltf_attribute_type_texcoord:
                uvAcc = attr.data;
                break;
            case cgltf_attribute_type_joints:   jointsAcc = attr.data; break;
            case cgltf_attribute_type_weights:  weightsAcc = attr.data; break;
            default: break;
        }
    }

    if (!posAcc || !jointsAcc || !weightsAcc)
    {
        std::fprintf(stderr,
            "[SkinnedModel] mesh missing POSITION/JOINTS_0/WEIGHTS_0 (pos=%p joints=%p weights=%p): %s\n",
            (void*)posAcc, (void*)jointsAcc, (void*)weightsAcc, fullPath.c_str());
        cgltf_free(data);
        return false;
    }

    const cgltf_size vertexCount = posAcc->count;
    std::vector<RawVertex> verts(vertexCount);

    for (cgltf_size i = 0; i < vertexCount; ++i)
    {
        float p[3] = { 0, 0, 0 };
        cgltf_accessor_read_float(posAcc, i, p, 3);
        verts[i].pos = glm::vec3(p[0], p[1], p[2]);

        if (normAcc)
        {
            float nrm[3] = { 0, 1, 0 };
            cgltf_accessor_read_float(normAcc, i, nrm, 3);
            verts[i].normal = glm::vec3(nrm[0], nrm[1], nrm[2]);
        }
        else
        {
            verts[i].normal = glm::vec3(0, 1, 0);
        }

        if (colorAcc)
        {
            float c[4] = { 1, 1, 1, 1 };
            cgltf_accessor_read_float(colorAcc, i, c, 4);
            verts[i].color = glm::vec3(c[0], c[1], c[2]);
        }
        else
        {
            verts[i].color = glm::vec3(1.0f);
        }

        if (uvAcc)
        {
            float uv[2] = { 0, 0 };
            cgltf_accessor_read_float(uvAcc, i, uv, 2);
            verts[i].uv = glm::vec2(uv[0], uv[1]);
        }
        else
        {
            verts[i].uv = glm::vec2(0.0f);
        }

        cgltf_uint j[4] = { 0, 0, 0, 0 };
        cgltf_accessor_read_uint(jointsAcc, i, j, 4);
        verts[i].joints[0] = (int)j[0];
        verts[i].joints[1] = (int)j[1];
        verts[i].joints[2] = (int)j[2];
        verts[i].joints[3] = (int)j[3];

        float w[4] = { 0, 0, 0, 0 };
        cgltf_accessor_read_float(weightsAcc, i, w, 4);
        verts[i].weights[0] = w[0];
        verts[i].weights[1] = w[1];
        verts[i].weights[2] = w[2];
        verts[i].weights[3] = w[3];
    }

    std::vector<GLuint> indices;
    if (prim.indices)
    {
        indices.resize(prim.indices->count);
        for (cgltf_size i = 0; i < prim.indices->count; ++i)
            indices[i] = (GLuint)cgltf_accessor_read_index(prim.indices, i);
    }
    else
    {
        indices.resize(vertexCount);
        for (cgltf_size i = 0; i < vertexCount; ++i)
            indices[i] = (GLuint)i;
    }

    const cgltf_skin& skin = data->skins[0];
    m_joints.resize(skin.joints_count);

    std::unordered_map<const cgltf_node*, int> nodeToJoint;
    for (cgltf_size i = 0; i < skin.joints_count; ++i)
        nodeToJoint[skin.joints[i]] = (int)i;

    for (cgltf_size i = 0; i < skin.joints_count; ++i)
    {
        const cgltf_node* node = skin.joints[i];
        Joint& joint = m_joints[i];
        joint.name = node->name ? node->name : ("joint_" + std::to_string(i));

        // Nearest ancestor that is a joint of this skin;
        // armatures often have non-joint nodes in between.
        joint.parentIndex = -1;
        for (const cgltf_node* p = node->parent; p != nullptr; p = p->parent)
        {
            auto it = nodeToJoint.find(p);
            if (it != nodeToJoint.end())
            {
                joint.parentIndex = it->second;
                break;
            }
        }

        if (node->has_translation)
            joint.restTranslation = glm::vec3(node->translation[0], node->translation[1], node->translation[2]);
        if (node->has_rotation)
            joint.restRotation = glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]);
        if (node->has_scale)
            joint.restScale = glm::vec3(node->scale[0], node->scale[1], node->scale[2]);

        if (skin.inverse_bind_matrices)
        {
            float m[16];
            cgltf_accessor_read_float(skin.inverse_bind_matrices, i, m, 16);
            joint.inverseBindMatrix = glm::make_mat4(m);
        }
    }

    m_clips.resize(data->animations_count);
    for (cgltf_size a = 0; a < data->animations_count; ++a)
    {
        const cgltf_animation& anim = data->animations[a];
        AnimationClip& clip = m_clips[a];
        clip.name = anim.name ? anim.name : ("clip_" + std::to_string(a));

        for (cgltf_size c = 0; c < anim.channels_count; ++c)
        {
            const cgltf_animation_channel& channel = anim.channels[c];
            auto it = nodeToJoint.find(channel.target_node);
            if (it == nodeToJoint.end())
                continue; // channel animates a node that isnot a joint of this skin (meow meow)

            int jointIndex = it->second;
            const cgltf_animation_sampler* sampler = channel.sampler;
            if (!sampler || !sampler->input || !sampler->output)
                continue;

            cgltf_size keyCount = sampler->input->count;
            JointTrack& track = clip.tracks[jointIndex];

            for (cgltf_size k = 0; k < keyCount; ++k)
            {
                float t = 0.0f;
                cgltf_accessor_read_float(sampler->input, k, &t, 1);
                clip.duration = std::max(clip.duration, t);

                if (channel.target_path == cgltf_animation_path_type_translation)
                {
                    float v[3];
                    cgltf_accessor_read_float(sampler->output, k, v, 3);
                    track.tTimes.push_back(t);
                    track.tValues.push_back(glm::vec3(v[0], v[1], v[2]));
                }
                else if (channel.target_path == cgltf_animation_path_type_rotation)
                {
                    float v[4];
                    cgltf_accessor_read_float(sampler->output, k, v, 4);
                    track.rTimes.push_back(t);
                    track.rValues.push_back(glm::quat(v[3], v[0], v[1], v[2]));
                }
                else if (channel.target_path == cgltf_animation_path_type_scale)
                {
                    float v[3];
                    cgltf_accessor_read_float(sampler->output, k, v, 3);
                    track.sTimes.push_back(t);
                    track.sValues.push_back(glm::vec3(v[0], v[1], v[2]));
                }
            }
        }
    }

    // Skip decoding and uploading a texture that will not be drawn
    if (loadDiffuseTexture &&
        prim.material &&
        prim.material->has_pbr_metallic_roughness &&
        prim.material->pbr_metallic_roughness.base_color_texture.texture &&
        prim.material->pbr_metallic_roughness.base_color_texture.texture->image)
    {
        const cgltf_image* image = prim.material->pbr_metallic_roughness.base_color_texture.texture->image;

        const unsigned char* pngData = nullptr;
        size_t pngSize = 0;

        if (image->buffer_view)
        {
            const cgltf_buffer_view* bv = image->buffer_view;
            pngData = (const unsigned char*)bv->buffer->data + bv->offset;
            pngSize = bv->size;
        }

        if (pngData && pngSize > 0)
        {
            int texW = 0, texH = 0, texChannels = 0;
            unsigned char* pixels = stbi_load_from_memory(pngData, (int)pngSize, &texW, &texH, &texChannels, 4);
            if (pixels)
            {
                glGenTextures(1, &m_diffuseTexture);
                glBindTexture(GL_TEXTURE_2D, m_diffuseTexture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glGenerateMipmap(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, 0);

                m_hasDiffuseTexture = true;
                std::printf("[SkinnedModel] loaded embedded diffuse texture %dx%d (%d channels in file)\n",
                    texW, texH, texChannels);

                stbi_image_free(pixels);
            }
            else
            {
                std::fprintf(stderr, "[SkinnedModel] failed to decode embedded diffuse texture: %s\n",
                    stbi_failure_reason());
            }
        }
        else
        {
            std::fprintf(stderr,
                "[SkinnedModel] material references a texture, but it isn't embedded (external image files not supported)\n");
        }
    }

    cgltf_free(data);

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(RawVertex)), verts.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RawVertex), (void*)offsetof(RawVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RawVertex), (void*)offsetof(RawVertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(RawVertex), (void*)offsetof(RawVertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_INT, sizeof(RawVertex), (void*)offsetof(RawVertex, joints));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(RawVertex), (void*)offsetof(RawVertex, weights));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(RawVertex), (void*)offsetof(RawVertex, uv));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices.size() * sizeof(GLuint)), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);

    m_indexCount = (GLsizei)indices.size();

    std::printf("[SkinnedModel] loaded '%s': %d vertices, %d indices, %d joints, %d clips\n",
        fullPath.c_str(), (int)vertexCount, (int)indices.size(), (int)m_joints.size(), (int)m_clips.size());
    for (const AnimationClip& clip : m_clips)
        std::printf("  clip: '%s' (%.2fs)\n", clip.name.c_str(), clip.duration);

    return true;
}

glm::mat4 SkinnedModel::localJointTransform(const AnimationClip& clip, int jointIndex, float time) const
{
    const Joint& joint = m_joints[jointIndex];

    glm::vec3 t = joint.restTranslation;
    glm::quat r = joint.restRotation;
    glm::vec3 s = joint.restScale;

    auto it = clip.tracks.find(jointIndex);
    if (it != clip.tracks.end())
    {
        const JointTrack& track = it->second;
        t = sampleVec3Track(track.tTimes, track.tValues, time, t);
        r = sampleQuatTrack(track.rTimes, track.rValues, time, r);
        s = sampleVec3Track(track.sTimes, track.sValues, time, s);
    }

    glm::mat4 T = glm::translate(glm::mat4(1.0f), t);
    glm::mat4 R = glm::mat4_cast(r);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), s);
    return T * R * S;
}

void SkinnedModel::computeGlobalTransforms(const AnimationClip& clip, float time, std::vector<glm::mat4>& outGlobal) const
{
    outGlobal.resize(m_joints.size());
    // glTF exporters list joints parents first, so one linear pass suffices
    for (size_t i = 0; i < m_joints.size(); ++i)
    {
        glm::mat4 local = localJointTransform(clip, (int)i, time);
        int parent = m_joints[i].parentIndex;
        outGlobal[i] = (parent >= 0) ? (outGlobal[parent] * local) : local;
    }
}

void SkinnedModel::sampleAnimation(int clipIndex, float timeSeconds, std::vector<glm::mat4>& outMatrices) const
{
    outMatrices.assign(m_joints.size(), glm::mat4(1.0f));
    if (clipIndex < 0 || clipIndex >= (int)m_clips.size())
        return;

    const AnimationClip& clip = m_clips[clipIndex];
    float t = clip.duration > 0.0f ? std::fmod(timeSeconds, clip.duration) : 0.0f;
    if (t < 0.0f) t += clip.duration;

    computeGlobalTransforms(clip, t, m_globalTransformScratch);

    for (size_t i = 0; i < m_joints.size(); ++i)
        outMatrices[i] = m_globalTransformScratch[i] * m_joints[i].inverseBindMatrix;
}

void SkinnedModel::sampleAnimationBlended(
    int clipIndexA, float timeA,
    int clipIndexB, float timeB,
    float blend,
    std::vector<glm::mat4>& outMatrices) const
{
    blend = glm::clamp(blend, 0.0f, 1.0f);

    if (blend <= 0.0001f || clipIndexB < 0)
    {
        sampleAnimation(clipIndexA, timeA, outMatrices);
        return;
    }
    if (blend >= 0.9999f || clipIndexA < 0)
    {
        sampleAnimation(clipIndexB, timeB, outMatrices);
        return;
    }

    outMatrices.assign(m_joints.size(), glm::mat4(1.0f));

    if (clipIndexA < 0 || clipIndexA >= (int)m_clips.size() ||
        clipIndexB < 0 || clipIndexB >= (int)m_clips.size())
        return;

    const AnimationClip& clipA = m_clips[clipIndexA];
    const AnimationClip& clipB = m_clips[clipIndexB];

    float tA = clipA.duration > 0.0f ? std::fmod(timeA, clipA.duration) : 0.0f;
    if (tA < 0.0f) tA += clipA.duration;
    float tB = clipB.duration > 0.0f ? std::fmod(timeB, clipB.duration) : 0.0f;
    if (tB < 0.0f) tB += clipB.duration;

    // Blend T/R/S per joint, not matrices (interpolated rotations would swim), then compose.
    m_globalTransformScratch.resize(m_joints.size());
    std::vector<glm::mat4>& globalBlended = m_globalTransformScratch;

    for (size_t i = 0; i < m_joints.size(); ++i)
    {
        const Joint& joint = m_joints[i];

        glm::vec3 tA_ = joint.restTranslation, tB_ = joint.restTranslation;
        glm::quat rA_ = joint.restRotation, rB_ = joint.restRotation;
        glm::vec3 sA_ = joint.restScale, sB_ = joint.restScale;

        auto itA = clipA.tracks.find((int)i);
        if (itA != clipA.tracks.end())
        {
            tA_ = sampleVec3Track(itA->second.tTimes, itA->second.tValues, tA, tA_);
            rA_ = sampleQuatTrack(itA->second.rTimes, itA->second.rValues, tA, rA_);
            sA_ = sampleVec3Track(itA->second.sTimes, itA->second.sValues, tA, sA_);
        }
        auto itB = clipB.tracks.find((int)i);
        if (itB != clipB.tracks.end())
        {
            tB_ = sampleVec3Track(itB->second.tTimes, itB->second.tValues, tB, tB_);
            rB_ = sampleQuatTrack(itB->second.rTimes, itB->second.rValues, tB, rB_);
            sB_ = sampleVec3Track(itB->second.sTimes, itB->second.sValues, tB, sB_);
        }

        glm::vec3 tBlend = glm::mix(tA_, tB_, blend);
        glm::quat rBlend = glm::slerp(rA_, rB_, blend);
        glm::vec3 sBlend = glm::mix(sA_, sB_, blend);

        glm::mat4 local =
            glm::translate(glm::mat4(1.0f), tBlend)
            * glm::mat4_cast(rBlend)
            * glm::scale(glm::mat4(1.0f), sBlend);

        int parent = joint.parentIndex;
        globalBlended[i] = (parent >= 0) ? (globalBlended[parent] * local) : local;
    }

    for (size_t i = 0; i < m_joints.size(); ++i)
        outMatrices[i] = globalBlended[i] * m_joints[i].inverseBindMatrix;
}

int SkinnedModel::findClipIndex(const std::string& name) const
{
    for (size_t i = 0; i < m_clips.size(); ++i)
        if (m_clips[i].name == name)
            return (int)i;
    return -1;
}

void SkinnedModel::destroy()
{
    if (m_ebo) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_diffuseTexture) { glDeleteTextures(1, &m_diffuseTexture); m_diffuseTexture = 0; }
    m_hasDiffuseTexture = false;
    m_indexCount = 0;
    m_joints.clear();
    m_clips.clear();
}

void SkinnedModel::draw() const
{
    if (m_indexCount <= 0)
        return;
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}
