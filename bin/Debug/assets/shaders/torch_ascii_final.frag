#version 330 core

// Проход 2 (финальный) — ПЕРЕДЕЛАНО: раньше здесь были два PNG-атласа
// AcerolaFX (edgesASCII.png/fillASCII.png). Теперь — только направленные
// края (см. torch_ascii_edges.frag), процедурный 4-глифовый шрифт
// ('|','-','/','\'), сгенерированный на CPU (см. TorchAsciiEffect.cpp,
// тем же приёмом, что и основной ASCII-шрифт движка) — без внешних PNG
// вообще. Композит поверх уже готового кадра, с альфа-блендингом,
// ТОЛЬКО там, где maskTex говорит "это факел".

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D maskTex;
uniform sampler2D cellInfoTex;   // результат Прохода 1, разрешение (окно/cellSize)
uniform sampler2D edgeFontTex;   // процедурный атлас 4 глифов ('|','-','/','\')

uniform vec2 screenResolution;
uniform float cellSize;
uniform float edgeGlyphCount; // = 4.0
uniform vec3 asciiColor;
// 0 = чёрно-белый режим (см. Settings -> COLOR), 1 = цветной — так
// символы факела корректно подчиняются общему переключателю цвета, а не
// всегда цветные независимо от него.
uniform float colorEnabled;
// БАГФИКС ("ASCII-штрихи факела видны поверх чёрного экрана во время
// фейда") — этот проход рисовался с фиксированной alpha=1.0 независимо
// от общего fadeAlpha экрана (см. AsciiEffect::setFadeAlpha() — тот
// затемняет ОСНОВНОЙ ASCII-рендер сцены, но не знал об этом отдельном
// проходе, который композитится ПОСЛЕ него, поверх уже готового кадра).
// Штрихи "прорезали" даже полностью чёрный экран во время смерти/фейда
// в меню. Теперь alpha домножается на (1 - fadeAlpha) — та же логика
// затухания, что и у остального экрана.
uniform float fadeAlpha;

void main()
{
    float mask = texture(maskTex, vUV).r;
    if (mask < 0.5)
        discard;
    if (fadeAlpha >= 1.0)
        discard; // полностью чёрный экран — штрихам всё равно не через что "просвечивать"

    vec2 fragPixel = vUV * screenResolution;
    ivec2 cellCoord = ivec2(floor(fragPixel / cellSize));
    vec4 cellInfo = texelFetch(cellInfoTex, cellCoord, 0);

    int mode = int(cellInfo.r + 0.5); // 0 = нет края, 1..4 = направление

    if (mode <= 0)
        discard; // нет края в этой ячейке — пусть виден обычный ASCII-рендер под этим слоем

    vec2 localPix = mod(fragPixel, cellSize);
    // Атлас всегда 1 глиф = cellSize x cellSize пикселей (генерируется
    // ровно под текущий cellSize, см. TorchAsciiEffect::create()), так
    // что здесь никакого масштабирования не нужно вообще.
    float glyphIndex = float(mode - 1);
    vec2 uv = vec2(
        (glyphIndex + localPix.x / cellSize) / edgeGlyphCount,
        1.0 - localPix.y / cellSize
    );

    float glyphValue = texture(edgeFontTex, uv).r;
    if (glyphValue < 0.5)
        discard;

    vec3 bwColor = vec3(1.0); // белый штрих в ч/б режиме — как и обычный ASCII-рендер сцены
    vec3 outColor = mix(bwColor, asciiColor, colorEnabled);

    fragColor = vec4(outColor, 1.0 - fadeAlpha);
}
