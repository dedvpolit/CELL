#version 330 core

// ============================================================================
// Скиннинг (skeletal animation) — для моделей, загруженных через
// SkinnedModel.h/.cpp (glTF, cgltf). Отдельный, самостоятельный шейдер, а
// не ветка в scene.vert: там и так уже плотно (viewmodel-факел, частицы,
// пламя), а скиннинг — принципиально другой тип геометрии (у обычной
// сцены всё либо статично, либо строится на CPU каждый кадр целиком, а
// тут одна и та же топология каждый кадр просто ПЕРЕГИБАЕТСЯ по костям).
//
// aJoints/aWeights — стандартная схема glTF: до 4 костей влияют на одну
// вершину, вклад каждой — соответствующий вес (в сумме = 1.0). Итоговая
// матрица трансформации вершины — взвешенная сумма (а не взвешенное
// среднее позиций — это стандартный, немного приближённый, но
// повсеместно используемый способ, называется linear blend skinning).
// ============================================================================

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in ivec4 aJoints;
layout(location = 4) in vec4 aWeights;
layout(location = 5) in vec2 aUV;

uniform mat4 view;
uniform mat4 projection;
// Мировая трансформация ВСЕГО существа целиком (позиция/поворот в
// лабиринте) — отдельно от скиннинга (который двигает кости ВНУТРИ
// модели, в её локальном пространстве).
uniform mat4 model;

// boneMatrices — уже включают в себя inverse bind matrix (см.
// SkinnedModel::sampleAnimation() — считается на CPU один раз в кадр для
// ВСЕХ вершин сразу, а не здесь по одной на вершину). MAX_BONES=64 — с
// запасом для простого гуманоидного рига (в THE WRAPPED, например,
// ожидается заметно меньше).
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
    // mat3(skinMatrix) корректно вращает нормаль только пока кости не
    // масштабируют геометрию неравномерно (обычный случай для риггинга
    // персонажей) — если понадобится точная normal-matrix (инверс-
    // транспонированная), можно посчитать её на CPU так же, как и сам
    // skinMatrix, но для типичного гуманоидного рига разница на глаз
    // будет незаметна.
    vNormal = normalize(mat3(model) * mat3(skinMatrix) * aNormal);
    vColor = aColor;
    vUV = aUV;

    gl_Position = projection * view * worldPos4;
}
