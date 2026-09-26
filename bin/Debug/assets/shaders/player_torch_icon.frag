#version 330 core

in vec2 vUV;
out vec4 fragColor;

uniform float uTime;

void main()
{
    vec3 handleColor = vec3(0.34, 0.25, 0.16);
    vec3 flameColorLow  = vec3(1.0, 0.82, 0.20);
    vec3 flameColorHigh = vec3(1.0, 0.30, 0.035);

    vec3 color = vec3(0.0);
    float alpha = 0.0;

    // ---- Рукоять: тонкий вертикальный прямоугольник в нижней части ----
    const float handleHalfWidth = 0.07;
    const float handleTop = 0.55;
    if (abs(vUV.x - 0.5) < handleHalfWidth && vUV.y < handleTop)
    {
        color = handleColor;
        alpha = 1.0;
    }

    // ---- Пламя: покачивающийся мерцающий "блоб" в верхней части ----
    // Та же идея мерцания (несколько наложенных синусов разной частоты),
    // что и у обычных факелов/частиц (см. scene.frag) — для визуальной
    // консистентности стиля.
    float flicker =
        0.85
        + 0.10 * sin(uTime * 9.0)
        + 0.05 * sin(uTime * 23.0 + 1.7);

    float wobbleX = sin(uTime * 6.0 + 0.4) * 0.02;

    vec2 flameCenter = vec2(0.5 + wobbleX, 0.70);
    float flameHalfHeight = 0.24 * flicker;
    float flameHalfWidth  = 0.15;

    vec2 d = vUV - flameCenter;
    d.x /= flameHalfWidth;
    d.y /= flameHalfHeight;
    float dist = length(d);

    if (dist < 1.0)
    {
        // t: 0 у основания пламени, 1 у макушки — даёт градиент
        // жёлтый-снизу -> оранжевый/красный-сверху, как у обычных факелов.
        float t = clamp((vUV.y - (flameCenter.y - flameHalfHeight)) / (2.0 * flameHalfHeight), 0.0, 1.0);
        vec3 fireColor = mix(flameColorLow, flameColorHigh, t);

        color = fireColor * (1.15 * flicker);
        alpha = 1.0;
    }

    if (alpha < 0.01)
        discard;

    fragColor = vec4(color, alpha);
}
