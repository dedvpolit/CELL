#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aType;

uniform mat4 view;
uniform mat4 projection;

uniform vec2 screenCenter;
uniform float screenScale;

out vec3 vNormal;
out vec3 vWorldPos;
out float vType;
out vec3 vLocal;

void main()
{
    // The compass is a screen-space (first-person) object: a small mesh pinned to a fixed spot on
    // the screen, so it does not drift as the camera pitches or turns. Its geometry is built flat
    // in the XZ plane (Y = thickness), so screen X/Y come from aPos.x/aPos.z, not aPos.x/aPos.y;
    // otherwise the disk (Y almost constant) collapses to a thin sliver.
    vec3 p = vec3(
        screenCenter.x + aPos.x * screenScale,
        screenCenter.y + aPos.z * screenScale,
        aPos.y * screenScale
    );

    vec3 n = aNormal;

    vNormal = n;
    vWorldPos = p;
    vType = aType;
    vLocal = aPos;

    gl_Position = projection * view * vec4(p, 1.0);
}
