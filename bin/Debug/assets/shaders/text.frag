#version 330 core

in vec2 vUV;
in vec3 vColor;
in float vAlpha;
in float vMode;
in vec2 vScreenPos;

out vec4 fragColor;

uniform sampler2D fontAtlas;

// The same simple hash used across the engine for a deterministic pseudo-random pattern; it does
// not flicker between frames because the input is only the screen position, no uTime.
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
        // A corrupted diary word (~word~): instead of real letters, draw a procedural "ink stain":
        // a ragged, uneven edge (not a solid rectangle) of the same size and shape the word would
        // occupy, as if the text faded or smeared.
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
