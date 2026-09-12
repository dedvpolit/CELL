#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <unordered_map>

// ============================================================================
// SkinnedModel — загрузчик анимированных glTF-моделей (например, "THE
// WRAPPED" от Codyanka, CC0) через cgltf (third_party/cgltf.h, MIT).
//
// Почему glTF, а не FBX напрямую: FBX — плохо документированный, местами
// проприетарный бинарный формат; писать под него парсер с нуля — плохая
// идея. glTF — открытый, специально спроектированный чтобы его было
// легко парсить формат (JSON + бинарные буферы). Экспортируется из
// Blender (у большинства бесплатных моделей есть .blend-исходник) в один
// .glb файл, дальше читается уже этим классом.
//
// Поддерживает ОДИН меш с ОДНИМ скином (типичный случай для простого
// персонажа) — этого достаточно для THE WRAPPED и подобных простых
// риггованных моделей одного персонажа. Несколько именованных клипов
// анимации хранятся все сразу (см. clipCount()/clipName()) — ничего не
// теряется при загрузке, даже если модель используется только в одном
// состоянии за раз.
// ============================================================================
class SkinnedModel {
public:
    // path — относительно assets/ (см. AssetPath::Resolve), .glb или .gltf.
    // loadDiffuseTexture=false (дефолт) — не декодировать/заливать
    // встроенную диффузную текстуру модели даже если она есть в файле.
    // ОПТИМИЗАЦИЯ ПАМЯТИ: враг (см. DungeonScene::m_enemySharedModel)
    // рисуется вершинным цветом, а не текстурой (см. большой комментарий
    // в DungeonScene.cpp у glUniform1f(m_uEnemyHasDiffuseTex, ...)) — с
    // текстурой модель, по отзыву, понравилась меньше. Раньше текстура
    // всё равно декодировалась и заливалась в VRAM (256x256 RGBA8 +
    // мипы) при КАЖДОЙ загрузке модели впустую — параметр просто
    // пропускает эту работу, ничего не удаляя из парсера: если другой
    // модели текстура понадобится, достаточно передать true при вызове.
    bool load(const std::string& path, bool loadDiffuseTexture = false);
    void destroy();

    // Индекс клипа по имени (см. животные названия из glTF: "Walk_Nervous",
    // "Idle_Watchful" и т.д.) или -1, если такого клипа нет в файле.
    int findClipIndex(const std::string& name) const;
    int clipCount() const { return (int)m_clips.size(); }
    const std::string& clipName(int i) const { return m_clips[i].name; }
    float clipDuration(int i) const { return m_clips[i].duration; }

    int jointCount() const { return (int)m_joints.size(); }

    // Диффузная текстура из материала модели (см. SkinnedModel.cpp —
    // читается прямо из .glb, если она там встроена, как у THE WRAPPED).
    // hasDiffuseTexture()==false — рисовать вершинным цветом (fallback),
    // как раньше.
    GLuint diffuseTexture() const { return m_diffuseTexture; }
    bool hasDiffuseTexture() const { return m_hasDiffuseTexture; }

    // Считает матрицы скиннинга (уже включают inverse bind pose) для
    // заданного клипа в заданный момент времени (секунды, зацикливается
    // по длине клипа сама). outMatrices дозаполняется/обрезается до
    // jointCount() — можно передавать один и тот же вектор каждый кадр.
    void sampleAnimation(int clipIndex, float timeSeconds, std::vector<glm::mat4>& outMatrices) const;

    // То же самое, но линейно смешивает ДВА клипа (для плавных переходов
    // между состояниями ИИ — та же идея, что уже используется для
    // покачивания/подъёма факела, просто теперь это блендинг поз, а не
    // просто чисел). blend=0 -> чистый clipA, blend=1 -> чистый clipB.
    void sampleAnimationBlended(
        int clipIndexA, float timeA,
        int clipIndexB, float timeB,
        float blend,
        std::vector<glm::mat4>& outMatrices) const;

    void draw() const;

private:
    struct Joint {
        std::string name;
        int parentIndex = -1; // -1 = корень скелета
        glm::mat4 inverseBindMatrix{ 1.0f };
        // Rest-позиция (T*R*S из самого узла glTF) — используется как
        //fallback для каналов, которых нет в конкретном клипе (не
        // каждый клип обязан анимировать каждую кость).
        glm::vec3 restTranslation{ 0.0f };
        glm::quat restRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 restScale{ 1.0f };
    };

    // Одна анимируемая кость в рамках ОДНОГО клипа — раздельные треки
    // T/R/S (в glTF это отдельные каналы, кость может анимировать не
    // все три сразу).
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
        // индекс сустава -> его трек в этом клипе (не для каждого
        // сустава обязательно есть запись — см. JointTrack fallback).
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

    // ОПТИМИЗАЦИЯ ("аллокация каждый кадр на каждого врага") —
    // переиспользуемый scratch-буфер для computeGlobalTransforms()
    // внутри sampleAnimation()/sampleAnimationBlended() (см. большой
    // комментарий в .cpp) вместо локального std::vector, выделяемого
    // заново на каждый вызов. mutable — это рабочая память метода, не
    // часть логического состояния модели (не нарушает const у методов
    // sampleAnimation()/sampleAnimationBlended() выше). Безопасно
    // переиспользовать между врагами: SkinnedModel теперь общая на всех
    // (см. DungeonScene::m_enemySharedModel), но вызовы происходят по
    // очереди в одном потоке, не пересекаются и не рекурсируют.
    mutable std::vector<glm::mat4> m_globalTransformScratch;
};
