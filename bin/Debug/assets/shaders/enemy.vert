#version 330 core

// Skeletal skinning for glTF models (SkinnedModel), a standalone shader because it is a different
// kind of geometry than scene.vert's. aJoints/aWeights follow glTF: up to 4 bones per vertex,
// weights sum to 1; the transform is a weighted sum of bone matrices (linear blend skinning).

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in ivec4 aJoints;
layout(location = 4) in vec4 aWeights;
layout(location = 5) in vec2 aUV;

uniform mat4 view;
uniform mat4 projection;
// World transform for the whole creature (position/rotation in the maze); separate from skinning,
// which moves bones inside the model, in its local space.
uniform mat4 model;

// boneMatrices already include the inverse bind matrix (computed on the CPU once per frame for all
// vertices, not here per vertex). MAX_BONES = 64 leaves headroom for a simple humanoid rig.
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
    // mat3(skinMatrix) rotates the normal correctly as long as the bones do not scale geometry
    // non-uniformly (typical for character rigging). An exact normal matrix (inverse-transpose)
    // could be computed on the CPU like skinMatrix, but for a typical humanoid rig the visual
    // difference is negligible.
    vNormal = normalize(mat3(model) * mat3(skinMatrix) * aNormal);
    vColor = aColor;
    vUV = aUV;

    gl_Position = projection * view * worldPos4;
}
