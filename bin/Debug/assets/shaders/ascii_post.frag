#version 330 core
in vec2 vUV;
out vec4 fragColor;

uniform sampler2D sceneTex;
uniform sampler2D fontTex;
uniform vec2  screenResolution;
uniform float cellSize;
uniform float rampLength;

uniform sampler2D uiFontTex;  // a fixed (not brightness-based) glyph atlas
uniform float uiGlyphCount;

// UI text layer: drawn first over everything and returns early on a text cell. uiTex is R8, uiCols
// x uiRows (one texel per character cell): 0 = empty, otherwise glyphIndex + 1.
uniform sampler2D uiTex;
uniform float uiCols;
uniform float uiRows;
uniform float uiEnabled;

// Smooth full-screen fade (0 = none, 1 = black) for the transition between the start menu and
// gameplay. Applied uniformly on every exit path of main() (UI text, regular ASCII scene +
// stamina) instead of a separate pass, so no second draw call or shader is needed.
uniform float fadeAlpha;

uniform float staminaFrac;
uniform float staminaAlpha;
uniform float healthFrac;     // 0..1, health fraction; controls how much the bar's frame drips

// Color mode: 0 = classic black-and-white ASCII, 1 = every element is tinted. This is purely a tint
// on the already chosen ASCII glyph/mask: the character set and layout never change, color just
// multiplies the finished 0/1 glyph mask, so the image stays strictly ASCII in either mode.
uniform float colorEnabled;

// "Unreliable vision": a disappearing glyph. uGlitchActive == 0 disables the effect entirely.
// uGlitchUV is the normalized (0..1) position of the glitching cell in screen coordinates, not grid
// coordinates (the CPU does not need to know this frame's real cols/rows).
uniform float uGlitchActive;
uniform vec2 uGlitchUV;
uniform float uGlitchRadiusCells; // the "spot" size in ASCII-grid cells, not pixels

// Lens (Settings -> LENS): a subtle barrel distortion of the scene sampling plus a circular
// vignette. The character grid, UI text and HUD stay undistorted.
uniform float lensEffectEnabled;
// Seconds since app start; used by the falling blood-drip animation (see bloodDamage/headRow near
// the end of main()).
uniform float uTime;

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// A simple deterministic pseudo-random hash of one number: decides whether a specific stamina-bar
// frame cell "drips", independent of frame/time (otherwise the drips would jitter).
float hash11(float p) {
    return fract(sin(p * 12.9898) * 43758.5453);
}

// Is a drop falling from column sourceCol? (not necessarily this fragment's column: a big drop
// spills one cell to the right). requireBig limits the check to a column whose drop is big.
bool evaluateBloodDrop(
    float sourceCol, float row, float totalRowsF, float bloodDamage, bool requireBig,
    vec2 fragPixelIn, float lensR2In, out vec3 outColor
) {
    // Column hash: decides whether the column has a drop at all (not every column at once, or it
    // would be a solid blood wall instead of separate drops). It is independent of time/frame,
    // deterministic from the column number, the same trick as the stamina bar's frame.
    float colHash = hash11(sourceCol + 517.0);
    const float kMaxActiveColumnFraction = 0.10; // fraction of active columns at maximum damage
    if (colHash >= bloodDamage * kMaxActiveColumnFraction)
        return false;

    // ~30% of active drops are big (thicker, 2 cells wide), 70% are regular (1 cell): this variety
    // avoids looking like a uniform Matrix-style character rain.
    float bigHash = hash11(sourceCol + 3301.0);
    bool isBig = bigHash < 0.30;
    if (requireBig && !isBig)
        return false;

    float speedJitter = 0.6 + 0.8 * hash11(sourceCol + 991.0);
    float phase = hash11(sourceCol + 233.0);
    // Big drops fall slightly slower (heavier) and have a longer tail: a natural difference between
    // a small and a big drop, not just head size.
    const float kTrailLengthCells = 5.0;
    const float kFallSpeedCellsPerSec = 7.0;
    float trailLen = isBig ? kTrailLengthCells * 1.4 : kTrailLengthCells;
    float fallSpeed = isBig ? kFallSpeedCellsPerSec * 0.75 : kFallSpeedCellsPerSec;

    float cycleLen = totalRowsF + trailLen;
    float t = fract(uTime * fallSpeed * speedJitter / cycleLen + phase);
    // The head starts above the screen's top edge (totalRowsF) and decreases toward the bottom
    // (-trailLen): the drop falls down.
    float headRow = totalRowsF - t * cycleLen;

    // Positive if this cell is above the drop's head: the falling drop already passed this level,
    // so the tail trails upward from the current head.
    float distBehindHead = row - headRow;
    if (distBehindHead < 0.0 || distBehindHead >= trailLen)
        return false;

    // Big drops are "thicker": noticeably more rows are drawn with the large glyph (16) before
    // switching to the small one (17), not just one row.
    float headZoneRows = isBig ? 2.4 : 1.3;
    float glyphIdx = (distBehindHead < headZoneRows) ? 16.0 : 17.0;

    vec2 localUV = fract(fragPixelIn / cellSize);
    localUV.y = 1.0 - localUV.y;
    vec2 glyphOrigin = vec2(glyphIdx / uiGlyphCount, 0.0);
    vec2 glyphUV = glyphOrigin + vec2(localUV.x / uiGlyphCount, localUV.y);
    float mask = texture(uiFontTex, glyphUV).r;
    if (mask <= 0.5)
        return false;

    // Blood color: full base brightness (1.0), with the tail darkening capped at x0.75 and the edge
    // at x0.55. The worst case (a drop's tail at the screen's very edge) is ~0.41, so drops stay
    // noticeably darker in the tail and at the edge without dissolving into the black background.
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

    // Lens: shared values computed once for the whole fragment and reused in every branch below (UI
    // text, regular ASCII scene). LENS_DISTORT_STRENGTH/LENS_VIGNETTE_STRENGTH are
    // deliberately small: the effect should be barely noticeable, not an obvious fisheye.
    const float LENS_DISTORT_STRENGTH  = 0.05;
    // Hand-tuned: 0.16 was unnoticeable and 0.55 too strong (almost the whole screen edge went
    // black). 0.5 with the 0.78..1.25 range is the chosen value.
    const float LENS_VIGNETTE_STRENGTH = 0.5;
    vec2  lensCenterPx   = screenResolution * 0.5;
    // Normalized per axis (each by half the width/height), so r = 1 is the middle of any screen
    // edge; dividing by one dimension darkened the sides too much on wide monitors.
    vec2  lensFromCenter = (fragPixel - lensCenterPx) / (screenResolution * 0.5);
    float lensR2         = dot(lensFromCenter, lensFromCenter);
    float lensVignette   = (lensEffectEnabled > 0.5)
        ? (1.0 - LENS_VIGNETTE_STRENGTH * smoothstep(0.78, 1.25, sqrt(lensR2)))
        : 1.0;

    // Health vignette: the edges darken as HP drops (strength and start radius depend on health);
    // reuses lensR2. No effect at full HP. healthFrac also drives the stamina bar's drips.
    const float HEALTH_VIGNETTE_MAX_STRENGTH = 0.85;
    float healthDamage    = 1.0 - clamp(healthFrac, 0.0, 1.0);
    float healthInnerR    = mix(0.95, 0.35, healthDamage);
    float healthOuterR    = mix(1.25, 0.85, healthDamage);
    float healthVignette  = 1.0 - HEALTH_VIGNETTE_MAX_STRENGTH * healthDamage
        * smoothstep(healthInnerR, healthOuterR, sqrt(lensR2));

    // The two vignettes (the LENS setting and the HP indicator) are independent and simply
    // multiplied, so they combine instead of replacing each other: full HP + LENS = a light lens
    // vignette; low HP without LENS = only the alarming damage darkening; both = stronger.
    float screenVignette = lensVignette * healthVignette;

    // UI text overrides everything in its cells and is positioned by normalized UV (screenUV *
    // uiCols/uiRows), not the live scene grid: the menu has its own grid independent of SHARPNESS.
    vec2 screenUV = fragPixel / screenResolution;
    vec2 uiCellF = screenUV * vec2(uiCols, uiRows);
    vec2 uiCellIndex = floor(uiCellF);

    if (uiEnabled > 0.5 &&
        uiCellIndex.x >= 0.0 && uiCellIndex.x < uiCols &&
        uiCellIndex.y >= 0.0 && uiCellIndex.y < uiRows) {

        vec2 localUV = fract(uiCellF);
        localUV.y = 1.0 - localUV.y;

        // uiTex is built on the CPU top-to-bottom (row 0 = top of the screen) while texture UV
        // grows bottom-to-top, so the row is flipped.
        vec2 uiTexel = (uiCellIndex + 0.5) / vec2(uiCols, uiRows);
        uiTexel.y = 1.0 - uiTexel.y;

        float raw = floor(texture(uiTex, uiTexel).r * 255.0 + 0.5);

        if (raw > 0.5) {
            float glyphIdx = raw - 1.0;
            vec2 glyphOrigin = vec2(glyphIdx / uiGlyphCount, 0.0);
            vec2 glyphUV = glyphOrigin + vec2(localUV.x / uiGlyphCount, localUV.y);
            float mask = texture(uiFontTex, glyphUV).r;
            vec3 bwColor = vec3(step(0.5, mask));

            // UI text tint in color mode: a warm "aged parchment" fitting the dark-fantasy style.
            // The mask (bwColor) stays 0/1: tinting only colors the already drawn ASCII character.
            const vec3 UI_TINT = vec3(0.88, 0.72, 0.38);
            vec3 outColor = mix(bwColor, bwColor * UI_TINT, colorEnabled);

            outColor = mix(outColor, vec3(0.0), fadeAlpha);
            outColor *= screenVignette;
            fragColor = vec4(outColor, 1.0);
            return;
        }
    }

    // HUD stamina bar color/opacity. Not returned immediately but blended at the end of main() so
    // it fades smoothly (staminaAlpha is continuous).
    vec3  hudColor = vec3(0.0);
    float hudAlpha = 0.0;

    // The stamina bar: about 2/3 of the grid width with a +/=/| frame and '#' fill. The frame is a
    // ragged blood streak whose density follows health; the fill follows stamina. Drawn by
    // cellIndex over the ASCII scene.
    if (staminaAlpha > 0.001) {
        float totalCols = floor(screenResolution.x / cellSize);
        float totalRows = floor(screenResolution.y / cellSize);

        float barInnerCols = floor(totalCols * 0.675);
        if (barInnerCols < 4.0) barInnerCols = 4.0;

        float barTotalCols = barInnerCols + 2.0;
        float barStartCol  = floor((totalCols - barTotalCols) * 0.5);
        float barEndCol    = barStartCol + barTotalCols - 1.0;

        // A small margin from the very bottom so the bar does not touch the window edge. The bar is
        // 4 character rows high: top/bottom frame border + two fill rows (so "###" is more
        // noticeable than a single thin row).
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

            // The lower the health, the denser the drips across the whole frame. At healthFrac >=
            // 1.0 the density is 0 (a perfectly clean frame).
            float dripDensity = clamp((1.0 - healthFrac) * 0.9, 0.0, 0.9);

            float glyphIdx;
            if ((atLeftEdge || atRightEdge) && (atTopEdge || atBottomEdge)) {
                glyphIdx = 15.0;
            } else if (atTopEdge || atBottomEdge) {
                // Horizontal sides: each column is either a clean line segment or a drip. A
                // different seed offset for top/bottom so the drips do not mirror each other.
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
                // Vertical sides: the same idea per row, with its own seed offset for left/right.
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

                // Color-mode tint of the bar: dark red frame, red-to-blue fill (no amber midpoint:
                // a yellow-blue RGB blend goes muddy green).
                bool isFrameGlyph = (atLeftEdge || atRightEdge || atTopEdge || atBottomEdge);
                vec3 tint;
                if (isFrameGlyph) {
                    tint = vec3(0.55, 0.05, 0.05);
                } else {
                    vec3 lowColor  = vec3(0.80, 0.12, 0.10); // nearly empty — red
                    vec3 highColor = vec3(0.20, 0.65, 0.95); // full — blue
                    float s = clamp(staminaFrac, 0.0, 1.0);
                    tint = mix(lowColor, highColor, s);
                }

                outColor = mix(bwColor, bwColor * tint, colorEnabled);
            }

            // Not returning immediately: the bar's color/alpha is stored and the shader below still
            // computes the regular ASCII scene under it, so at staminaAlpha < 1 whatever normally
            // draws in this cell shows through smoothly.
            hudColor = outColor;
            hudAlpha = staminaAlpha;
        }
    }

    // Perf: a hardware mip lookup replaces a 3x3 box filter of sceneTex. The result is constant
    // across a glyph cell, so per-pixel filtering repeated the same fetches for every pixel. The
    // mip chain is built once per frame in AsciiEffect::end().
    vec2 samplePx = cellOrigin + vec2(0.5) * cellSize;

    // Lens: shifts the sampling point outward proportionally to distance squared (barrel). Only
    // what is shown inside a cell moves, not the grid; the offset is computed once per cell center.
    if (lensEffectEnabled > 0.5) {
        vec2 fromCenter = (samplePx - lensCenterPx) / (screenResolution * 0.5);
        float k = 1.0 + LENS_DISTORT_STRENGTH * dot(fromCenter, fromCenter);
        samplePx = lensCenterPx + fromCenter * k * (screenResolution * 0.5);
    }

    vec2 uv = samplePx / screenResolution;
    float mipLevel = log2(max(cellSize, 1.0));
    vec3 c = textureLod(sceneTex, uv, mipLevel).rgb;

    float lum = luminance(c);
    vec3 colorSum = c;

    lum = pow(clamp(lum, 0.0, 1.0), 0.75);

    const float bayer[16] = float[](
        0.0,  8.0,  2.0, 10.0,
       12.0,  4.0, 14.0,  6.0,
        3.0, 11.0,  1.0,  9.0,
       15.0,  7.0, 13.0,  5.0
    );
    int bx = int(mod(fragPixel.x, 4.0));
    int by = int(mod(fragPixel.y, 4.0));
    float dither = (bayer[by * 4 + bx] / 16.0) - 0.5;
    lum = clamp(lum + dither * (1.0 / rampLength) * 1.5, 0.0, 1.0);

    float glyphIndex = floor(clamp(lum, 0.0, 0.999) * rampLength);

    // Local fragment coordinates inside the cell (0..1): they slice the glyph along the physical
    // pixel block's boundary rather than a terminal grid, since cellOrigin comes from the
    // fragment's continuous screen position.
    vec2 localUV = fract(fragPixel / cellSize);
    localUV.y = 1.0 - localUV.y;

    vec2 glyphOrigin = vec2(glyphIndex / rampLength, 0.0);
    vec2 glyphUV = glyphOrigin + vec2(localUV.x / rampLength, localUV.y);

    float mask = texture(fontTex, glyphUV).r;
    float bw = step(0.5, mask);

    // "Unreliable vision": a disappearing glyph spot with a ragged edge (per-cell radius threshold
    // 0.5x-1.3x uGlitchRadiusCells; the center stays solid). Compared in ASCII-grid cells; the CPU
    // passes a normalized position so it need not know cols/rows.
    if (uGlitchActive > 0.5)
    {
        vec2 targetCell = floor(uGlitchUV * (screenResolution / cellSize));
        float d = distance(cellIndex, targetCell);

        // A salt from uGlitchUV: each new glitch event gets its own tearing pattern instead of the
        // same frozen one.
        float edgeNoise = hash11(
            cellIndex.x * 12.9898 + cellIndex.y * 78.233
            + uGlitchUV.x * 991.0 + uGlitchUV.y * 397.0
        );
        float raggedRadius = uGlitchRadiusCells * mix(0.5, 1.3, edgeNoise);

        if (d <= raggedRadius)
            bw = 0.0;
    }

    // Glyph color in color mode: the filtered scene hue normalized by the max channel, not by
    // brightness (the glyph density already encodes brightness); otherwise dark colored areas would
    // turn pale gray.
    float maxChannel = max(max(colorSum.r, colorSum.g), colorSum.b);
    vec3 hueColor = maxChannel > 0.001 ? (colorSum / maxChannel) : vec3(1.0);
    hueColor = mix(hueColor, vec3(1.0), 0.12);

    vec3 glyphTint = mix(vec3(1.0), hueColor, colorEnabled);
    vec3 sceneColor = vec3(bw) * glyphTint;

    // Blend the regular ASCII scene with the HUD stamina bar (if it drew anything in this cell).
    // hudAlpha changes smoothly (not 0/1), so the transition is smooth too.
    vec3 finalColor = mix(sceneColor, hudColor, hudAlpha);
    finalColor = mix(finalColor, vec3(0.0), fadeAlpha);
    finalColor *= screenVignette;

    // Blood drips across the screen until HP recovers: reuses glyphs 16/17 and hash11() from the
    // stamina frame, animated by uTime and looping.
    float bloodDamage = 1.0 - clamp(healthFrac, 0.0, 1.0);
    if (bloodDamage > 0.001) {
        float totalRowsF = floor(screenResolution.y / cellSize);
        vec3 bloodColor;
        // First check for a drop in this cell's own column; otherwise this cell may be the right
        // half of a big drop from the left column (requireBig = true), giving big drops a real
        // 2-cell width.
        if (evaluateBloodDrop(cellIndex.x, cellIndex.y, totalRowsF, bloodDamage, false, fragPixel, lensR2, bloodColor)) {
            finalColor = bloodColor;
        } else if (evaluateBloodDrop(cellIndex.x - 1.0, cellIndex.y, totalRowsF, bloodDamage, true, fragPixel, lensR2, bloodColor)) {
            finalColor = bloodColor;
        }
    }

    fragColor = vec4(finalColor, 1.0);
}
