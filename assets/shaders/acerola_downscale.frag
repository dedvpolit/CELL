#version 430 core
// Pass 2/9: average color + luminance of each cell, one texel per cell. The average comes from the
// scene's mip chain at the level matching the cell footprint in scene texels.
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D sceneTex;
uniform float cellSize;
uniform vec2 outSize;
uniform float mipLevel;
uniform float lensStrength;
vec2 lensWarp(vec2 uv) {
    vec2 d = (uv - 0.5) * 2.0;
    return 0.5 + (uv - 0.5) * (1.0 + lensStrength * dot(d, d));
}
void main() {
    vec2 centerPx = (floor(gl_FragCoord.xy) + 0.5) * cellSize;
    vec2 uv = lensWarp(clamp(centerPx / outSize, 0.0, 1.0));
    vec3 c = textureLod(sceneTex, uv, mipLevel).rgb;
    FragColor = vec4(c, dot(c, vec3(0.2126, 0.7152, 0.0722)));
}
