#version 330 core

// Проход 1 — ПЕРЕДЕЛАНО: вместо Sobel по яркости (первая производная,
// давала ложные "кружки" вокруг ярких точечных объектов вроде факела —
// см. большой комментарий в TorchAsciiEffect.h) теперь КРИВИЗНА глубины
// (вторая производная, дискретный Лаплас) — метод из уже проверенной
// рабочей версии этого же эффекта. Считается СРАЗУ на уровне ASCII-
// ЯЧЕЙКИ (это render target разрешением windowSize/cellSize — каждый
// вызов этого шейдера — это и есть одна ячейка целиком), поэтому
// отдельного прохода с голосованием по 8x8 пикселям, как раньше, больше
// не нужно вообще.

out vec4 fragColor; // R = направление+1 (0=нет края,1='|',2='-',3='/',4='\'), G = покрытие маской (0..1)

uniform sampler2D sceneTex;      // цвет — не используется здесь, оставлено для единообразия сигнатур
uniform sampler2D maskTex;       // см. scene.frag: fragTorchMask
uniform sampler2D sceneDepthTex; // настоящая depth-текстура (см. AsciiEffect::sceneDepthTexture())

uniform vec2 screenResolution;
uniform float cellSize;
uniform float camNear;
uniform float camFar;
uniform float edgeDepthThreshold; // мировые юниты — см. TorchAsciiEffect.cpp за пояснением масштаба

float linearizeDepth(float d, float nearP, float farP)
{
    float z = d * 2.0 - 1.0;
    return (2.0 * nearP * farP) / (farP + nearP - z * (farP - nearP));
}

// Среднее по маске (0..1) в этой ASCII-ячейке — по нескольким точкам, а
// не одной, чтобы клетки ровно на границе силуэта факела не терялись.
float cellMaskAt(vec2 cellIdx)
{
    vec2 base = cellIdx * cellSize;
    float m = 0.0;
    const int NS = 2;
    for (int y = 0; y < NS; y++)
        for (int x = 0; x < NS; x++)
        {
            vec2 offset = (vec2(x, y) + 0.5) / float(NS) * cellSize;
            m += texture(maskTex, (base + offset) / screenResolution).r;
        }
    return m / float(NS * NS);
}

// Средняя ЛИНЕАРИЗОВАННАЯ глубина в произвольной ASCII-ячейке (не своей
// — используется для соседей при расчёте кривизны). 2x2-усреднение
// гасит мелкий шум (микрорельеф стен/факела в scene.frag), как и в
// проверенной версии.
float cellDepthAt(vec2 cellIdx)
{
    vec2 base = cellIdx * cellSize;
    float d = 0.0;
    const int NS = 2;
    for (int y = 0; y < NS; y++)
        for (int x = 0; x < NS; x++)
        {
            vec2 offset = (vec2(x, y) + 0.5) / float(NS) * cellSize;
            d += linearizeDepth(texture(sceneDepthTex, (base + offset) / screenResolution).r, camNear, camFar);
        }
    return d / float(NS * NS);
}

void main()
{
    ivec2 cellCoordI = ivec2(gl_FragCoord.xy);
    vec2 cellCoord = vec2(cellCoordI);

    float coverage = cellMaskAt(cellCoord);

    if (coverage < 0.1)
    {
        fragColor = vec4(0.0, coverage, 0.0, 1.0);
        return;
    }

    // ---- Кривизна глубины (см. большой комментарий в TorchAsciiEffect.h
    // за тем, почему вторая производная, а не первая) ----
    float centerDepth = cellDepthAt(cellCoord);
    float dtl = cellDepthAt(cellCoord + vec2(-1.0, -1.0));
    float dt  = cellDepthAt(cellCoord + vec2( 0.0, -1.0));
    float dtr = cellDepthAt(cellCoord + vec2( 1.0, -1.0));
    float dl  = cellDepthAt(cellCoord + vec2(-1.0,  0.0));
    float dr  = cellDepthAt(cellCoord + vec2( 1.0,  0.0));
    float dbl = cellDepthAt(cellCoord + vec2(-1.0,  1.0));
    float db  = cellDepthAt(cellCoord + vec2( 0.0,  1.0));
    float dbr = cellDepthAt(cellCoord + vec2( 1.0,  1.0));

    float curveX  = dl  - 2.0 * centerDepth + dr;  // излом по X  -> '|'
    float curveY  = dt  - 2.0 * centerDepth + db;  // излом по Y  -> '-'
    float curveD1 = dtl - 2.0 * centerDepth + dbr; // диагональ  -> '\'
    float curveD2 = dtr - 2.0 * centerDepth + dbl; // диагональ  -> '/'

    float aX = abs(curveX), aY = abs(curveY), aD1 = abs(curveD1), aD2 = abs(curveD2);
    float mag = max(max(aX, aY), max(aD1, aD2));

    int mode = 0; // 0 = нет края
    if (mag > edgeDepthThreshold)
    {
        if (mag == aX) mode = 1;      // '|'
        else if (mag == aY) mode = 2; // '-'
        else if (mag == aD2) mode = 3; // '/'
        else mode = 4;                 // '\'
    }

    fragColor = vec4(float(mode), coverage, 0.0, 1.0);
}
