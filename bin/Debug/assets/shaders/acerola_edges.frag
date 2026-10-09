#version 430 core
// Pass 3/4: geometric edges merged with the DoG mask. A pixel is a geometric edge when depth or the
// view-space normal (reconstructed from depth by finite differences) jumps across its 8 neighbors.
in vec2 vUV;
out float FragColor;
uniform sampler2D depthTex;
uniform sampler2D sceneTex;   // alpha 0 = wall torch flame/handle; has mips, so texelFetch
uniform sampler2D dogTex;
uniform vec2 texel;
uniform float nearPlane;
uniform float farPlane;
uniform float depthThreshold;
uniform float normalThreshold;

float linearDepth(vec2 offset)
{
    float d = texture(depthTex, vUV + offset * texel).r * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - d * (farPlane - nearPlane));
}

void main()
{
    // Wall torches: their flat-shaded faces would be traced as strokes.
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 maxP = textureSize(sceneTex, 0) - 1;
    float torchMask = 1.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            torchMask = min(torchMask, texelFetch(sceneTex, clamp(p + ivec2(x, y), ivec2(0), maxP), 0).a);
    if (torchMask < 0.5)
    {
        FragColor = 0.0;
        return;
    }

    // The normal at q needs depth at q, q - (0,1) and q + (1,0), so a 3x3 block of normals reads a
    // 4x4 block of depths: x in [-1, 2], y in [-2, 1].
    float depth[16];
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            depth[y * 4 + x] = linearDepth(vec2(x - 1, y - 2));

    float depthSum = 0.0;
    float normalSum = 0.0;
    vec3 normals[9];
    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            int c = (y + 1) * 4 + x;
            vec2 uv = vUV + vec2(x - 1, y - 1) * texel;
            vec3 vc = vec3(uv - 0.5, 1.0) * depth[c];
            vec3 vn = vec3(uv - vec2(0.0, texel.y) - 0.5, 1.0) * depth[c - 4];
            vec3 ve = vec3(uv + vec2(texel.x, 0.0) - 0.5, 1.0) * depth[c + 1];
            normals[y * 3 + x] = normalize(cross(vc - vn, vc - ve));
            depthSum += abs(depth[c] - depth[6]);
        }
    }
    for (int i = 0; i < 9; ++i)
    {
        vec3 d = abs(normals[i] - normals[4]);
        normalSum += d.x + d.y + d.z;
    }

    float geoEdge = (depthSum > depthThreshold || normalSum > normalThreshold) ? 1.0 : 0.0;
    FragColor = abs(texture(dogTex, vUV).r - geoEdge);
}
