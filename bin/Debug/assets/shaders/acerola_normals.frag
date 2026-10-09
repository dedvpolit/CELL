#version 430 core
// Pass 5/9: view-space normal reconstructed from depth by finite differences, one scene texel apart.
// Output: (normal.xyz, linear depth).
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D depthTex;
uniform sampler2D sceneTex;       // alpha 0 = wall torch flame/handle (scene.frag)
uniform vec2 sceneTexel;
uniform float nearPlane;
uniform float farPlane;
float linearizeDepth(float d) {
    float z = 2.0 * d - 1.0;
    return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - z * (farPlane - nearPlane));
}
void main() {
    vec2 pc = vUV;
    // Wall torches (scene.frag alpha 0): their flat-shaded faces would be traced as strokes, so they
    // get depth -1, which the edge-detect pass treats as "no edges here".
    if (texture(sceneTex, pc).a < 0.5) {
        FragColor = vec4(0.0, 0.0, 1.0, -1.0);
        return;
    }
    vec2 pn = pc - vec2(0.0, sceneTexel.y);
    vec2 pe = pc + vec2(sceneTexel.x, 0.0);
    float dc = linearizeDepth(texture(depthTex, pc).r);
    float dn = linearizeDepth(texture(depthTex, pn).r);
    float de = linearizeDepth(texture(depthTex, pe).r);
    vec3 vc = vec3(pc - 0.5, 1.0) * dc;
    vec3 vn = vec3(pn - 0.5, 1.0) * dn;
    vec3 ve = vec3(pe - 0.5, 1.0) * de;
    FragColor = vec4(normalize(cross(vc - vn, vc - ve)), dc);
}
