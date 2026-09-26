#version 330 core

// Screen-space 2D layer (pixel coordinates) for diary text and credits, drawn after
// AsciiEffect::end() like Compass, with its own orthographic pixel -> NDC projection. aPos is
// computed on the CPU (TextRenderer.h) from stbtt_GetBakedQuad.

layout(location = 0) in vec2 aPos;   // screen pixel, (0,0) = top-left corner
layout(location = 1) in vec2 aUV;    // UV into the font atlas (for "glyph" mode)
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aAlpha;
layout(location = 4) in float aMode; // 0 = sample the atlas, 1 = procedural "ink stain"

uniform vec2 screenSize;

out vec2 vUV;
out vec3 vColor;
out float vAlpha;
out float vMode;
out vec2 vScreenPos; // for the stain's procedural noise (see text.frag)

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
