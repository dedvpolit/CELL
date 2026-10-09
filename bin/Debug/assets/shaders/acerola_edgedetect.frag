#version 430 core
// Pass 6/9: geometric edges (depth or normal jump across the 8 neighbors) merged with the DoG mask.
in vec2 vUV;
out float FragColor;
uniform sampler2D normalsTex;
uniform sampler2D dogTex;
uniform vec2 texel;
uniform float depthThreshold;
uniform float normalThreshold;
void main() {
    vec4 c  = texture(normalsTex, vUV);
    vec4 w  = texture(normalsTex, vUV + vec2(-1, 0) * texel);
    vec4 e  = texture(normalsTex, vUV + vec2( 1, 0) * texel);
    vec4 n  = texture(normalsTex, vUV + vec2( 0,-1) * texel);
    vec4 s  = texture(normalsTex, vUV + vec2( 0, 1) * texel);
    vec4 nw = texture(normalsTex, vUV + vec2(-1,-1) * texel);
    vec4 sw = texture(normalsTex, vUV + vec2( 1,-1) * texel);
    vec4 ne = texture(normalsTex, vUV + vec2(-1, 1) * texel);
    vec4 se = texture(normalsTex, vUV + vec2( 1, 1) * texel);

    // Masked wall-torch pixels (depth -1, see acerola_normals.frag): no edges there.
    if (min(min(min(c.w, w.w), min(e.w, n.w)), min(min(s.w, nw.w), min(min(sw.w, ne.w), se.w))) < 0.0) {
        FragColor = 0.0;
        return;
    }

    float depthSum = abs(w.w - c.w) + abs(e.w - c.w) + abs(n.w - c.w) + abs(s.w - c.w)
                   + abs(nw.w - c.w) + abs(sw.w - c.w) + abs(ne.w - c.w) + abs(se.w - c.w);
    float geoEdge = (depthSum > depthThreshold) ? 1.0 : 0.0;

    vec3 normalSum = abs(w.rgb - c.rgb) + abs(e.rgb - c.rgb) + abs(n.rgb - c.rgb) + abs(s.rgb - c.rgb)
                   + abs(nw.rgb - c.rgb) + abs(sw.rgb - c.rgb) + abs(ne.rgb - c.rgb) + abs(se.rgb - c.rgb);
    if (dot(normalSum, vec3(1.0)) > normalThreshold) geoEdge = 1.0;

    float dog = texture(dogTex, vUV).r;
    FragColor = clamp(abs(dog - geoEdge), 0.0, 1.0);
}
