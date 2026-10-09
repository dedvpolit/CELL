#version 330 core

// Skinning for glTF models: up to 4 joints per vertex, linear blend skinning.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in ivec4 aJoints;
layout(location = 4) in vec4 aWeights;
layout(location = 5) in vec2 aUV;

uniform mat4 view;
uniform mat4 projection;
// Creature transform in the world; skinning happens in model space.
uniform mat4 model;

// Bone matrices include the inverse bind matrix. 64 leaves headroom for a humanoid rig.
#define MAX_BONES 64
uniform mat4 boneMatrices[MAX_BONES];

out vec3 vNormal;
out vec3 vColor;
out vec3 vWorldPos;
out vec2 vUV;

void main()
{
    mat4 skinMatrix =
        aWeights.x * boneMatrices[aJoints.x]
        + aWeights.y * boneMatrices[aJoints.y]
        + aWeights.z * boneMatrices[aJoints.z]
        + aWeights.w * boneMatrices[aJoints.w];

    vec4 skinnedPos = skinMatrix * vec4(aPos, 1.0);
    vec4 worldPos4 = model * skinnedPos;

    vWorldPos = worldPos4.xyz;
    // mat3(skin) is correct for rigs without non-uniform bone scale.
    vNormal = normalize(mat3(model) * mat3(skinMatrix) * aNormal);
    vColor = aColor;
    vUV = aUV;

    gl_Position = projection * view * worldPos4;
}
