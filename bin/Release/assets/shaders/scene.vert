#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aMatId;

uniform mat4 view;
uniform mat4 projection;
uniform float uTime;
uniform vec3 camPos;

// Hand torch viewmodel: geometry is in camera space and goes to world space through the inverse
// view matrix, so it follows yaw, pitch and bob. matId is offset by +10 for this draw.
uniform bool uIsViewmodelDraw;
uniform mat4 uInvView;
uniform vec2 uViewmodelSway;
uniform float uViewmodelTorchFuel; // 0..1, shrinks the flame; see scene.frag

// "Unreliable vision": the flame of torch uGlitchTorchIndex jitters until uGlitchTorchUntilTime.
uniform float uGlitchTorchIndex;
uniform float uGlitchTorchUntilTime;

out vec3 vNormal;
out vec3 vColor;
out vec3 vWorldPos;
out float vMatId;
out float vParticleLife;

// The depth pre-pass and the shading pass must produce bit-identical depth.
invariant gl_Position;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void emit(vec3 worldPos, vec3 normal, float matId, float life)
{
    vWorldPos = worldPos;
    vNormal = normal;
    vColor = aColor;
    vMatId = matId;
    vParticleLife = life;
    gl_Position = projection * view * vec4(worldPos, 1.0);
}

void main()
{
    vec3 pos = aPos;
    vec3 normal = aNormal;
    float matId = aMatId;

    if (uIsViewmodelDraw)
    {
        vec3 swayed = aPos + vec3(uViewmodelSway, 0.0);
        pos = (uInvView * vec4(swayed, 1.0)).xyz;
        normal = mat3(uInvView) * aNormal;
        matId -= 10.0;
    }

    // Flame: matId = 2 + torchIndex / 4096. The vertex normal is the direction from the flame's
    // center, so the flame is re-shaped procedurally around it.
    if (matId > 1.5 && matId < 2.5)
    {
        const float FLAME_RADIUS = 0.10;
        vec3 local = normal;
        vec3 center = pos - normal * FLAME_RADIUS;

        float torchId = floor((matId - 2.0) * 4096.0 + 0.5);
        float seed = hash(vec2(torchId + 17.31, torchId * 3.71 + 9.17));
        float time = uTime * (5.5 + seed * 1.5);
        float upper = smoothstep(0.0, 0.85, max(local.y, 0.0));

        float stretch = 1.0 + upper * (0.16 + 0.07 * sin(time * 1.37 + seed * 8.0 + local.x * 5.0));
        float squeeze = 1.0 - upper * 0.16 + 0.045 * sin(time * 1.73 + local.y * 8.0 + seed * 11.0);
        float swayX = (sin(uTime * 7.0 + seed * 14.0) * 0.026 + sin(uTime * 12.0 + seed * 5.0) * 0.011) * upper * upper;
        float swayZ = (cos(uTime * 6.3 + seed * 9.0) * 0.020 + cos(uTime * 10.7 + seed * 17.0) * 0.009) * upper * upper;
        float wobble = sin(time * 2.3 + local.x * 9.0 + local.z * 7.0) * 0.012 * upper;

        vec3 animated = vec3(local.x * squeeze, local.y * stretch, local.z * squeeze) * FLAME_RADIUS;
        animated.x += wobble + swayX;
        animated.z += swayZ;
        animated.y += upper * 0.012 * sin(uTime * 8.0 + seed * 10.0);

        if (uIsViewmodelDraw)
        {
            const float kFuelShrinkStartsBelow = 0.3;
            const float kMinVisibleFlameFuel = 0.22;
            animated *= mix(kMinVisibleFlameFuel, 1.0, clamp(uViewmodelTorchFuel / kFuelShrinkStartsBelow, 0.0, 1.0));
        }

        if (torchId == uGlitchTorchIndex && uTime < uGlitchTorchUntilTime)
        {
            float jseed = hash(vec2(torchId + 91.0, floor(uTime * 20.0)));
            vec3 jitter = vec3(jseed - 0.5, hash(vec2(jseed, torchId)) - 0.5, hash(vec2(torchId, jseed)) - 0.5);
            animated += jitter * 0.09;
        }

        emit(center + animated, normalize(vec3(local.x * squeeze, local.y * stretch, local.z * squeeze)), matId, 0.0);
        return;
    }

    // Sparks (GL_POINTS): matId = 3 + (torchIndex * 10 + slot) / 16384, three slots per torch.
    // Each spark lives 0.85 s of a 1.05 s cycle.
    if (matId > 2.5)
    {
        float particleKey = floor((matId - 3.0) * 16384.0 + 0.5);
        float torchId = floor(particleKey / 10.0);
        float slot = mod(particleKey, 10.0);

        const float SPAWN_INTERVAL = 0.35;
        const float LIFETIME = 0.85;
        const float CYCLE = SPAWN_INTERVAL * 3.0;
        float torchPhase = hash(vec2(torchId * 17.13 + 5.71, torchId * 31.77 + 8.23)) * CYCLE;
        float t = mod(uTime + torchPhase - slot * SPAWN_INTERVAL + CYCLE, CYCLE);

        if (t >= LIFETIME)
        {
            // Between sparks: move the vertex out of clip space.
            vWorldPos = pos;
            vNormal = normal;
            vColor = aColor;
            vMatId = matId;
            vParticleLife = 1.0;
            gl_PointSize = 1.0;
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }

        float life = t / LIFETIME;
        float pseed = hash(vec2(torchId * 13.17 + slot * 7.31, torchId * 29.73 + slot * 11.19));
        vec3 velocity = normal;
        velocity.x += (pseed - 0.5) * 0.045;
        velocity.z += (hash(vec2(pseed * 41.3, torchId + slot * 3.7)) - 0.5) * 0.035;
        velocity = normalize(velocity);

        vec3 offset = velocity * t;
        offset.y += 0.18 * t * t;
        offset.x += sin(uTime * 8.0 + torchId * 4.0 + slot * 5.7) * 0.018 * life
                  + sin(t * 5.0 + pseed * 12.0) * 0.012 * life;
        offset.z += cos(uTime * 7.0 + torchId * 5.0 + slot * 4.2) * 0.015 * life
                  + cos(t * 4.4 + pseed * 17.0) * 0.010 * life;

        vec3 worldPos = pos + offset;
        float sizeByLife = mix(2.20, 0.85, smoothstep(0.0, 1.0, life));
        float distanceScale = mix(5.0, 2.0, clamp(length(worldPos - camPos) / 12.0, 0.0, 1.0));
        gl_PointSize = distanceScale * sizeByLife;
        emit(worldPos, velocity, matId, life);
        return;
    }

    emit(pos, normal, matId, 0.0);
}
