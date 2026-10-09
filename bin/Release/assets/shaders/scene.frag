#version 330 core

in vec3 vNormal;
in vec3 vColor;
in vec3 vWorldPos;
in float vMatId;
in float vParticleLife;

out vec4 fragColor;

uniform vec3 camPos;
uniform float uTime;
uniform float renderDistance;

// Depth pre-pass: only the discard decisions run, no shading.
uniform bool uDepthOnly;

// Alpha of lit geometry; below 1 only for the donut fading in, drawn with blending.
uniform float uAlpha;

// Dev tools, P key: -1 = off, 0..4 = Zoning palette preset.
uniform int devPaletteOverride;
const vec3 kDevPaletteWall[5] = vec3[5](
    vec3(0.55, 0.35, 0.25), vec3(0.30, 0.33, 0.38), vec3(0.28, 0.38, 0.24),
    vec3(0.42, 0.40, 0.38), vec3(0.50, 0.28, 0.22));
const vec3 kDevPaletteFloor[5] = vec3[5](
    vec3(0.18, 0.16, 0.13), vec3(0.10, 0.11, 0.14), vec3(0.10, 0.14, 0.10),
    vec3(0.15, 0.14, 0.13), vec3(0.17, 0.12, 0.10));

// Hand torch: moves every frame, so it has no shadow test.
uniform vec3 playerLightPos;
uniform vec3 playerLightDir;
uniform vec3 playerLightColor;
uniform float playerLightIntensity;

// Keep in sync with enemy.frag. light = scale * intensity / (1 + d^2 * falloff).
// kHandTorchFalloff >= kHandTorchLightScale * kTorchFalloff / (2.1 * kTorchLightScale) keeps the
// hand torch no brighter than the weakest wall torch (intensity 2.1) at any distance.
const float kTorchLightScale = 0.96;
const float kHandTorchLightScale = 1.31;
const float kTorchFalloff = 0.35;
const float kHandTorchFalloff = 0.23;
const float kTorchRange = 8.0;
const float kAmbient = 0.05;

// Dev tools, L key: static spotlights.
uniform vec3 devLightPos[8];
uniform vec3 devLightDir[8];
uniform int devLightCount;

uniform vec3 torchPos[32];
uniform vec3 torchColor[32];
uniform float torchIntensity[32];
uniform int torchShadowRow[32]; // row of each active torch in uTorchShadowMap
uniform int torchCount;

// TorchShadowMap: distance from the flame to the first wall/column per direction.
uniform sampler2D uTorchShadowMap;
const int kShadowAngles = 1024;       // TorchShadowMap::kAngles
const float kShadowSurfaceMargin = 0.05;

// One texel per torch, 1 = lit, 0 = picked up. The flame is baked into the chunk mesh, so this is
// the only way to hide it.
uniform sampler2D uTorchLitMask;
const float kTorchLitMaskSize = 32.0; // DungeonScene::kTorchLitMaskDim

// The hand torch viewmodel reuses the flame code with a torch id outside uTorchLitMask.
uniform bool uIsViewmodelDraw;
uniform float uViewmodelTorchFuel;
const float kFuelShrinkStartsBelow = 0.3; // same as scene.vert
const float kMinVisibleFlameFuel = 0.22;

// Enemies are dynamic, so they are tested analytically as vertical cylinders.
#define MAX_ENEMY_OCCLUDERS 8
uniform vec2 enemyOccluderPosXZ[MAX_ENEMY_OCCLUDERS];
uniform int enemyOccluderCount;
uniform float enemyOccluderRadius;
uniform float enemyOccluderHeight;

// wallTexEnabled = 0 falls back to procedural bricks.
uniform sampler2D wallTex;
uniform float wallTexEnabled;
uniform float wallTexContrast;
const vec2 WALL_TEX_TILE = vec2(0.5);


const float PI = 3.14159265;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float fbmNoise(vec2 p)
{
    return hash(floor(p * 3.0)) * 0.5 + hash(floor(p * 11.0)) * 0.3 + hash(floor(p * 37.0)) * 0.2;
}

float torchLitAt(float torchId)
{
    ivec2 texel = ivec2(int(mod(torchId, kTorchLitMaskSize)), int(torchId / kTorchLitMaskSize));
    return texelFetch(uTorchLitMask, texel, 0).r;
}

float viewmodelFuelBrightness()
{
    return mix(kMinVisibleFlameFuel, 1.0, clamp(uViewmodelTorchFuel / kFuelShrinkStartsBelow, 0.0, 1.0));
}

bool enemyOccludes(vec3 from, vec3 to)
{
    vec2 delta = to.xz - from.xz;
    float dist = length(delta);
    vec2 dir = delta / dist;
    // Margins keep the segment off the lit surface and off the light itself.
    float startT = min(0.05, dist * 0.5);
    float endT = max(startT, dist - 0.05);
    vec2 p0 = from.xz + dir * startT;
    vec2 seg = dir * (endT - startT);
    float segLenSq = dot(seg, seg);

    for (int i = 0; i < enemyOccluderCount; ++i)
    {
        vec2 c = enemyOccluderPosXZ[i];
        float t = segLenSq > 1e-9 ? clamp(dot(c - p0, seg) / segLenSq, 0.0, 1.0) : 0.0;
        if (length(c - (p0 + seg * t)) < enemyOccluderRadius)
        {
            float heightFrac = clamp((startT + t * (endT - startT)) / dist, 0.0, 1.0);
            if (mix(from.y, to.y, heightFrac) < enemyOccluderHeight)
                return true;
        }
    }
    return false;
}

bool torchShadowed(vec3 from, int slot)
{
    vec2 d = from.xz - torchPos[slot].xz;
    float r = length(d);
    if (r < 0.15)
        return false;

    // The larger of the two nearest directions: no acne on walls seen at grazing angles, at the
    // cost of shadow edges shifting by at most one texel (~5 cm at the light's full range).
    float u = (atan(d.y, d.x) * (0.5 / PI) + 0.5) * float(kShadowAngles) - 0.5;
    int a0 = int(floor(u)) & (kShadowAngles - 1);
    int a1 = (a0 + 1) & (kShadowAngles - 1);
    int row = torchShadowRow[slot];
    float occluder = max(texelFetch(uTorchShadowMap, ivec2(a0, row), 0).r,
                         texelFetch(uTorchShadowMap, ivec2(a1, row), 0).r);
    if (r - kShadowSurfaceMargin > occluder)
        return true;

    return enemyOccluderCount > 0 && enemyOccludes(from, torchPos[slot]);
}

vec3 wallDetail(vec2 uv, vec3 texColor, vec3 baseColor, out float detail)
{
    float mossNoise = hash(floor(uv * 10.0));
    float moss = step(0.55, mossNoise) * clamp(1.0 - uv.y * 1.3, 0.0, 1.0);

    if (wallTexEnabled > 0.5)
    {
        // Contrast stretch so the dim scene still spans enough of the ASCII ramp.
        texColor = clamp((texColor - 0.5) * wallTexContrast + 0.5, 0.0, 1.0);
        // Tint by the zone color relative to the base palette the texture was authored for.
        baseColor = texColor * (baseColor / vec3(0.55, 0.35, 0.25));
        detail = 1.0;
    }
    else
    {
        vec2 scaled = uv * vec2(4.0, 6.0);
        scaled.x += mod(floor(scaled.y), 2.0) * 0.5;
        vec2 b = fract(scaled);
        const float mortarW = 0.06;
        float mortar = clamp(step(b.x, mortarW) + step(b.y, mortarW * 1.5), 0.0, 1.0);
        vec3 brickColor = baseColor * (hash(floor(scaled) + 3.0) * 0.6 + 0.35);
        baseColor = mix(brickColor, brickColor * 0.35, mortar);
        detail = 1.0 - mortar * 0.5;
    }

    return mix(baseColor, baseColor * vec3(0.35, 0.45, 0.30), moss * 0.8);
}

vec3 floorDetail(vec2 uv, vec3 baseColor, out float detail)
{
    vec2 tileUv = uv * 3.0;
    vec2 tileId = floor(tileUv);
    vec2 f = fract(tileUv);
    const float groutW = 0.02;
    float grout = clamp(step(f.x, groutW) + step(1.0 - groutW, f.x)
                      + step(f.y, groutW) + step(1.0 - groutW, f.y), 0.0, 1.0);

    baseColor *= hash(tileId + 50.0) * 0.30 + 0.75;
    baseColor = mix(baseColor, baseColor * 0.25, grout);
    float moss = step(0.45, hash(tileId + 500.0));
    baseColor = mix(baseColor, baseColor * vec3(0.30, 0.42, 0.28), moss * 0.7);
    baseColor *= hash(floor(uv * 25.0) + 200.0) * 0.15 + 0.92;

    detail = 1.0 - grout * 0.6;
    return baseColor;
}

void main()
{
    float cameraDistance = length(vWorldPos - camPos);
    if (cameraDistance > renderDistance)
        discard;

    float fadeStart = renderDistance - 5.0;
    float distanceFade = 1.0 - smoothstep(fadeStart, renderDistance, cameraDistance);

    // Spark particles: matId = 3 + (torchIndex * 10 + slot) / 16384. Blended pass only.
    if (vMatId > 2.5)
    {
        float torchLit = 1.0;
        float fuel = 1.0;
        if (uIsViewmodelDraw)
            fuel = viewmodelFuelBrightness();
        else
            torchLit = torchLitAt(floor(floor((vMatId - 3.0) * 16384.0 + 0.5) / 10.0));

        float pointDistance = length(gl_PointCoord - 0.5);
        if (pointDistance > 0.5)
            discard;

        float alpha = 1.0 - smoothstep(0.15, 0.5, pointDistance);
        float lifeFade = 1.0 - smoothstep(0.58, 1.0, vParticleLife);
        vec3 color = mix(vec3(1.0, 0.25, 0.025), vec3(1.0, 0.88, 0.32), 1.0 - vParticleLife);
        fragColor = vec4(color, alpha * lifeFade * distanceFade * torchLit * fuel);
        return;
    }

    // Flame (matId = 2 + id / 4096) and handle (matId = 1 + id / 4096) of a wall torch. Alpha 0
    // tells the ASCII pass to skip edge detection on their flat-shaded faces.
    if (vMatId > 0.5)
    {
        bool isFlame = vMatId > 1.5;
        float torchId = floor((vMatId - (isFlame ? 2.0 : 1.0)) * 4096.0 + 0.5);
        if (!uIsViewmodelDraw && torchLitAt(torchId) < 0.5)
            discard;
        if (uDepthOnly)
        {
            fragColor = vec4(0.0);
            return;
        }

        vec3 color;
        if (isFlame)
        {
            float seed = hash(vec2(torchId + 4.0, torchId * 7.1));
            float flicker = 1.12 + 0.15 * sin(uTime * 13.0 + seed * 17.0)
                                 + 0.08 * sin(uTime * 23.0 + seed * 31.0)
                                 + 0.045 * sin(uTime * 37.0 + seed * 7.0);
            float verticalFire = clamp(vNormal.y * 0.5 + 0.5, 0.0, 1.0);

            // Guide torches mark themselves with vColor = (x, 0, 1); regular flames have g = 0.6.
            bool isGuideFlame = vColor.g < 0.01 && vColor.b > 0.5;
            vec3 lower = isGuideFlame ? vec3(0.30, 0.55, 1.0) : vec3(1.0, 0.82, 0.20);
            vec3 upper = isGuideFlame ? vec3(0.05, 0.15, 0.55) : vec3(1.0, 0.30, 0.035);

            float fuel = uIsViewmodelDraw ? viewmodelFuelBrightness() : 1.0;
            color = mix(lower, upper, verticalFire) * flicker * vColor.r * fuel;
        }
        else
        {
            color = vColor * 1.15;
        }

        fragColor = vec4(clamp(color * distanceFade, 0.0, 1.0), uIsViewmodelDraw ? 1.0 : 0.0);
        return;
    }

    if (uDepthOnly)
    {
        fragColor = vec4(0.0);
        return;
    }

    vec3 n = normalize(vNormal);
    bool isWall = abs(n.y) < 0.5;
    vec2 uv = !isWall ? vWorldPos.xz : (abs(n.x) > 0.5 ? vWorldPos.zy : vWorldPos.xy);

    // Sampled before any branch on the surface type, so the mip level comes from well-defined
    // derivatives.
    vec3 wallTexColor = texture(wallTex, uv * WALL_TEX_TILE).rgb;

    vec3 baseColor = vColor;
    if (devPaletteOverride >= 0)
        baseColor = isWall ? kDevPaletteWall[devPaletteOverride] : kDevPaletteFloor[devPaletteOverride];

    // The exit door and the donut (matId 0.3..0.4) are untextured.
    float detail = 1.0;
    if (!(vMatId > 0.3 && vMatId < 0.4))
        baseColor = isWall ? wallDetail(uv, wallTexColor, baseColor, detail) : floorDetail(uv, baseColor, detail);

    baseColor *= fbmNoise(uv * 2.0 + vColor.xy * 0.01) * 0.30 + 0.85;
    vec3 surface = baseColor * detail;
    vec3 lit = surface * kAmbient;

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

        // A single shadow ray gives a razor edge; jittering its origin in world space turns it
        // into a narrow dithered band.
        float jitter = (hash(vWorldPos.xz * 41.0 + seed) - 0.5) * 0.3;
        vec3 shadowFrom = vWorldPos + n * 0.04 + vec3(jitter, 0.0, -jitter);
        if (torchShadowed(shadowFrom, i))
            continue;

        lit += surface * torchColor[i] * ndotl * attenuation;
    }

    // Hand torch: nearly omnidirectional with a slight forward accent.
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
                    lit += surface * playerLightColor * pNdotl * pAtten;
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
            lit += surface * vec3(0.90, 0.95, 1.0) * dNdotl * dAtten;
    }

    lit *= 1.0 - smoothstep(4.0, renderDistance, cameraDistance);

    fragColor = vec4(clamp(lit, 0.0, 1.0), uAlpha);
}
