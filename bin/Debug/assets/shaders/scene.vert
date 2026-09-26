#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aMatId;

uniform mat4 view;
uniform mat4 projection;
uniform float uTime;
// camPos: the same camera-position uniform already uploaded for scene.frag (see
// DungeonScene::cacheUniformLocations()/render()), reused here so the particle branch does not need
// a per-vertex 4x4 matrix inverse to recover the camera position.
uniform vec3 camPos;

// Hand-torch viewmodel (PlayerTorchViewmodel.h): true only while drawing it. Its geometry is fixed
// camera-space offsets, so world position/normal are reconstructed through the inverse view matrix
// (always invertible, unlike the cross()-based basis that produced NaN at extreme angles); it
// therefore follows yaw, pitch and bob for free.
uniform bool uIsViewmodelDraw;
// Computed once on the CPU: inverse(view) is the same for every vertex of this model (~250), so
// computing it per vertex would be wasted GPU work.
uniform mat4 uInvView;
uniform vec2 uViewmodelSway;

// The player torch's fuel (same meaning and 0..1 range as uViewmodelTorchFuel in scene.frag), used
// here to physically shrink the flame geometry as fuel depletes, not only to dim its color in the
// fragment stage.
uniform float uViewmodelTorchFuel;

// "Unreliable vision": one specific torch's flame trembles. torchId == uGlitchTorchIndex && uTime <
// uGlitchTorchUntilTime triggers a sharp random jolt of the flame geometry on top of the regular
// flicker. -1 = no glitch active (nothing is checked).
uniform float uGlitchTorchIndex;
uniform float uGlitchTorchUntilTime;

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
    vec3 pos = aPos;
    vec3 normal = aNormal;
    float matId = aMatId;

    if (uIsViewmodelDraw)
    {
        // Slight sway (turning inertia plus idle breathing) from uViewmodelSway, computed on the
        // CPU with scalar math and applied to camera-space coordinates before the world
        // reconstruction, so nothing here can degenerate.
        vec3 swayedPos = aPos;
        swayedPos.x += uViewmodelSway.x;
        swayedPos.y += uViewmodelSway.y;

        pos = (uInvView * vec4(swayedPos, 1.0)).xyz;
        normal = mat3(uInvView) * aNormal;
        // The reserved +10.0 offset is subtracted here: for the rest of the function matId is back
        // in the same ranges as the rest of the scene (1.5..2.5 flame, regular geometry), so no
        // separate material branch is needed.
        matId -= 10.0;
    }

    if (matId > 1.5 && matId < 2.5)
    {
        const float FLAME_RADIUS = 0.10;

        vec3 local =
            normal;

        vec3 center =
            pos
            - normal * FLAME_RADIUS;

        // Torch ID scale: the index is encoded as matId = 2.0 + torchIndex / 4096, which leaves
        // room for MAX_TORCHES = 1024 with margin (the range stays below 2.5).
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

        float upper =
            smoothstep(
                0.0,
                0.85,
                max(local.y, 0.0)
            );

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

        // Fuel-based shrink, viewmodel only (wall torches always 1.0): full size until 70% of the
        // fuel is spent, then shrinking (kFuelShrinkStartsBelow) but never to 0
        // (kMinVisibleFlameFuel, matched with scene.frag so geometry and brightness shrink
        // together).
        if (uIsViewmodelDraw)
        {
            const float kFuelShrinkStartsBelow = 0.3;
            const float kMinVisibleFlameFuel = 0.22;
            float shrinkT =
                clamp(
                    uViewmodelTorchFuel / kFuelShrinkStartsBelow,
                    0.0,
                    1.0
                );
            float fuelScale =
                mix(
                    kMinVisibleFlameFuel,
                    1.0,
                    shrinkT
                );
            animatedLocal *= fuelScale;
        }

        // "Unreliable vision": a sharp geometry jitter for one specific torch, applied on top of
        // the regular flicker/shrink rather than replacing it, so it reads as something being wrong
        // with this flame, not just a different animation.
        if (torchId == uGlitchTorchIndex && uTime < uGlitchTorchUntilTime)
        {
            float jseed = hash(vec2(torchId + 91.0, floor(uTime * 20.0)));
            vec3 jitter = vec3(
                jseed - 0.5,
                hash(vec2(jseed, torchId)) - 0.5,
                hash(vec2(torchId, jseed)) - 0.5
            );
            animatedLocal += jitter * 0.09;
        }

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

    // Spark particles (GL_POINTS): matId = 3.0 + (torchIndex * 10 + particleSlot) /
    // PARTICLE_ID_SCALE with slot 0..2 (three sparks per torch at staggered phases). The scale
    // keeps the range in [3.0, 4.0) and exactly decodable in float (16384 fits 1024 torches). A
    // spark lives PARTICLE_LIFETIME = 0.85 s of a 1.05 s cycle.

    if (matId > 2.5)
    {
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

        const float SPAWN_INTERVAL =
    0.35;

const float PARTICLE_LIFETIME =
    0.85;

const float PARTICLE_CYCLE =
    SPAWN_INTERVAL * 3.0;

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

        // Outside its lifetime the spark is invisible: the 0.20 s left in the cycle (1.05 - 0.85)
        // are a pause before the next spark of this slot. It is discarded by moving the vertex
        // outside clip space.

        if (particleTime >= PARTICLE_LIFETIME)
        {
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

        float life =
            particleTime
            /
            PARTICLE_LIFETIME;

        vParticleLife = life;

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

        vec3 particleOffset =
            velocity
            * particleTime;

        particleOffset.y +=
            0.18
            * particleTime
            * particleTime;

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

        // Perf: camPos (already uploaded for scene.frag's lighting) is used instead of extracting
        // the camera position from inverse(view), which would be a full 4x4 matrix inverse for
        // every particle vertex, every frame.
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
