#version 330 core
in vec2 vUV;
out vec4 fragColor;

// AcerolaAscii output: one texel per cell (rgb = tint, a = glyph index / 255) and its glyph atlas.
uniform sampler2D sceneCellTex;
uniform sampler2D sceneGlyphTex;

// 0 = ASCII, 1 = raw 3D scene (dev), 2 = edge mask (dev), 3 = plain 3D for players ("shaders off").
// Plain applies the same gamma lift and exposure as the glyph ramp, so brightness roughly matches.
uniform int renderView;
uniform sampler2D debugSceneTex;
uniform sampler2D debugEdgesTex;
uniform vec2  screenResolution;
uniform float cellSize;

uniform sampler2D uiFontTex;  // a fixed (not brightness-based) glyph atlas
uniform float uiGlyphCount;

// UI text layer, drawn over everything. R8, one texel per menu cell: 0 = empty, else glyph index +
// 1.
uniform sampler2D uiTex;
uniform float uiCols;
uniform float uiRows;
uniform float uiEnabled;

// Full-screen fade (0 = none, 1 = black), applied on every exit path of main().
uniform float fadeAlpha;

uniform float staminaFrac;
uniform float staminaAlpha;
uniform float healthFrac;     // 0..1, health fraction; controls how much the bar's frame drips

// Color mode: multiplies the finished 0/1 glyph mask by a tint; glyph choice is unchanged.
uniform float colorEnabled;

// Lens: the vignette is applied here. The barrel distortion happens in AcerolaAscii, so the grid,
// UI text and HUD stay straight.
uniform float lensEffectEnabled;
// Seconds since start, for the blood drips.
uniform float uTime;

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// Deterministic per-cell hash, so drips do not jitter between frames.
float hash11(float p) {
    return fract(sin(p * 12.9898) * 43758.5453);
}

// Whether a drop from column sourceCol covers this cell. A big drop spills one cell to the right;
// requireBig tests only for that.
bool evaluateBloodDrop(
    float sourceCol, float row, float totalRowsF, float bloodDamage, bool requireBig,
    vec2 fragPixelIn, float lensR2In, out vec3 outColor
) {
    // Only some columns carry a drop, deterministic from the column number.
    float colHash = hash11(sourceCol + 517.0);
    const float kMaxActiveColumnFraction = 0.10; // fraction of active columns at maximum damage
    if (colHash >= bloodDamage * kMaxActiveColumnFraction)
        return false;

    // ~30% of drops are big (two cells wide), for variety.
    float bigHash = hash11(sourceCol + 3301.0);
    bool isBig = bigHash < 0.30;
    if (requireBig && !isBig)
        return false;

    float speedJitter = 0.6 + 0.8 * hash11(sourceCol + 991.0);
    float phase = hash11(sourceCol + 233.0);
    // Big drops fall slower and have longer tails.
    const float kTrailLengthCells = 5.0;
    const float kFallSpeedCellsPerSec = 7.0;
    float trailLen = isBig ? kTrailLengthCells * 1.4 : kTrailLengthCells;
    float fallSpeed = isBig ? kFallSpeedCellsPerSec * 0.75 : kFallSpeedCellsPerSec;

    float cycleLen = totalRowsF + trailLen;
    float t = fract(uTime * fallSpeed * speedJitter / cycleLen + phase);
    // The head starts above the top edge and moves down.
    float headRow = totalRowsF - t * cycleLen;

    // Positive above the head: the tail trails upward.
    float distBehindHead = row - headRow;
    if (distBehindHead < 0.0 || distBehindHead >= trailLen)
        return false;

    // Big drops draw more rows with the large glyph (16) before the small one (17).
    float headZoneRows = isBig ? 2.4 : 1.3;
    float glyphIdx = (distBehindHead < headZoneRows) ? 16.0 : 17.0;

    vec2 localUV = fract(fragPixelIn / cellSize);
    localUV.y = 1.0 - localUV.y;
    vec2 glyphOrigin = vec2(glyphIdx / uiGlyphCount, 0.0);
    vec2 glyphUV = glyphOrigin + vec2(localUV.x / uiGlyphCount, localUV.y);
    float mask = texture(uiFontTex, glyphUV).r;
    if (mask <= 0.5)
        return false;

    // Tail darkening is capped at x0.75 and edge darkening at x0.55, so the worst case (~0.41)
    // stays visible.
    float trailFade = 1.0 - (distBehindHead / trailLen) * 0.25;
    float edgeDist = clamp(sqrt(lensR2In), 0.0, 1.0);
    float edgeDarken = mix(1.0, 0.55, edgeDist);

    outColor = vec3(1.0, 0.04, 0.04) * trailFade * edgeDarken;
    return true;
}

void main() {
    vec2 fragPixel  = vUV * screenResolution;
    vec2 cellIndex  = floor(fragPixel / cellSize);
    vec2 cellOrigin = cellIndex * cellSize;

    // Lens vignette, shared by every branch below.
    const float LENS_VIGNETTE_STRENGTH = 0.5;
    vec2  lensCenterPx   = screenResolution * 0.5;
    // Normalized per axis, so r = 1 is the middle of any screen edge regardless of aspect ratio.
    vec2  lensFromCenter = (fragPixel - lensCenterPx) / (screenResolution * 0.5);
    float lensR2         = dot(lensFromCenter, lensFromCenter);
    float lensVignette   = (lensEffectEnabled > 0.5)
        ? (1.0 - LENS_VIGNETTE_STRENGTH * smoothstep(0.78, 1.25, sqrt(lensR2)))
        : 1.0;

    // Health vignette: edges darken as HP drops; none at full HP.
    const float HEALTH_VIGNETTE_MAX_STRENGTH = 0.85;
    float healthDamage    = 1.0 - clamp(healthFrac, 0.0, 1.0);
    float healthInnerR    = mix(0.95, 0.35, healthDamage);
    float healthOuterR    = mix(1.25, 0.85, healthDamage);
    float healthVignette  = 1.0 - HEALTH_VIGNETTE_MAX_STRENGTH * healthDamage
        * smoothstep(healthInnerR, healthOuterR, sqrt(lensR2));

    // The lens and health vignettes multiply.
    float screenVignette = lensVignette * healthVignette;

    // UI text uses its own grid, independent of SHARPNESS.
    vec2 screenUV = fragPixel / screenResolution;
    vec2 uiCellF = screenUV * vec2(uiCols, uiRows);
    vec2 uiCellIndex = floor(uiCellF);

    if (uiEnabled > 0.5 &&
        uiCellIndex.x >= 0.0 && uiCellIndex.x < uiCols &&
        uiCellIndex.y >= 0.0 && uiCellIndex.y < uiRows) {

        vec2 localUV = fract(uiCellF);
        localUV.y = 1.0 - localUV.y;

        // uiTex rows are stored top first.
        vec2 uiTexel = (uiCellIndex + 0.5) / vec2(uiCols, uiRows);
        uiTexel.y = 1.0 - uiTexel.y;

        float raw = floor(texture(uiTex, uiTexel).r * 255.0 + 0.5);

        if (raw > 0.5) {
            float glyphIdx = raw - 1.0;
            vec2 glyphOrigin = vec2(glyphIdx / uiGlyphCount, 0.0);
            vec2 glyphUV = glyphOrigin + vec2(localUV.x / uiGlyphCount, localUV.y);
            float mask = texture(uiFontTex, glyphUV).r;
            vec3 bwColor = vec3(step(0.5, mask));

            // Warm parchment tint for UI text in color mode.
            const vec3 UI_TINT = vec3(0.88, 0.72, 0.38);
            vec3 outColor = mix(bwColor, bwColor * UI_TINT, colorEnabled);

            outColor = mix(outColor, vec3(0.0), fadeAlpha);
            outColor *= screenVignette;
            fragColor = vec4(outColor, 1.0);
            return;
        }
    }

    // Blended at the end of main() so the bar fades smoothly.
    vec3  hudColor = vec3(0.0);
    float hudAlpha = 0.0;

    // Stamina bar: about 2/3 of the grid width, '#' fill for stamina, a ragged blood frame for
    // health.
    if (staminaAlpha > 0.001) {
        float totalCols = floor(screenResolution.x / cellSize);
        float totalRows = floor(screenResolution.y / cellSize);

        float barInnerCols = floor(totalCols * 0.675);
        if (barInnerCols < 4.0) barInnerCols = 4.0;

        float barTotalCols = barInnerCols + 2.0;
        float barStartCol  = floor((totalCols - barTotalCols) * 0.5);
        float barEndCol    = barStartCol + barTotalCols - 1.0;

        // Four rows: frame top and bottom plus two fill rows.
        const float BOTTOM_PAD = 2.0;
        float rowBottomBorder  = BOTTOM_PAD;
        float rowContentLow    = BOTTOM_PAD + 1.0;
        float rowContentHigh   = BOTTOM_PAD + 2.0;
        float rowTopBorder     = BOTTOM_PAD + 3.0;

        float col = cellIndex.x;
        float row = cellIndex.y;

        if (col >= barStartCol && col <= barEndCol &&
            row >= rowBottomBorder && row <= rowTopBorder) {

            vec2 localUV = fract(fragPixel / cellSize);
            localUV.y = 1.0 - localUV.y;

            bool atLeftEdge  = (col == barStartCol);
            bool atRightEdge = (col == barEndCol);
            bool atTopEdge    = (row == rowTopBorder);
            bool atBottomEdge = (row == rowBottomBorder);

            // Drip density grows as health drops; a clean frame at full health.
            float dripDensity = clamp((1.0 - healthFrac) * 0.9, 0.0, 0.9);

            float glyphIdx;
            if ((atLeftEdge || atRightEdge) && (atTopEdge || atBottomEdge)) {
                glyphIdx = 15.0;
            } else if (atTopEdge || atBottomEdge) {
                // Different seeds for top and bottom so the drips do not mirror.
                float seedOffset = atTopEdge ? 401.0 : 733.0;
                float rnd = hash11(col + seedOffset);

                if (rnd < dripDensity * 0.6) {
                    glyphIdx = 16.0;
                } else if (rnd < dripDensity) {
                    glyphIdx = 17.0;
                } else {
                    glyphIdx = 13.0;
                }
            } else if (atLeftEdge || atRightEdge) {
                // Same per row for the left and right sides.
                float seedOffset = atLeftEdge ? 157.0 : 911.0;
                float rnd = hash11(row + seedOffset);

                if (rnd < dripDensity * 0.6) {
                    glyphIdx = 16.0;
                } else if (rnd < dripDensity) {
                    glyphIdx = 17.0;
                } else {
                    glyphIdx = 14.0;
                }
            } else {
                float innerIndex  = col - (barStartCol + 1.0);
                float filledCols  = floor(clamp(staminaFrac, 0.0, 1.0) * barInnerCols + 0.5);
                glyphIdx = (innerIndex < filledCols) ? 1.0 : 0.0;
            }

            vec3 outColor = vec3(0.0);
            if (glyphIdx >= 0.0) {
                vec2 glyphOrigin = vec2(glyphIdx / uiGlyphCount, 0.0);
                vec2 glyphUV = glyphOrigin + vec2(localUV.x / uiGlyphCount, localUV.y);
                float mask = texture(uiFontTex, glyphUV).r;
                vec3 bwColor = vec3(step(0.5, mask));

                // Color mode: dark red frame, red-to-blue fill (no amber midpoint, a yellow-blue
                // blend goes muddy green).
                bool isFrameGlyph = (atLeftEdge || atRightEdge || atTopEdge || atBottomEdge);
                vec3 tint;
                if (isFrameGlyph) {
                    tint = vec3(0.55, 0.05, 0.05);
                } else {
                    vec3 lowColor  = vec3(0.80, 0.12, 0.10); // nearly empty: red
                    vec3 highColor = vec3(0.20, 0.65, 0.95); // full: blue
                    float s = clamp(staminaFrac, 0.0, 1.0);
                    tint = mix(lowColor, highColor, s);
                }

                outColor = mix(bwColor, bwColor * tint, colorEnabled);
            }

            // No early return: the scene is still computed under the bar so it shows through while
            // the bar fades.
            hudColor = outColor;
            hudAlpha = staminaAlpha;
        }
    }

    // Glyph choice, tint, lens warp and the glitch spot were resolved per cell by AcerolaAscii.
    ivec2 cellI = ivec2(cellIndex);
    vec4 cellData = texelFetch(sceneCellTex, cellI, 0);
    int sceneGlyph = int(cellData.a * 255.0 + 0.5);
    int cellPx = int(cellSize);
    ivec2 localPx = ivec2(fragPixel) - cellI * cellPx;
    float sceneInk = texelFetch(sceneGlyphTex, ivec2(sceneGlyph * cellPx + localPx.x, cellPx - 1 - localPx.y), 0).r;
    vec3 sceneColor = cellData.rgb * step(0.5, sceneInk);
    if (renderView == 1)
        sceneColor = textureLod(debugSceneTex, vUV, 0.0).rgb;
    else if (renderView == 2)
        sceneColor = vec3(texture(debugEdgesTex, vUV).r);
    else if (renderView == 3)
        sceneColor = clamp(pow(textureLod(debugSceneTex, vUV, 0.0).rgb, vec3(0.75)) * 1.2, 0.0, 1.0);

    vec3 finalColor = mix(sceneColor, hudColor, hudAlpha);
    finalColor = mix(finalColor, vec3(0.0), fadeAlpha);
    finalColor *= screenVignette;

    // Blood drips across the screen while hurt, reusing glyphs 16/17.
    float bloodDamage = 1.0 - clamp(healthFrac, 0.0, 1.0);
    if (bloodDamage > 0.001) {
        float totalRowsF = floor(screenResolution.y / cellSize);
        vec3 bloodColor;
        // This column's drop, or the right half of a big drop from the column to the left.
        if (evaluateBloodDrop(cellIndex.x, cellIndex.y, totalRowsF, bloodDamage, false, fragPixel, lensR2, bloodColor)) {
            finalColor = bloodColor;
        } else if (evaluateBloodDrop(cellIndex.x - 1.0, cellIndex.y, totalRowsF, bloodDamage, true, fragPixel, lensR2, bloodColor)) {
            finalColor = bloodColor;
        }
    }

    fragColor = vec4(finalColor, 1.0);
}
