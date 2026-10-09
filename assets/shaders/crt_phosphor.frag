#version 430 core
// Phosphor persistence: a bright pixel fades out over a few frames instead of vanishing.
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D currentTex;
uniform sampler2D previousTex;
uniform float decay; // per-frame factor, 0 = no afterglow

void main()
{
    vec3 current = texture(currentTex, vUV).rgb;
    // The small offset lets 8-bit values reach zero instead of sticking at 1/255.
    vec3 previous = max(texture(previousTex, vUV).rgb * decay - 1.0 / 255.0, 0.0);
    FragColor = vec4(max(current, previous), 1.0);
}
