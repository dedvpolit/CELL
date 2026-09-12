#version 330 core

// ============================================================================
// text.vert — экранный (2D, пиксельные координаты) слой текста дневников,
// рисуемый поверх ASCII-постпроцесса тем же принципом, что и Compass (см.
// Compass.h: "рисуется своим отдельным шейдером... после AsciiEffect::end(),
// не проходит через основной ASCII-постпроцесс") — только здесь вместо 3D
// геометрии компаса просто ортографическая проекция пиксель->NDC, без
// view/projection матриц: это чисто 2D оверлей.
//
// См. TextRenderer.h за тем, откуда берутся aPos (уже в пикселях экрана,
// посчитаны на CPU через stbtt_GetBakedQuad — реальная пропорциональная
// раскладка шрифта VT323, а не сетка фиксированных клеток, как у старого
// плоского UI-шрифта в AsciiEffect.cpp).
// ============================================================================

layout(location = 0) in vec2 aPos;   // пиксель экрана, (0,0) = верхний левый угол
layout(location = 1) in vec2 aUV;    // UV в атласе шрифта (для режима "глиф")
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aAlpha;
layout(location = 4) in float aMode; // 0 = сэмплировать атлас, 1 = процедурное "чернильное пятно"

uniform vec2 screenSize;

out vec2 vUV;
out vec3 vColor;
out float vAlpha;
out float vMode;
out vec2 vScreenPos; // для процедурного шума пятна (см. text.frag)

void main()
{
    vec2 ndc = vec2(
        (aPos.x / screenSize.x) * 2.0 - 1.0,
        1.0 - (aPos.y / screenSize.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);

    vUV = aUV;
    vColor = aColor;
    vAlpha = aAlpha;
    vMode = aMode;
    vScreenPos = aPos;
}
