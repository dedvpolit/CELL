#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "render/SkinnedModel.h"

// ============================================================================
// EnemyCharacter — держит ССЫЛКУ на общую (одну на всех существ,
// см. DungeonScene::m_enemySharedModel) загруженную SkinnedModel + СВОЮ
// собственную позицию/поворот в мире + СВОЁ текущее анимационное
// состояние. НЕ содержит ИИ (погоню, восприятие игрока и т.д.) — это
// чисто "тело + анимация", та же роль, что у PlayerTorchViewmodel для
// факела: сначала инфраструктура, поведение — отдельным, следующим шагом.
//
// БАГФИКС/ОПТИМИЗАЦИЯ (жалоба "игра жрёт 160-190 МБ ОЗУ"): раньше КАЖДЫЙ
// EnemyCharacter (dev-манекен + все kEnemyCount настоящих врагов, т.е. 8
// штук) грузил СВОЙ СОБСТВЕННЫЙ независимый экземпляр SkinnedModel из
// ОДНОГО И ТОГО ЖЕ файла (the_wrapped.glb) — 8 независимых копий меша и
// анимационных клипов на CPU, 8 отдельных VAO/VBO/EBO и 8 отдельных
// диффузных текстур на GPU, хотя все 8 существ визуально идентичны
// (отличаются только позицией/ИИ-состоянием). Теперь модель грузится
// РОВНО ОДИН РАЗ (см. DungeonScene::m_enemySharedModel), а каждый
// EnemyCharacter лишь ссылается на неё через attachSharedModel() —
// экономит ~7/8 памяти, которую раньше тратил враг, ничего не меняя
// визуально (SkinnedModel::draw()/sampleAnimation() и так были
// логически "только для чтения" операциями над общими данными — общий
// указатель на них ничего не ломает, отдельным остаётся только
// per-инстансный m_boneMatrices ниже, который и должен быть свой у
// каждого).
//
// Состояния были явно поименованы по названиям клипов в THE WRAPPED
// (Codyanka, CC0): Idle_Watchful, Walk_Nervous, Run_Frantic, Attack_Lunge,
// Wall_slam, "Scream.lol". Если у другой модели имена клипов другие —
// поменять только setClipNames() ниже, остальной код не привязан к
// конкретным строкам.
// ============================================================================
class EnemyCharacter {
public:
    // Присоединяет уже загруженную ОБЩУЮ модель (см.
    // DungeonScene::m_enemySharedModel) — сам EnemyCharacter ничего не
    // грузит и не владеет GPU-ресурсами модели, только держит указатель
    // на неё. sharedModel == nullptr (модель не удалось загрузить, либо
    // намеренно ещё не готова) — тогда персонаж просто не рисуется (см.
    // isLoaded()/draw()), как раньше при неудачном load().
    void attachSharedModel(const SkinnedModel* sharedModel);
    void destroy(); // сбрасывает указатель/состояние; GPU-ресурсы САМОЙ модели не трогает (см. выше)

    enum class State {
        Idle,
        Walk,
        Run,
        Attack,
        WallSlam,
        Scream
    };

    // Явное сопоставление состояние -> имя клипа в файле — один раз при
    // инициализации, чтобы не хардкодить строки внутри update()/draw().
    void setClipName(State state, const std::string& clipName);

    void setState(State state); // мгновенно — используется setState + update() ниже для плавного блендинга между позами
    void update(float deltaTime);

    void setPosition(const glm::vec3& pos) { m_position = pos; }
    void setYawDegrees(float yaw) { m_yawDegrees = yaw; }
    glm::vec3 position() const { return m_position; }

    // uModel/uBoneMatrices — те же имена uniform'ов, что в enemy.vert.
    void draw(GLint uModelLoc, GLint uBoneMatricesLoc) const;

    bool isLoaded() const { return m_loaded; }

    // Проброс к текстуре модели (см. SkinnedModel::hasDiffuseTexture()) —
    // для DungeonScene, чтобы забиндить перед отрисовкой. Гвард на
    // m_model==nullptr — до attachSharedModel()/при неудачной загрузке.
    GLuint diffuseTexture() const { return m_model ? m_model->diffuseTexture() : 0; }
    bool hasDiffuseTexture() const { return m_model && m_model->hasDiffuseTexture(); }

private:
    // Указатель на ОБЩУЮ модель (см. большой комментарий в начале файла
    // и DungeonScene::m_enemySharedModel) — НЕ владеющий, ничего не
    // освобождает в деструкторе/destroy(). Может быть nullptr (модель не
    // загружена/не присоединена) — везде ниже это обязано проверяться
    // через m_loaded ДО разыменования.
    const SkinnedModel* m_model = nullptr;
    bool m_loaded = false;

    std::string m_clipNames[6]; // индекс — тот же порядок, что в enum State

    State m_currentState = State::Idle;
    State m_previousState = State::Idle;
    float m_stateTime = 0.0f;      // время внутри ТЕКУЩЕГО состояния — для блендинга
    float m_currentClipTime = 0.0f;
    float m_previousClipTime = 0.0f;

    glm::vec3 m_position{ 0.0f };
    float m_yawDegrees = 0.0f;

    // УЛУЧШЕНИЕ ("враг скользит, а не идёт") — см. большой комментарий в
    // update() (.cpp): для Walk/Run клип двигается по фактически
    // пройденному расстоянию, а не по реальному времени — нужна позиция
    // на КОНЕЦ предыдущего кадра, чтобы посчитать дельту.
    glm::vec3 m_lastUpdatePosition{ 0.0f };
    bool m_lastUpdatePositionInit = false;

    std::vector<glm::mat4> m_boneMatrices; // переиспользуемый буфер — не аллоцировать каждый кадр
};
