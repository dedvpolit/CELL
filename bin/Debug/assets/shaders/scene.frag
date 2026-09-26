#version 330 core

in vec3 vNormal;
in vec3 vColor;
in vec3 vWorldPos;
in float vMatId;
in float vParticleLife;

out vec4 fragColor;

uniform vec3 camPos;
uniform float uTime;

// Dev tools: forced environment palette (P key). -1 = off (regular baked zone colors); 0..4 selects
// one of the Zoning presets, duplicated here because zone colors are baked into vertex colors, not
// looked up in the shader.
uniform int devPaletteOverride;
const vec3 kDevPaletteWall[5] = vec3[5](
    vec3(0.55, 0.35, 0.25),
    vec3(0.30, 0.33, 0.38),
    vec3(0.28, 0.38, 0.24),
    vec3(0.42, 0.40, 0.38),
    vec3(0.50, 0.28, 0.22)
);
const vec3 kDevPaletteFloor[5] = vec3[5](
    vec3(0.18, 0.16, 0.13),
    vec3(0.10, 0.11, 0.14),
    vec3(0.10, 0.14, 0.10),
    vec3(0.15, 0.14, 0.13),
    vec3(0.17, 0.12, 0.10)
);

// Hand torch: a cheap light with no shadow test. Wall torches are static and justify the full
// raymarch; this one moves every frame with the camera, so a raymarch per pixel costs more than it
// is worth for a light at the player's feet.
uniform vec3 playerLightPos;
uniform vec3 playerLightDir;
uniform vec3 playerLightColor;
uniform float playerLightIntensity;

// Dev tools: static spotlights (L key): a narrow cone frozen at the direction they were created
// with, no shadow test.
uniform vec3 devLightPos[8];
uniform vec3 devLightDir[8];
uniform int devLightCount;

uniform vec3 torchPos[32];
uniform vec3 torchColor[32];
uniform float torchIntensity[32];
uniform int torchCount;

uniform sampler2D mapTex;

// Mask of extinguished wall torches (pickupWallTorch()). The flame is baked into the chunk mesh and
// ignores torchIntensity, so a 32x32 lookup texture (one texel per torch, 1.0 = lit) is updated
// with a single glTexSubImage2D on pickup.
uniform sampler2D uTorchLitMask;

const float kTorchLitMaskSize = 32.0; // must match DungeonScene::kTorchLitMaskDim

// Hand-torch viewmodel draw (same uniform as in scene.vert). It reuses the flame/particle code with
// a reserved torchIndex = 1536, outside uTorchLitMask (32x32): texelFetch() out of range is
// undefined, so the torchLit check must be skipped for it.
uniform bool uIsViewmodelDraw;

// Fuel of the player's current torch, 0..1: used only when uIsViewmodelDraw is true, to shrink and
// dim the flame smoothly as fuel depletes (see the flame block below); it does not affect wall
// torches.
uniform float uViewmodelTorchFuel;

// Columns: for the ray-vs-circle test in shadowedByWall() below. A column cell in mapTex is already
// floor, so the grid test does not see it, and without a separate check a column would not cast a
// shadow although it blocks light. MAX_COLUMNS = 16: columns are rare (usually 0-2 per map).
#define MAX_COLUMNS 16
uniform vec2 columnPos[MAX_COLUMNS];
uniform int columnCount;
uniform float columnRadius; // see Columns::kColumnRadius (C++)

// Enemy shadows (see shadowedByWall() below): the same scheme as the columns above, a small uniform
// array checked with an exact analytical test inside shadowedByWall(), with no separate texture or
// render pass.
#define MAX_ENEMY_OCCLUDERS 8
uniform vec2 enemyOccluderPosXZ[MAX_ENEMY_OCCLUDERS];
uniform int enemyOccluderCount;
uniform float enemyOccluderRadius;
uniform float enemyOccluderHeight;

// Wall texture: wallTexEnabled = 0.0 if the file was not found or failed to load at startup; walls
// then fall back to the procedural "brick" look, so a missing file does not turn into black or
// broken walls.
uniform sampler2D wallTex;
uniform float wallTexEnabled;
uniform float wallTexContrast;

// Texture repeats per world unit: texUV = uv * WALL_TEX_TILE. Tuned for the 256x256 Torment
// Textures pack.
const vec2 WALL_TEX_TILE = vec2(0.5, 0.5);

// Draw/fog distance: 16.0 for a regular player, can be increased from the CPU for cinematic noclip
// shots (a uniform, so no shader recompilation is needed).
uniform float renderDistance;

// Alpha for the win monument's pedestal (a narrow matId range). The opaque pass hardcodes alpha =
// 1.0, so this object is drawn in a separate call with GL_BLEND.
uniform float uMonumentFadeAlpha;

const vec2 MAP_SIZE =
    vec2(128.0);

// Mirrors WallShapes::IsLocalPointSolid (CPU): keep in sync if the chamfer formula changes there.
// cut: 0 = None, 1 = SW, 2 = SE, 3 = NE, 4 = NW (matches WallShapes::CornerCut).
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

    // Columns: an exact ray-vs-circle test over the whole [p0,p1] segment, independent of the
    // mapTex grid (the same idea as the player's circle-vs-square collision in
    // PlayerController.cpp: a column is not described by the grid, so its test is separate too).
    for (int ci = 0; ci < columnCount; ++ci)
    {
        vec2 seg = p1 - p0;
        float segLenSq = dot(seg, seg);
        float t = segLenSq > 1e-9 ? clamp(dot(columnPos[ci] - p0, seg) / segLenSq, 0.0, 1.0) : 0.0;
        vec2 closest = p0 + seg * t;
        if (length(columnPos[ci] - closest) < columnRadius)
            return true;
    }

    // Enemies: the same exact ray-vs-circle test as columns plus a height check (an enemy is not an
    // infinitely tall pillar). Unlike a shadow map it cannot miss the thin silhouette and works on
    // walls too.
    for (int ei = 0; ei < enemyOccluderCount; ++ei)
    {
        vec2 seg = p1 - p0;
        float segLenSq = dot(seg, seg);
        float t = segLenSq > 1e-9 ? clamp(dot(enemyOccluderPosXZ[ei] - p0, seg) / segLenSq, 0.0, 1.0) : 0.0;
        vec2 closest = p0 + seg * t;
        if (length(enemyOccluderPosXZ[ei] - closest) < enemyOccluderRadius)
        {
            // t above is a parameter along the trimmed [p0,p1], not the full [rayStart,to]: it is
            // converted back to a fraction of the full ray length so the height is taken at the
            // right point between from.y and to.y, not one shifted by the self-shadowing margins.
            float distAlongRay = startT + t * (endT - startT);
            float heightFrac = clamp(distAlongRay / dist, 0.0, 1.0);
            float rayY = mix(from.y, to.y, heightFrac);
            if (rayY < enemyOccluderHeight)
                return true;
        }
    }

    ivec2 mapMax =
        ivec2(MAP_SIZE) - ivec2(1);

    ivec2 cell =
        ivec2(floor(p0));

    ivec2 endCell =
        ivec2(floor(p1));

    // Grid tracing (Amanatides & Woo DDA): walks strictly cell to cell so no cell between the
    // surface and the light can be skipped, unlike sampling the ray at fixed steps.

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
                // Chamfered corner: the ray is tested with WallShapes' math at the point where it
                // crosses the cell (midpoint of [tEnter, tExit]), so the shadow follows the cut.
                // cellData.a is the diagonalChainMask flag. Known limitation: regular chamfers vary
                // on the CPU (0.20..0.55) but the shader uses a fixed 0.35 (enemy.frag too).
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
    return fract(
        sin(
            dot(
                p,
                vec2(
                    127.1,
                    311.7
                )
            )
        )
        * 43758.5453123
    );
}

float fbmNoise(vec2 p)
{
    float n = 0.0;

    n +=
        hash(
            floor(
                p * 3.0
            )
        )
        * 0.5;

    n +=
        hash(
            floor(
                p * 11.0
            )
        )
        * 0.3;

    n +=
        hash(
            floor(
                p * 37.0
            )
        )
        * 0.2;

    return n;
}

void main()
{
    float cameraDistance =
        length(
            vWorldPos
            - camPos
        );

    // The fog range comes from the renderDistance uniform (16.0 for a regular player, larger for
    // cinematic shots).
    float FADE_START =
        renderDistance
        - 5.0;

    if (
        cameraDistance
        >
        renderDistance
    )
    {
        discard;
    }

    if (vMatId > 2.5)
    {
        // Same torchLit check as the flame, but particles encode torchIndex * 10 + slot, so the
        // decode differs. Skipped (1.0) for the viewmodel for the same reason as above; it dims by
        // fuel instead.
        const float kFuelShrinkStartsBelow = 0.3; // same threshold as the flame size in scene.vert
        const float kMinVisibleFlameFuel = 0.22;
        float fuelBrightnessParticle =
            uIsViewmodelDraw
                ? mix(kMinVisibleFlameFuel, 1.0, clamp(uViewmodelTorchFuel / kFuelShrinkStartsBelow, 0.0, 1.0))
                : 1.0;

        float torchLitParticle = 1.0;
        if (!uIsViewmodelDraw)
        {
            const float kParticleIdScale = 16384.0;
            float particleKeyForLitCheck =
                floor((vMatId - 3.0) * kParticleIdScale + 0.5);
            float torchIdForLitCheck =
                floor(particleKeyForLitCheck / 10.0);
            torchLitParticle =
                texelFetch(
                    uTorchLitMask,
                    ivec2(
                        int(mod(torchIdForLitCheck, kTorchLitMaskSize)),
                        int(torchIdForLitCheck / kTorchLitMaskSize)
                    ),
                    0
                ).r;
        }

        vec2 centered =
            gl_PointCoord
            - vec2(0.5);

        float pointDistance =
            length(centered);

        if (
            pointDistance > 0.5
        )
        {
            discard;
        }

        float particleAlpha =
            1.0
            -
            smoothstep(
                0.15,
                0.5,
                pointDistance
            );

        // Particle lifetime fade: fully bright for the first ~60% of life, fading to nothing over
        // the last ~40%, completely gone at life = 1.0.

        float lifeFade =
            1.0
            -
            smoothstep(
                0.58,
                1.0,
                vParticleLife
            );

        float lifeGlow =
            lifeFade;

        vec3 particleColor =
            mix(
                vec3(
                    1.0,
                    0.25,
                    0.025
                ),
                vec3(
                    1.0,
                    0.88,
                    0.32
                ),
                1.0 - vParticleLife
            );

        float particleFade =
            1.0
            -
            smoothstep(
                FADE_START,
                renderDistance,
                cameraDistance
            );

        fragColor =
            vec4(
                particleColor,
                particleAlpha
                * lifeGlow
                * particleFade
                * torchLitParticle
                * fuelBrightnessParticle
            );

        return;
    }

    if (vMatId > 1.5)
    {
        const float FLAME_ID_SCALE =
            4096.0;

        float torchId =
            floor(
                (vMatId - 2.0)
                * FLAME_ID_SCALE
                + 0.5
            );

        // Fuel-based brightness: 1.0 for wall torches. The hand torch dims only once 70% of the
        // fuel is spent (kFuelShrinkStartsBelow, matches scene.vert) and never reaches 0 while
        // raised.
        const float kFuelShrinkStartsBelow = 0.3;
        const float kMinVisibleFlameFuel = 0.22;
        float fuelBrightness =
            uIsViewmodelDraw
                ? mix(kMinVisibleFlameFuel, 1.0, clamp(uViewmodelTorchFuel / kFuelShrinkStartsBelow, 0.0, 1.0))
                : 1.0;

        // Whether this specific torch has been extinguished: texel (torchId mod 32, torchId div 32)
        // of the 32x32 lookup texture. Wall torches only: the viewmodel skips the check (torchId =
        // 1536 is outside the texture, texelFetch() would be undefined).
        if (!uIsViewmodelDraw)
        {
            float torchLit =
                texelFetch(
                    uTorchLitMask,
                    ivec2(
                        int(mod(torchId, kTorchLitMaskSize)),
                        int(torchId / kTorchLitMaskSize)
                    ),
                    0
                ).r;

            // This block is in the opaque pass, where alpha = 0 would paint black over the wall, so
            // discard is the only way to hide an extinguished flame.
            if (torchLit < 0.5)
            {
                discard;
            }
        }

        float seed =
            hash(
                vec2(
                    torchId + 4.0,
                    torchId * 7.1
                )
            );

        float flicker =
            1.12
            +
            0.15
            * sin(
                uTime * 13.0
                + seed * 17.0
            )
            +
            0.08
            * sin(
                uTime * 23.0
                + seed * 31.0
            )
            +
            0.045
            * sin(
                uTime * 37.0
                + seed * 7.0
            );

        float verticalFire =
            clamp(
                vNormal.y * 0.5
                + 0.5,
                0.0,
                1.0
            );

        // Landmark torches ask for a blue flame with the signal vColor = (1, 0, 1); a regular torch
        // never has g == 0 (it is always 0.60). r is ignored (it is flicker brightness, not color,
        // see glow below).
        bool isGuideFlame = vColor.g < 0.01 && vColor.b > 0.5;

        vec3 lowerColor =
            isGuideFlame
                ? vec3(0.30, 0.55, 1.0)
                : vec3(
                    1.0,
                    0.82,
                    0.20
                );

        vec3 upperColor =
            isGuideFlame
                ? vec3(0.05, 0.15, 0.55)
                : vec3(
                    1.0,
                    0.30,
                    0.035
                );

        vec3 fireColor =
            mix(
                lowerColor,
                upperColor,
                verticalFire
            );

        vec3 glow =
            fireColor
            * flicker
            * vColor.r
            * fuelBrightness;

        float flameFade =
            1.0
            -
            smoothstep(
                FADE_START,
                renderDistance,
                cameraDistance
            );

        glow *=
            flameFade;

        fragColor =
            vec4(
                clamp(
                    glow,
                    0.0,
                    1.0
                ),
                1.0
            );

        return;
    }

    if (vMatId > 0.5)
    {
        // Extinguished torches also hide their handle (discard), otherwise it would stay on the
        // wall. Skipped for the viewmodel (torchId 1536 is outside uTorchLitMask).
        if (!uIsViewmodelDraw)
        {
            const float HANDLE_ID_SCALE = 4096.0;
            float handleTorchId =
                floor((vMatId - 1.0) * HANDLE_ID_SCALE + 0.5);
            float handleLit =
                texelFetch(
                    uTorchLitMask,
                    ivec2(
                        int(mod(handleTorchId, kTorchLitMaskSize)),
                        int(handleTorchId / kTorchLitMaskSize)
                    ),
                    0
                ).r;
            if (handleLit < 0.5)
            {
                discard;
            }
        }

        vec3 handle =
            vColor
            * 1.15;

        float handleFade =
            1.0
            -
            smoothstep(
                FADE_START,
                renderDistance,
                cameraDistance
            );

        handle *=
            handleFade;

        fragColor =
            vec4(
                clamp(
                    handle,
                    0.0,
                    1.0
                ),
                1.0
            );

        return;
    }

    vec3 n =
        normalize(
            vNormal
        );

    vec2 uv;

    if (abs(n.y) > 0.5)
    {
        uv =
            vWorldPos.xz;
    }
    else if (abs(n.x) > 0.5)
    {
        uv =
            vWorldPos.zy;
    }
    else
    {
        uv =
            vWorldPos.xy;
    }

    vec3 baseColor =
        vColor;

    // Dev tools: the forced palette replaces the regular vColor after it is read but before the
    // texture/detail below, so all following texture/grime/torch tinting works on the new baseColor
    // exactly as with a regular baked zone color; nothing else in the shader has to change.
    if (devPaletteOverride >= 0)
    {
        baseColor =
            (abs(n.y) < 0.5)
                ? kDevPaletteWall[devPaletteOverride]
                : kDevPaletteFloor[devPaletteOverride];
    }

    float detail =
        1.0;

    // The torus has no texture, but the monument does: both branches below (wall and floor) are
    // excluded only for the torus (the narrow range 0.3..0.4); the win button's pedestal (0.2) does
    // not fall into it and gets the regular texture/pattern like any other wall/floor.
    if (!(vMatId > 0.3 && vMatId < 0.4))
    {
    if (abs(n.y) < 0.5)
    {
        // Applies the stone wall texture to any geometry with a vertical normal (abs(n.y) < 0.5
        // separates wall from floor by the normal, not by object type).
        if (wallTexEnabled > 0.5)
        {
            vec2 texUV = uv * WALL_TEX_TILE;
            vec3 texColor = texture(wallTex, texUV).rgb;

            // Contrast stretch around gray before the wall tone and light: without it the dim
            // lighting leaves the ASCII quantizer only a few dark ramp characters. wallTexContrast
            // = 1.0 leaves it unchanged.
            texColor = clamp(
                (texColor - vec3(0.5)) * wallTexContrast + vec3(0.5),
                0.0,
                1.0
            );

            // Tint with baseColor (baked vColor, or the forced palette color) instead of replacing
            // it, so lighting and fog keep working. It must read baseColor, not vColor, or the P
            // key would recolor the floor but not the walls.
            baseColor =
                texColor
                * (
                    baseColor
                    / vec3(0.55, 0.35, 0.25)
                );

            // The same procedural moss/grime on top of the real texture: hides tiling regularity
            // and keeps the damp-dungeon mood whichever image is plugged in.
            float mossNoise =
                hash(
                    floor(
                        uv * 10.0
                    )
                );

            float mossZone =
                clamp(
                    1.0
                    - (
                        uv.y * 1.3
                    ),
                    0.0,
                    1.0
                );

            float moss =
                step(
                    0.55,
                    mossNoise
                )
                * mossZone;

            baseColor =
                mix(
                    baseColor,
                    baseColor
                    * vec3(
                        0.35,
                        0.45,
                        0.30
                    ),
                    moss * 0.8
                );

            detail = 1.0;
        }
        else
        {
            // Fallback: the procedural "brick" look, used only if wallTex failed to load.
            vec2 scaled =
                uv
                * vec2(
                    4.0,
                    6.0
                );

            float row =
                floor(
                    scaled.y
                );

            float rowOffset =
                mod(
                    row,
                    2.0
                )
                * 0.5;

            vec2 scaledOffset =
                vec2(
                    scaled.x
                    + rowOffset,
                    scaled.y
                );

            vec2 brickId =
                floor(
                    scaledOffset
                );

            vec2 b =
                fract(
                    scaledOffset
                );

            float mortarW =
                0.06;

            float mortar =
                step(
                    b.x,
                    mortarW
                )
                +
                step(
                    b.y,
                    mortarW * 1.5
                );

            mortar =
                clamp(
                    mortar,
                    0.0,
                    1.0
                );

            float brickShade =
                hash(
                    brickId
                    + 3.0
                )
                * 0.6
                + 0.35;

            vec3 brickColor =
                baseColor
                * brickShade;

            baseColor =
                mix(
                    brickColor,
                    brickColor
                    * 0.35,
                    mortar
                );

            float mossNoise =
                hash(
                    floor(
                        uv * 10.0
                    )
                );

            float mossZone =
                clamp(
                    1.0
                    - (
                        uv.y * 1.3
                    ),
                    0.0,
                    1.0
                );

            float moss =
                step(
                    0.55,
                    mossNoise
                )
                * mossZone;

            baseColor =
                mix(
                    baseColor,
                    baseColor
                    * vec3(
                        0.35,
                        0.45,
                        0.30
                    ),
                    moss * 0.8
                );

            detail =
                1.0
                - mortar * 0.5;
        }
    }

    else
    {
        vec2 tileUv =
            uv * 3.0;

        vec2 tileId =
            floor(
                tileUv
            );

        vec2 tileFrac =
            fract(
                tileUv
            );

        float groutW =
            0.02;

        float grout =
            step(
                tileFrac.x,
                groutW
            )
            +
            step(
                1.0 - groutW,
                tileFrac.x
            )
            +
            step(
                tileFrac.y,
                groutW
            )
            +
            step(
                1.0 - groutW,
                tileFrac.y
            );

        grout =
            clamp(
                grout,
                0.0,
                1.0
            );

        float tileShade =
            hash(
                tileId
                + 50.0
            )
            * 0.30
            + 0.75;

        baseColor *=
            tileShade;

        baseColor =
            mix(
                baseColor,
                baseColor * 0.25,
                grout
            );

        float mossNoise =
            hash(
                tileId
                + 500.0
            );

        float moss =
            step(
                0.45,
                mossNoise
            );

        vec3 darkMoss =
            baseColor
            * vec3(
                0.30,
                0.42,
                0.28
            );

        baseColor =
            mix(
                baseColor,
                darkMoss,
                moss * 0.7
            );

        float fineNoise =
            hash(
                floor(
                    uv * 25.0
                )
                + 200.0
            )
            * 0.15
            + 0.92;

        baseColor *=
            fineNoise;

        detail =
            1.0
            - grout * 0.6;
    }
    }

    float grain =
        fbmNoise(
            uv * 2.0
            + vColor.xy * 0.01
        )
        * 0.30
        + 0.85;

    baseColor *=
        grain;

    // Kept at 0.05: 0.14 made the image too bright and killed the dungeon's dark atmosphere.
    // Texture readability in shadow is handled by wallTexContrast and WALL_TEX_TILE above, not by
    // overall scene brightness.
    float ambient =
        0.05;

    vec3 lit =
        baseColor
        * ambient
        * detail;

    for (
        int i = 0;
        i < 32;
        i++
    )
    {
        if (i >= torchCount)
            break;

        float seed =
            float(i)
            * 17.0;

        float flicker =
            0.82
            + 0.13
            * sin(
                uTime * 9.0
                + seed
            )
            + 0.05
            * sin(
                uTime * 23.0
                + seed * 1.7
            );

        vec3 toLight =
            torchPos[i]
            - vWorldPos;

        float lightDist =
            length(
                toLight
            );

        if (
            lightDist > 8.0
        )
        {
            continue;
        }

        float ndotl =
            max(
                dot(
                    n,
                    normalize(
                        toLight
                    )
                ),
                0.0
            );

        if (
            ndotl <= 0.001
        )
        {
            continue;
        }

        float attenuation =
            (
                torchIntensity[i]
                * flicker
            )
            /
            (
                1.0
                +
                lightDist
                * lightDist
                * 0.35
            );

        // Perf: skip torches whose contribution is already imperceptible before the expensive
        // shadow raymarch. The threshold is deliberately low so flicker cannot make the light snap
        // on and off.
        if (attenuation * ndotl < 0.0008)
        {
            continue;
        }

        // No per-rank cutoff ("only the nearest N torches cast shadows"): ranks flip as the player
        // moves and shadows would pop; the cost lever is MAX_ACTIVE_TORCHES. Since one ray gives a
        // perfectly sharp shadow edge, the ray origin is jittered by world-space noise seeded per
        // torch, giving a narrow dithered band instead of a crisp line (as in ascii_post.frag).
        float shadowJitter =
            (hash(vWorldPos.xz * 41.0 + float(i) * 17.0) - 0.5) * 0.3;
        vec3 shadowRayFrom =
            vWorldPos
            + n * 0.04
            + vec3(shadowJitter, 0.0, -shadowJitter);

        if (
            shadowedByWall(
                shadowRayFrom,
                torchPos[i]
            )
        )
        {
            continue;
        }

        lit +=
            baseColor
            * torchColor[i]
            * ndotl
            * attenuation
            * detail;
    }

    // The hand torch: no shadowedByWall(), so it is cheap: it does not scale with
    // MAX_ACTIVE_TORCHES, and its cost does not grow with the number of wall torches.

    {
        vec3 toFrag = vWorldPos - playerLightPos;
        float pDist = length(toFrag);

        // A hard outer distance cutoff, to skip the rest of the formula where the contribution is
        // already negligible.
        if (pDist < 12.0)
        {
            vec3 toFragDir = toFrag / max(pDist, 0.0001);

            float pNdotl =
                max(
                    dot(n, -toFragDir),
                    0.0
                );

            if (pNdotl > 0.001)
            {
                // Nearly omnidirectional, as a real torch should be: the difference between "all
                // around" and "where I am looking" is small (0.85 -> 1.0), just a slight forward
                // accent, not a flashlight-like beam.
                float facing =
                    clamp(
                        dot(playerLightDir, toFragDir),
                        -1.0,
                        1.0
                    );
                float forwardBoost =
                    mix(
                        0.85,
                        1.0,
                        smoothstep(-0.2, 0.9, facing)
                    );

                // Falloff coefficient (0.35 for wall torches). Tune this number for a
                // shorter/longer average range in all directions.
                float pFalloff =
                    1.0
                    / (1.0 + pDist * pDist * 0.13);

                // A slight flicker for atmosphere, not in sync with the wall torches (its own phase
                // offset of 3.7).
                float pFlicker =
                    0.9 + 0.1 * sin(uTime * 11.0 + 3.7);

                float pAtten =
                    pFalloff
                    * forwardBoost
                    * playerLightIntensity
                    * pFlicker;

                if (pAtten * pNdotl > 0.0008)
                {
                    lit +=
                        baseColor
                        * playerLightColor
                        * pNdotl
                        * pAtten
                        * detail;
                }
            }
        }
    }

    // Dev tools: static spotlights use the hand torch formula with a real narrow cone, no flicker
    // and a gentler falloff (0.03 vs 0.13) to reach across a room.

    for (
        int i = 0;
        i < 8;
        i++
    )
    {
        if (i >= devLightCount)
            break;

        vec3 toFrag =
            vWorldPos
            - devLightPos[i];

        float dDist =
            length(
                toFrag
            );

        if (
            dDist > 40.0
        )
        {
            continue;
        }

        vec3 toFragDir =
            toFrag
            / max(dDist, 0.0001);

        float dNdotl =
            max(
                dot(n, -toFragDir),
                0.0
            );

        if (dNdotl <= 0.001)
        {
            continue;
        }

        float facing =
            clamp(
                dot(devLightDir[i], toFragDir),
                -1.0,
                1.0
            );

        // A real cone: ~0 outside it. Beyond roughly 56 degrees off-axis the beam gives nothing,
        // within roughly 32 degrees it is at full brightness, with a soft but noticeable edge in
        // between.
        float cone =
            smoothstep(0.55, 0.85, facing);

        if (cone <= 0.001)
        {
            continue;
        }

        float dFalloff =
            1.0
            / (1.0 + dDist * dDist * 0.03);

        float dAtten =
            dFalloff
            * cone
            * 2.0; // intensity: noticeably brighter than the hand torch (a spotlight tool, not a wick)

        if (dAtten * dNdotl > 0.0008)
        {
            lit +=
                baseColor
                * vec3(0.90, 0.95, 1.0)
                * dNdotl
                * dAtten
                * detail;
        }
    }

    float dist =
        length(
            vWorldPos
            - camPos
        );

    float fogNear =
        4.0;

    float fogFar =
        renderDistance;

    float fogVisibility =
        1.0
        -
        smoothstep(
            fogNear,
            fogFar,
            dist
        );

    lit *=
        fogVisibility;

    lit =
        clamp(
            lit,
            0.0,
            1.0
        );

    // "Monument dissolves": see uMonumentFadeAlpha above. A narrow range (0.05..0.5) so it does not
    // affect the rest of the regular opaque geometry, whose matId is exactly 0.0.
    float outAlpha = 1.0;
    if (vMatId > 0.05 && vMatId < 0.5)
        outAlpha = uMonumentFadeAlpha;

    fragColor =
        vec4(
            lit,
            outAlpha
        );
}
