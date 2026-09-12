#version 330 core

in vec2 vUV;
in vec3 vColor;
in float vAlpha;
in float vMode;
in vec2 vScreenPos;

out vec4 fragColor;

uniform sampler2D fontAtlas;

// Тот же простой hash, что используется по всему движку для
// детерминированного (не мерцающего по кадрам, т.к. вход — только
// экранная позиция клетки, без uTime) псевдослучайного узора — см.
// аналогичные hash() в scene.vert/ascii_post.frag.
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main()
{
    if (vMode > 1.5)
    {
        // Сплошная заливка (подсветка выбранной строки журнала и т.п.) —
        // без шума, без атласа, просто цвет*alpha. См. TextRenderer::drawRect().
        fragColor = vec4(vColor, vAlpha);
        return;
    }

    if (vMode > 0.5)
    {
        // Испорченное слово дневника (см. Diaries.h: ~слово~) — вместо
        // реальных букв рисуем процедурное "чернильное пятно": рваный,
        // неровный край (не сплошной прямоугольник) той же формы/размера,
        // что занимало бы слово, будто текст выцвел/расплылся. Тот же
        // смысл, что раньше давал PickDenseGlyph() (шумовой узор из
        // плотных ASCII-символов) в старой сетке — здесь эквивалент на
        // новом слое, не завязанный на старый битмап-атлас AsciiEffect.
        float n  = hash(floor(vScreenPos * 0.35));
        float n2 = hash(floor(vScreenPos * 0.12) + 17.0);
        float density = 0.55 + 0.30 * n2; // неровная граница пятна, не идеальный прямоугольник
        if (n > density) discard;

        fragColor = vec4(vColor * (0.55 + 0.35 * n), vAlpha);
        return;
    }

    float a = texture(fontAtlas, vUV).r;
    if (a < 0.04) discard;
    fragColor = vec4(vColor, a * vAlpha);
}
