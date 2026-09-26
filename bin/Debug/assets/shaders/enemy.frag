#version 330 core

// Lights the enemy like the rest of the scene: full shadow test against wall torches, a cheap hand
// light and fog. The shadow code is copied from scene.frag (no #include support in ShaderLoader):
// keep both in sync. Color comes from vertex color (or the optional diffuse texture), not from
// baseColor/detail.

in vec3 vNormal;
in vec3 vColor;
in vec3 vWorldPos;
in vec2 vUV;

out vec4 fragColor;

uniform vec3 camPos;
uniform float uTime;

uniform vec3 playerLightPos;
uniform vec3 playerLightDir;
uniform vec3 playerLightColor;
uniform float playerLightIntensity;

uniform vec3 devLightPos[8];
uniform vec3 devLightDir[8];
uniform int devLightCount;

// Dev tools: cycling environment palettes (P key). The enemy has no wall/floor, so this is a tint
// rather than a swap: the model's own color is multiplied by the ratio "new palette / base
// palette", the same formula as the wall-texture tint in scene.frag, shifting the tone in the same
// direction as the rest of the scene while keeping the model's own shading.
uniform int devPaletteOverride;
const vec3 kDevPaletteWall[5] = vec3[5](
    vec3(0.55, 0.35, 0.25),
    vec3(0.30, 0.33, 0.38),
    vec3(0.28, 0.38, 0.24),
    vec3(0.42, 0.40, 0.38),
    vec3(0.50, 0.28, 0.22)
);

uniform vec3 torchPos[32];
uniform vec3 torchColor[32];
uniform float torchIntensity[32];
uniform int torchCount;

uniform sampler2D mapTex;

#define MAX_COLUMNS 16
uniform vec2 columnPos[MAX_COLUMNS];
uniform int columnCount;
uniform float columnRadius;

uniform float renderDistance;

// The model's diffuse texture. hasDiffuseTex = 0 uses only vColor (the C++ side currently always
// passes 0, see the note in DungeonScene.cpp where hasDiffuseTex is set).
uniform sampler2D diffuseTex;
uniform float hasDiffuseTex;

const vec2 MAP_SIZE = vec2(128.0);

bool isLocalPointSolidGLSL(float lx, float lz, int cut, float chamferSize)
{
    if (cut == 1) return !(lx + lz < chamferSize);
    if (cut == 2) return !((1.0 - lx) + lz < chamferSize);
    if (cut == 3) return !((1.0 - lx) + (1.0 - lz) < chamferSize);
    if (cut == 4) return !(lx + (1.0 - lz) < chamferSize);
    return true;
}

bool shadowedByWall(
    vec3 from,
    vec3 to
)
{
    vec2 rayStart =
        from.xz;

    vec2 delta =
        to.xz
        - rayStart;

    float dist =
        length(delta);

    if (dist < 0.15)
        return false;

    vec2 dir =
        delta / dist;

    // A small margin at the segment's start and end: keeps the ray from self-shadowing its own
    // surface cell and from stumbling on the cell the light source itself sits in.
    float startT =
        min(0.05, dist * 0.5);

    float endT =
        max(
            startT,
            dist - 0.05
        );

    vec2 p0 =
        rayStart + dir * startT;

    vec2 p1 =
        rayStart + dir * endT;

    for (int ci = 0; ci < columnCount; ++ci)
    {
        vec2 seg = p1 - p0;
        float segLenSq = dot(seg, seg);
        float t = segLenSq > 1e-9 ? clamp(dot(columnPos[ci] - p0, seg) / segLenSq, 0.0, 1.0) : 0.0;
        vec2 closest = p0 + seg * t;
        if (length(columnPos[ci] - closest) < columnRadius)
            return true;
    }

    ivec2 mapMax =
        ivec2(MAP_SIZE) - ivec2(1);

    ivec2 cell =
        ivec2(floor(p0));

    ivec2 endCell =
        ivec2(floor(p1));

    // Grid tracing (Amanatides & Woo DDA): instead of sampling the ray at a few equally spaced
    // points (which misses thin walls and gives blocky, ragged shadow edges at low step counts or
    // diagonal angles), walk strictly cell to cell along the ray, so no cell between the surface
    // and the light can be skipped at any distance or angle.

    ivec2 stepDir =
        ivec2(
            dir.x > 0.0 ? 1 : (dir.x < 0.0 ? -1 : 0),
            dir.y > 0.0 ? 1 : (dir.y < 0.0 ? -1 : 0)
        );

    const float BIG =
        1.0e8;

    float tDeltaX =
        (abs(dir.x) > 1e-6) ? abs(1.0 / dir.x) : BIG;

    float tDeltaY =
        (abs(dir.y) > 1e-6) ? abs(1.0 / dir.y) : BIG;

    float nextBoundaryX =
        (stepDir.x > 0) ? float(cell.x + 1) : float(cell.x);

    float nextBoundaryY =
        (stepDir.y > 0) ? float(cell.y + 1) : float(cell.y);

    float tMaxX =
        (abs(dir.x) > 1e-6)
            ? (nextBoundaryX - p0.x) / dir.x
            : BIG;

    float tMaxY =
        (abs(dir.y) > 1e-6)
            ? (nextBoundaryY - p0.y) / dir.y
            : BIG;

    // Enough cells, with margin, for the light's diagonal range (the light is capped at ~8 units,
    // so at most ~23 cells diagonally).
    const int MAX_STEPS = 48;

    // The t parameter for entering the current cell along [p0,p1] (0 = ray start). Updated at the
    // end of each iteration to the cell's exit t; needed only for chamfered corners, to find
    // exactly where inside the cell the ray crosses it.
    float tEnter = 0.0;

    for (
        int i = 0;
        i < MAX_STEPS;
        ++i
    )
    {
        if (
            cell.x < 0 ||
            cell.y < 0 ||
            cell.x > mapMax.x ||
            cell.y > mapMax.y
        )
        {
            return true;
        }

        vec4 cellData =
            texelFetch(
                mapTex,
                cell,
                0
            );

        float tExit = min(tMaxX, tMaxY);

        if (cellData.r > 0.5)
        {
            int cutType = int(round(cellData.g * 255.0));
            if (cutType == 0)
            {
                return true;
            }
            else
            {
                // Chamfered corner: tested with WallShapes' math at the point where the ray crosses
                // the cell (midpoint of [tEnter, tExit]) so the shadow follows the cut. cellData.a
                // is the diagonalChainMask flag. Known limitation: regular chamfers vary on the CPU
                // (0.20..0.55) but this shader uses a fixed 0.35 (scene.frag too).
                float chamferSize = (cellData.a > 0.5) ? 0.92 : 0.35;

                float tMid = clamp((tEnter + min(tExit, dist)) * 0.5, 0.0, dist);
                vec2 hitPoint = rayStart + dir * tMid;
                vec2 localXZ = hitPoint - vec2(cell);
                if (isLocalPointSolidGLSL(localXZ.x, localXZ.y, cutType, chamferSize))
                    return true;
            }
        }

        if (cell == endCell)
            break;

        tEnter = tExit;

        if (tMaxX < tMaxY)
        {
            cell.x += stepDir.x;
            tMaxX += tDeltaX;
        }
        else
        {
            cell.y += stepDir.y;
            tMaxY += tDeltaY;
        }
    }

    return false;
}

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main()
{
    vec3 n = normalize(vNormal);

    vec3 texColor = texture(diffuseTex, vUV).rgb;
    vec3 baseColor = mix(vColor, vColor * texColor, hasDiffuseTex);

    // Dev tools: tint for the current palette, after choosing between plain/textured model coloring
    // and before the lighting below, so all following physics (torches, dev spotlights, fog) works
    // on top of the already shifted tone without other shader changes.
    if (devPaletteOverride >= 0)
    {
        baseColor *= (kDevPaletteWall[devPaletteOverride] / vec3(0.55, 0.35, 0.25));
    }


    float detail = 1.0;

    float ambient = 0.05;
    vec3 lit = baseColor * ambient * detail;

    for (int i = 0; i < 32; i++)
    {
        if (i >= torchCount)
            break;

        float seed = float(i) * 17.0;

        float flicker =
            0.82
            + 0.13 * sin(uTime * 9.0 + seed)
            + 0.05 * sin(uTime * 23.0 + seed * 1.7);

        vec3 toLight = torchPos[i] - vWorldPos;
        float lightDist = length(toLight);

        if (lightDist > 8.0)
            continue;

        float ndotl = max(dot(n, normalize(toLight)), 0.0);
        if (ndotl <= 0.001)
            continue;

        float attenuation =
            (torchIntensity[i] * flicker)
            / (1.0 + lightDist * lightDist * 0.35);

        if (attenuation * ndotl < 0.0008)
            continue;

        float shadowJitter =
            (hash(vWorldPos.xz * 41.0 + float(i) * 17.0) - 0.5) * 0.3;
        vec3 shadowRayFrom =
            vWorldPos
            + n * 0.04
            + vec3(shadowJitter, 0.0, -shadowJitter);

        if (shadowedByWall(shadowRayFrom, torchPos[i]))
            continue;

        lit += baseColor * torchColor[i] * ndotl * attenuation * detail;
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

                float pFalloff = 1.0 / (1.0 + pDist * pDist * 0.13);
                float pFlicker = 0.9 + 0.1 * sin(uTime * 11.0 + 3.7);

                float pAtten = pFalloff * forwardBoost * playerLightIntensity * pFlicker;

                if (pAtten * pNdotl > 0.0008)
                {
                    lit += baseColor * playerLightColor * pNdotl * pAtten * detail;
                }
            }
        }
    }

    for (int i = 0; i < 8; i++)
    {
        if (i >= devLightCount)
            break;

        vec3 toFrag = vWorldPos - devLightPos[i];
        float dDist = length(toFrag);
        if (dDist > 40.0)
            continue;

        vec3 toFragDir = toFrag / max(dDist, 0.0001);
        float dNdotl = max(dot(n, -toFragDir), 0.0);
        if (dNdotl <= 0.001)
            continue;

        float facing = clamp(dot(devLightDir[i], toFragDir), -1.0, 1.0);
        float cone = smoothstep(0.55, 0.85, facing);
        if (cone <= 0.001)
            continue;

        float dFalloff = 1.0 / (1.0 + dDist * dDist * 0.03);
        float dAtten = dFalloff * cone * 2.0;

        if (dAtten * dNdotl > 0.0008)
        {
            lit += baseColor * vec3(0.90, 0.95, 1.0) * dNdotl * dAtten * detail;
        }
    }

    float dist = length(vWorldPos - camPos);
    float fogNear = 4.0;
    float fogFar = renderDistance;
    float fogVisibility = 1.0 - smoothstep(fogNear, fogFar, dist);

    lit *= fogVisibility;
    lit = clamp(lit, 0.0, 1.0);

    fragColor = vec4(lit, 1.0);
}
