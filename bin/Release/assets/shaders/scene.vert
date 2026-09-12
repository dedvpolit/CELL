#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aMatId;

uniform mat4 view;
uniform mat4 projection;
uniform float uTime;
// Same camera-position uniform already uploaded for scene.frag's `camPos`
// (see DungeonScene::cacheUniformLocations() / render()) — reused here so
// the particle branch below no longer needs a per-vertex 4x4 matrix
// inverse just to recover where the camera is.
uniform vec3 camPos;

// ---- Факел-viewmodel в руке игрока (см. PlayerTorchViewmodel.h) ----
// true ТОЛЬКО во время отрисовки этой конкретной модели — она передаёт
// свою геометрию в системе координат КАМЕРЫ (aPos/aNormal — фиксированные
// локальные смещения вида "0.3 влево, 0.6 вниз, 0.4 вперёд", а не мировые
// координаты), поэтому здесь нужно сперва восстановить настоящие мировые
// pos/normal через обратную матрицу вида — это ВСЕГДА корректно определено
// (view — это твёрдое движение (поворот+перенос), у него всегда есть
// обратная), в отличие от прежних попыток вручную считать "право"/"верх"
// через cross() направления взгляда, которые дважды вырождались в NaN на
// экстремальных углах. Заодно — раз позиция/нормаль восстанавливаются из
// ТЕКУЩЕЙ матрицы вида, которая уже включает и yaw, и pitch, и покачивание
// камеры — факел автоматически следует за ЛЮБЫМ поворотом камеры (в т.ч.
// "вверх-вниз"), без единой строчки отдельной анимации.
uniform bool uIsViewmodelDraw;
// Посчитана один раз на CPU (см. DungeonScene::render()), а не здесь на
// каждой вершине — inverse(view) в шейдере одинакова для всех ~250 вершин
// этой модели, вычислять её 250 раз вместо одного было бы просто лишней
// работой GPU без всякой пользы.
uniform mat4 uInvView;
uniform vec2 uViewmodelSway;

out vec3 vNormal;
out vec3 vColor;
out vec3 vWorldPos;
out float vMatId;
out float vParticleLife;

float hash(vec2 p)
{
    return fract(
        sin(dot(p, vec2(127.1, 311.7)))
        * 43758.5453123
    );
}

void main()
{
    // Локальные копии атрибутов — во всех ветках ниже используется ИМЕННО
    // pos/normal/matId, не aPos/aNormal/aMatId напрямую, чтобы одна и та же
    // логика (флейм-анимация, обычная геометрия) одинаково работала и для
    // обычной мировой геометрии, и для viewmodel-факела, различие — только
    // в блоке ниже.
    vec3 pos = aPos;
    vec3 normal = aNormal;
    float matId = aMatId;

    if (uIsViewmodelDraw)
    {
        // Небольшое покачивание — "инерция" при повороте + лёгкое дыхание
        // в покое (см. DungeonScene::render(), uViewmodelSway — считается
        // там на CPU простой скалярной арифметикой: разница текущего и
        // сглаженного yaw, никаких cross()/базисов, так что вырождаться
        // здесь нечему). Применяется К ЛОКАЛЬНЫМ координатам (в системе
        // камеры), ДО восстановления мировой позиции через uInvView —
        // поэтому "право"/"вверх" здесь буквально X/Y локальной системы,
        // а не какие-то заново считаемые направления.
        vec3 swayedPos = aPos;
        swayedPos.x += uViewmodelSway.x;
        swayedPos.y += uViewmodelSway.y;

        pos = (uInvView * vec4(swayedPos, 1.0)).xyz;
        normal = mat3(uInvView) * aNormal;
        // Зарезервированное смещение +10.0 (см. PlayerTorchViewmodel.cpp)
        // снимаем здесь же — дальше по функции matId снова в тех же
        // диапазонах (1.5..2.5 пламя, обычная геометрия), что и для
        // остальной сцены, никакой отдельной ветки материалов не нужно.
        matId -= 10.0;
    }

    // ==================================================
    // Огонь
    //
    // matId:
    // 2.0 + torchIndex / 100
    // ==================================================

    if (matId > 1.5 && matId < 2.5)
    {
        const float FLAME_RADIUS = 0.10;

        // normal ����� �������� ��������� ��������
        // ����� ������ �������� 1.
        vec3 local =
            normal;

        // ��������������� ����� �����.
        vec3 center =
            pos
            - normal * FLAME_RADIUS;

        // ID ������ (2x ����� ��� MAX_TORCHES = 1024).
        const float FLAME_ID_SCALE =
            4096.0;

        float torchId =
            floor(
                (matId - 2.0)
                * FLAME_ID_SCALE
                + 0.5
            );

        float seed =
            hash(
                vec2(
                    torchId + 17.31,
                    torchId * 3.71 + 9.17
                )
            );

        float time =
            uTime
            * (
                5.5
                + seed * 1.5
            );

        // ������� ����� �������.
        float upper =
            smoothstep(
                0.0,
                0.85,
                max(local.y, 0.0)
            );

        // ==================================================
        // ����������
        // ==================================================

        float stretch =
            1.0
            + upper
            * (
                0.16
                + 0.07
                * sin(
                    time * 1.37
                    + seed * 8.0
                    + local.x * 5.0
                )
            );

        // ==================================================
        // ������
        // ==================================================

        float squeeze =
            1.0
            - upper * 0.16
            +
            0.045
            * sin(
                time * 1.73
                + local.y * 8.0
                + seed * 11.0
            );

        // ==================================================
        // ����������� � �������
        // ==================================================

        float swayX =
            (
                sin(
                    uTime * 7.0
                    + seed * 14.0
                )
                * 0.026
                +
                sin(
                    uTime * 12.0
                    + seed * 5.0
                )
                * 0.011
            )
            *
            upper
            *
            upper;

        float swayZ =
            (
                cos(
                    uTime * 6.3
                    + seed * 9.0
                )
                * 0.020
                +
                cos(
                    uTime * 10.7
                    + seed * 17.0
                )
                * 0.009
            )
            *
            upper
            *
            upper;

        // ==================================================
        // ���������� �����������
        // ==================================================

        float wobble =
            sin(
                time * 2.3
                + local.x * 9.0
                + local.z * 7.0
            )
            *
            0.012
            *
            upper;

        vec3 animatedLocal;

        animatedLocal.x =
            local.x
            * FLAME_RADIUS
            * squeeze
            + wobble;

        animatedLocal.y =
            local.y
            * FLAME_RADIUS
            * stretch;

        animatedLocal.z =
            local.z
            * FLAME_RADIUS
            * squeeze;

        animatedLocal.x += swayX;
        animatedLocal.z += swayZ;

        animatedLocal.y +=
            upper
            * (
                0.012
                * sin(
                    uTime * 8.0
                    + seed * 10.0
                )
            );

        vec3 worldPos =
            center
            + animatedLocal;

        vNormal =
            normalize(
                vec3(
                    local.x * squeeze,
                    local.y * stretch,
                    local.z * squeeze
                )
            );

        vColor =
            aColor;

        vWorldPos =
            worldPos;

        vMatId =
            matId;

        vParticleLife =
            0.0;

        gl_Position =
            projection
            * view
            * vec4(
                worldPos,
                1.0
            );

        return;
    }

    // ==================================================
    // �������� �������
    //
    // matId:
    //
    // 3.0 + (torchIndex * 10 + particleSlot) / 16384
    //
    // particleSlot:
    // 0 = ������ "������" �������
    // 1 = ������ "������" �������
    //
    // ��������: ������� ���� ��������� 1000 ��� ��������, �� ��� ��������
    // ��������� torchIndex >= 100 ��� (torchIndex*10+slot) >= 1000
    // ��������, ��� ��������� matId ��������� �� ���������� [3.0, 4.0)
    // � ��������� torchId/particleSlot ���������������� �� ������ �����.
    // ��-�� ����� ���������� ������ ��������� ���������� (�������,
    // "�������") � ���������� ������. 16384 ��� ������ ��� MAX_TORCHES = 1024
    // (16384 / 10 = 1638 ��������), ��� ��� �������.
    //
    // ��� ������ ����� ������, ���:
    //
    // lifetime = 0.85 sec
    // spawn interval = 0.50 sec
    //
    // �� ���� � ������ ��������� ����� ������� ������
    // ��� ������ ������������ 0.35 �������.
    // ==================================================

    if (matId > 2.5)
    {
        // --------------------------------------------------
        // ������������� ID ������ � ���� �������.
        // --------------------------------------------------

        const float PARTICLE_ID_SCALE = 16384.0;

        float particleKey =
            floor(
                (matId - 3.0)
                * PARTICLE_ID_SCALE
                + 0.5
            );

        float torchId =
            floor(
                particleKey / 10.0
            );

        float particleSlot =
            mod(
                particleKey,
                10.0
            );

        // --------------------------------------------------
        // ��������� ����� ������.
        // --------------------------------------------------

        const float SPAWN_INTERVAL =
    0.35;

const float PARTICLE_LIFETIME =
    0.85;

const float PARTICLE_CYCLE =
    SPAWN_INTERVAL * 3.0;

// --------------------------------------------------
// ���������� ���� ������� ������.
// --------------------------------------------------

float phaseSeed =
    hash(
        vec2(
            torchId * 17.13 + 5.71,
            torchId * 31.77 + 8.23
        )
    );

float torchPhase =
    phaseSeed
    * PARTICLE_CYCLE;

// --------------------------------------------------
// ������ slot ����������� ����� 0.35 ���.
// --------------------------------------------------

float particleClock =
    uTime
    + torchPhase
    - particleSlot * SPAWN_INTERVAL;

particleClock =
    mod(
        particleClock
        + PARTICLE_CYCLE,
        PARTICLE_CYCLE
    );

float particleTime =
    particleClock;

        // --------------------------------------------------
        // ������ ������ ���� ������ 0.85 �������.
        // ����� spawn interval = 1.0 � lifetime = 0.85
        // ������� 0.15 ���, ����� ���� ��������� ����.
        // --------------------------------------------------

        if (particleTime >= PARTICLE_LIFETIME)
        {
            // ������� ��� ���������� �����.
            // ������� � �� ������� clip space.

            vWorldPos =
                pos;

            vNormal =
                normal;

            vColor =
                aColor;

            vMatId =
                matId;

            vParticleLife =
                1.0;

            gl_PointSize =
                1.0;

            gl_Position =
                vec4(
                    2.0,
                    2.0,
                    2.0,
                    1.0
                );

            return;

        }

        // --------------------------------------------------
        // ��������������� ������� �������.
        // 0 = ������ ���������
        // 1 = ����� ������
        // --------------------------------------------------

        float life =
            particleTime
            /
            PARTICLE_LIFETIME;

        vParticleLife = life;

        // --------------------------------------------------
        // � ������ ������ ���� ��������.
        // ��� �� ��������� ���� �������� ��������
        // ��������� ��������� ���������.
        // --------------------------------------------------

        float particleSeed =
            hash(
                vec2(
                    torchId * 13.17
                        + particleSlot * 7.31,
                    torchId * 29.73
                        + particleSlot * 11.19
                )
            );

        vec3 velocity =
            normal;

        // ��������� �������������� �������� ��������.
        velocity.x +=
            (particleSeed - 0.5)
            * 0.045;

        velocity.z +=
            (
                hash(
                    vec2(
                        particleSeed * 41.3,
                        torchId + particleSlot * 3.7
                    )
                )
                - 0.5
            )
            * 0.035;

        velocity =
            normalize(
                velocity
            );

        // --------------------------------------------------
        // �������� �������.
        // --------------------------------------------------

        vec3 particleOffset =
            velocity
            * particleTime;

        // �������������-�������� ���������.
        // ������� ���������� �����������, �� ��������
        // ������ �����������.
        particleOffset.y +=
            0.18
            * particleTime
            * particleTime;

        // --------------------------------------------------
        // ��������� ��������� ��������.
        // --------------------------------------------------

        particleOffset.x +=
            sin(
                uTime * 8.0
                + torchId * 4.0
                + particleSlot * 5.7
            )
            * 0.018
            * life;

        particleOffset.z +=
            cos(
                uTime * 7.0
                + torchId * 5.0
                + particleSlot * 4.2
            )
            * 0.015
            * life;

        // --------------------------------------------------
        // �������������� ����� ����� ����������.
        // --------------------------------------------------

        particleOffset.x +=
            sin(
                particleTime * 5.0
                + particleSeed * 12.0
            )
            * 0.012
            * life;

        particleOffset.z +=
            cos(
                particleTime * 4.4
                + particleSeed * 17.0
            )
            * 0.010
            * life;

        vec3 worldPos =
            pos
            + particleOffset;

        vWorldPos =
            worldPos;

        vNormal =
            velocity;

        vColor =
            aColor;

        vMatId =
            matId;

        // --------------------------------------------------
        // ������ �������.
        //
        // � ������ ���� �������,
        // � ����� ����� �����������.
        // --------------------------------------------------

        float sizeByLife =
            mix(
                2.20,
                0.85,
                smoothstep(
                    0.0,
                    1.0,
                    life
                )
            );

        // --------------------------------------------------
        // ���������� �� ������.
        // --------------------------------------------------

        // Perf: was `length(worldPos - (inverse(view) * vec4(0,0,0,1)).xyz)`
        // — a full 4x4 matrix inverse computed for every single particle
        // vertex, every frame, just to recover the camera's world position.
        // `camPos` is the exact same value, already uploaded as a uniform
        // for scene.frag's lighting — reused here instead of re-deriving it.
        float distanceToCamera =
            length(
                worldPos
                - camPos
            );

        float distanceScale =
            mix(
                5.0,
                2.0,
                clamp(
                    distanceToCamera / 12.0,
                    0.0,
                    1.0
                )
            );

        gl_PointSize =
            distanceScale
            * sizeByLife;

        gl_Position =
            projection
            * view
            * vec4(
                worldPos,
                1.0
            );

        return;
    }

    // ==================================================
    // ������� ���������
    // ==================================================

    vNormal =
        normal;

    vColor =
        aColor;

    vWorldPos =
        pos;

    vMatId =
        matId;

    vParticleLife =
        0.0;

    gl_Position =
        projection
        * view
        * vec4(
            pos,
            1.0
        );
}
