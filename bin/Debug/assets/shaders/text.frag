#version 330 core

in vec2 vUV;
in vec3 vColor;
in float vAlpha;
in float vMode;
in vec2 vScreenPos;

out vec4 fragColor;

uniform sampler2D fontAtlas;

// Screen-position hash: stable between frames.
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main()
{
    if (vMode > 1.5)
    {
        fragColor = vec4(vColor, vAlpha);
        return;
    }

    if (vMode > 0.5)
    {
        // Corrupted word (~word~): a ragged ink stain of the word's size instead of letters.
        float n  = hash(floor(vScreenPos * 0.35));
        float n2 = hash(floor(vScreenPos * 0.12) + 17.0);
        float density = 0.55 + 0.30 * n2; // an uneven stain edge, not a perfect rectangle
        if (n > density) discard;

        fragColor = vec4(vColor * (0.55 + 0.35 * n), vAlpha);
        return;
    }

    float a = texture(fontAtlas, vUV).r;
    if (a < 0.04) discard;
    fragColor = vec4(vColor, a * vAlpha);
}
