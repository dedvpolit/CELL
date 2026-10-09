#version 330 core

// Lights the enemy like scene.frag: baked torch shadows, hand torch and fog. Keep the lighting
// constants in sync with scene.frag.

in vec3 vNormal;
in vec3 vColor;
in vec3 vWorldPos;
in vec2 vUV;

out vec4 fragColor;

uniform vec3 camPos;
uniform float uTime;
uniform float renderDistance;

uniform vec3 playerLightPos;
uniform vec3 playerLightDir;
uniform vec3 playerLightColor;
uniform float playerLightIntensity;

const float kTorchLightScale = 0.96;
const float kHandTorchLightScale = 1.31;
const float kTorchFalloff = 0.35;
const float kHandTorchFalloff = 0.23;
const float kTorchRange = 8.0;
const float kAmbient = 0.05;

uniform vec3 devLightPos[8];
uniform vec3 devLightDir[8];
uniform int devLightCount;

// Dev palette (P key): the model has no wall/floor split, so the palette is applied as a tint.
uniform int devPaletteOverride;
const vec3 kDevPaletteWall[5] = vec3[5](
    vec3(0.55, 0.35, 0.25), vec3(0.30, 0.33, 0.38), vec3(0.28, 0.38, 0.24),
    vec3(0.42, 0.40, 0.38), vec3(0.50, 0.28, 0.22));

uniform vec3 torchPos[32];
uniform vec3 torchColor[32];
uniform float torchIntensity[32];
uniform int torchShadowRow[32];
uniform int torchCount;

uniform sampler2D uTorchShadowMap;
const int kShadowAngles = 1024;
const float kShadowSurfaceMargin = 0.05;

uniform sampler2D diffuseTex;
uniform float hasDiffuseTex;

const float PI = 3.14159265;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

bool torchShadowed(vec3 from, int slot)
{
    vec2 d = from.xz - torchPos[slot].xz;
    float r = length(d);
    if (r < 0.15)
        return false;
    float u = (atan(d.y, d.x) * (0.5 / PI) + 0.5) * float(kShadowAngles) - 0.5;
    int a0 = int(floor(u)) & (kShadowAngles - 1);
    int a1 = (a0 + 1) & (kShadowAngles - 1);
    int row = torchShadowRow[slot];
    float occluder = max(texelFetch(uTorchShadowMap, ivec2(a0, row), 0).r,
                         texelFetch(uTorchShadowMap, ivec2(a1, row), 0).r);
    return r - kShadowSurfaceMargin > occluder;
}

void main()
{
    vec3 n = normalize(vNormal);

    vec3 baseColor = mix(vColor, vColor * texture(diffuseTex, vUV).rgb, hasDiffuseTex);
    if (devPaletteOverride >= 0)
        baseColor *= kDevPaletteWall[devPaletteOverride] / vec3(0.55, 0.35, 0.25);

    vec3 lit = baseColor * kAmbient;

    for (int i = 0; i < torchCount; ++i)
    {
        vec3 toLight = torchPos[i] - vWorldPos;
        float lightDist = length(toLight);
        if (lightDist > kTorchRange)
            continue;

        float ndotl = dot(n, toLight / lightDist);
        if (ndotl <= 0.001)
            continue;

        // Seeded by the global torch index: slot order changes as the player moves.
        float seed = float(torchShadowRow[i]) * 17.0;
        float flicker = 0.82 + 0.13 * sin(uTime * 9.0 + seed) + 0.05 * sin(uTime * 23.0 + seed * 1.7);
        float attenuation = torchIntensity[i] * flicker * kTorchLightScale
                          / (1.0 + lightDist * lightDist * kTorchFalloff);
        if (attenuation * ndotl < 0.0008)
            continue;

        float jitter = (hash(vWorldPos.xz * 41.0 + seed) - 0.5) * 0.3;
        if (torchShadowed(vWorldPos + n * 0.04 + vec3(jitter, 0.0, -jitter), i))
            continue;

        lit += baseColor * torchColor[i] * ndotl * attenuation;
    }

    {
        vec3 toFrag = vWorldPos - playerLightPos;
        float pDist = length(toFrag);
        if (pDist < 12.0)
        {
            vec3 toFragDir = toFrag / max(pDist, 0.0001);
            float pNdotl = max(dot(n, -toFragDir), 0.0);
            if (pNdotl > 0.001)
            {
                float facing = clamp(dot(playerLightDir, toFragDir), -1.0, 1.0);
                float forwardBoost = mix(0.85, 1.0, smoothstep(-0.2, 0.9, facing));
                float pFlicker = 0.9 + 0.1 * sin(uTime * 11.0 + 3.7);
                float pAtten = forwardBoost * playerLightIntensity * kHandTorchLightScale * pFlicker
                             / (1.0 + pDist * pDist * kHandTorchFalloff);
                if (pAtten * pNdotl > 0.0008)
                    lit += baseColor * playerLightColor * pNdotl * pAtten;
            }
        }
    }

    for (int i = 0; i < devLightCount; ++i)
    {
        vec3 toFrag = vWorldPos - devLightPos[i];
        float dDist = length(toFrag);
        if (dDist > 40.0)
            continue;
        vec3 toFragDir = toFrag / max(dDist, 0.0001);
        float dNdotl = max(dot(n, -toFragDir), 0.0);
        float cone = smoothstep(0.55, 0.85, clamp(dot(devLightDir[i], toFragDir), -1.0, 1.0));
        float dAtten = cone * 2.0 / (1.0 + dDist * dDist * 0.03);
        if (dAtten * dNdotl > 0.0008)
            lit += baseColor * vec3(0.90, 0.95, 1.0) * dNdotl * dAtten;
    }

    lit *= 1.0 - smoothstep(4.0, renderDistance, length(vWorldPos - camPos));
    fragColor = vec4(clamp(lit, 0.0, 1.0), 1.0);
}
