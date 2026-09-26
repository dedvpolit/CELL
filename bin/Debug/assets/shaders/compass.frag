#version 330 core

in vec3 vNormal;
in vec3 vWorldPos;
in float vType;
in vec3 vLocal;

out vec4 fragColor;

uniform sampler2D minimapTex;
uniform float minimapYawDeg;

uniform sampler2D uiFontTex;
uniform float uiGlyphCount;

// Same toggle as AsciiEffect (color mode): the compass uses its own shader and does not go through
// the ASCII post-process, so it applies the tint itself.
uniform float colorEnabled;
// enemyMinimapOffset[i]: enemy cell minus player cell on X, reversed on Z (computed on the CPU,
// same system as off below). enemySpottedAlpha[i] fades from 1 to 0
// (EnemyAI::spottedMarkerAlpha()); at 0 the marker is skipped. enemyCount = filled entries
// (0..kMaxEnemies).
const int kMaxEnemies = 7; // = DungeonScene::kEnemyCount — keep in sync if the enemy count changes
uniform float enemySpottedAlpha[kMaxEnemies];
uniform vec2 enemyMinimapOffset[kMaxEnemies];
uniform int enemyCount;

const float MAP_N = 21.0;
const float MAP_RADIUS = 0.235;

void main()
{
    if (vType > 1.5 && vType < 2.5) {
        vec2 local = vLocal.xz;

        const float halfGrid = 10.0;              // (MAP_N - 1) / 2, MAP_N = 21
        const float ringOuterEdge = halfGrid + 1.0;
        const float marginRadius = ringOuterEdge + 1.0;

        float cellSize = MAP_RADIUS / marginRadius;

        vec2 cellF = local / cellSize;
        vec2 off = floor(cellF + 0.5);
        vec2 localUV = fract(cellF + 0.5);
        localUV.y = 1.0 - localUV.y;

        float dist = length(off);
        if (dist > marginRadius)
            discard;

        float glyphIdx = -1.0;
        float matchedEnemyAlpha = 0.0; // 0 if no enemy landed in this cell (see outColor below)
        // Landmark torch cell (value 220/255 ~ 0.86, between a regular wall 170/255 ~ 0.667 and a
        // torch 255/255 = 1.0): colored blue "*" instead of the usual orange. Stored here because
        // glyph tinting is computed further down, outside v's scope.
        bool isGuideTorchCell = false;
        // "Been here before" (value 200/255 ~ 0.78, between a regular wall and a landmark torch):
        // the perimeter wall of a pocket whose diary has been read is colored purple (#AC5CCB)
        // instead of the usual gray. Stored here for the same reason as isGuideTorchCell.
        bool isDiaryReadWallCell = false;

        if (dist <= halfGrid) {
            if (abs(off.x) < 0.5 && abs(off.y) < 0.5) {
                float yawRad = radians(minimapYawDeg);
                vec2 dir = vec2(cos(yawRad), -sin(yawRad));

                float TWO_PI = 6.2831853;
                float ang = atan(dir.y, dir.x);
                if (ang < 0.0)
                    ang += TWO_PI;

                float sector = floor(mod(ang / (TWO_PI / 8.0) + 0.5, 8.0));

                if (sector < 0.5)
                    glyphIdx = 6.0;      // >
                else if (sector < 1.5)
                    glyphIdx = 5.0;      // /
                else if (sector < 2.5)
                    glyphIdx = 4.0;      // ^
                else if (sector < 3.5)
                    glyphIdx = 11.0;     // NW
                else if (sector < 4.5)
                    glyphIdx = 10.0;     // <
                else if (sector < 5.5)
                    glyphIdx = 9.0;      // /
                else if (sector < 6.5)
                    glyphIdx = 8.0;      // v
                else
                    glyphIdx = 7.0;      // SE
            } else {
                float gx = off.x + halfGrid;
                float gy = halfGrid - off.y;

                // Only the best-matching enemy's alpha is stored; the marker is blended after the
                // cell glyph so it fades smoothly instead of swapping glyphs. The 0.5 tolerance
                // covers interpolation error (offsets are integers). With two enemies in one cell
                // the larger alpha wins.
                for (int ei = 0; ei < kMaxEnemies; ei++) {
                    if (ei >= enemyCount)
                        break;
                    if (distance(off, enemyMinimapOffset[ei]) < 0.5) {
                        matchedEnemyAlpha = max(matchedEnemyAlpha, enemySpottedAlpha[ei]);
                    }
                }

                vec2 mapUV = (vec2(gx, gy) + 0.5) / MAP_N;
                float v = texture(minimapTex, mapUV).r;

                // A landmark torch is encoded as 220/255 ~ 0.8627, between a regular wall (170/255
                // ~ 0.667) and a regular torch (1.0). It uses the same "*" glyph as a regular
                // torch; the only difference is the tint below (blue instead of orange).
                if (v > 0.95) {
                    glyphIdx = 3.0;      // * (regular torch)
                } else if (v > 0.8) {
                    glyphIdx = 3.0;      // * (landmark torch)
                    isGuideTorchCell = true;
                } else if (v > 0.7) {
                    glyphIdx = 1.0;      // # (pocket wall with a read diary)
                    isDiaryReadWallCell = true;
                } else if (v > 0.55) {
                    glyphIdx = 1.0;      // #
                } else if (v > 0.2) {
                    glyphIdx = 2.0;      // .
                } else {
                    glyphIdx = -1.0;     // unexplored
                }
            }
        } else {
            float R = ringOuterEdge;
            float ax = abs(off.x);
            float ay = abs(off.y);

            float idealY = sqrt(max(R * R - ax * ax, 0.0));
            float idealX = sqrt(max(R * R - ay * ay, 0.0));

            bool testY = (ax <= R) && (abs(ay - idealY) < 0.55);
            bool testX = (ay <= R) && (abs(ax - idealX) < 0.55);

            if (testY || testX)
                glyphIdx = 12.0;
        }

        vec3 outColor = vec3(0.0);

        if (glyphIdx >= 0.0) {
            vec2 glyphOrigin = vec2(glyphIdx / uiGlyphCount, 0.0);
            vec2 glyphUV = glyphOrigin + vec2(localUV.x / uiGlyphCount, localUV.y);
            float mask = texture(uiFontTex, glyphUV).r;
            vec3 bwColor = vec3(step(0.5, mask));

            vec3 tint = vec3(1.0);
            if (glyphIdx == 3.0)      tint = isGuideTorchCell ? vec3(0.25, 0.55, 1.0) : vec3(1.00, 0.55, 0.10);
            else if (glyphIdx == 1.0) tint = isDiaryReadWallCell ? vec3(0.675, 0.361, 0.796) : vec3(0.55, 0.55, 0.62);
            else if (glyphIdx == 2.0) tint = vec3(0.42, 0.34, 0.22);
            else if (glyphIdx >= 4.0 && glyphIdx <= 11.0)
                                        tint = vec3(0.20, 0.90, 0.95);
            else if (glyphIdx == 12.0) tint = vec3(0.62, 0.62, 0.68);

            outColor = mix(bwColor, bwColor * tint, colorEnabled);
        }

        // Blended on top of the already computed outColor (fog/wall/floor/torch/empty) instead of
        // swapping glyphIdx ahead of time, so the transition is smooth: at matchedEnemyAlpha = 1 it
        // is fully the marker, at 0 fully what it would be without it, and any value in between is
        // an honest mix of both colors.
        if (matchedEnemyAlpha > 0.001) {
            vec2 glyphOrigin = vec2(26.0 / uiGlyphCount, 0.0);
            vec2 glyphUV = glyphOrigin + vec2(localUV.x / uiGlyphCount, localUV.y);
            float markerMask = texture(uiFontTex, glyphUV).r;
            vec3 markerBW = vec3(step(0.5, markerMask));
            vec3 markerTint = vec3(0.95, 0.12, 0.12);
            vec3 markerColor = mix(markerBW, markerBW * markerTint, colorEnabled);
            outColor = mix(outColor, markerColor, matchedEnemyAlpha);
        }

        fragColor = vec4(outColor, 1.0);
        return;
    }

    vec3 n = normalize(vNormal);
    float topLight = 0.55 + 0.45 * max(dot(n, vec3(0.0, 1.0, 0.0)), 0.0);
    vec3 metal = vec3(0.12, 0.12, 0.10) * topLight;

    if (vType < 0.5)
        metal += vec3(0.08, 0.07, 0.05);

    fragColor = vec4(clamp(metal, 0.0, 1.0), 1.0);
}
