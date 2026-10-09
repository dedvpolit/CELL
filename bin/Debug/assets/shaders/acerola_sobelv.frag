#version 430 core
// Pass 8/9: vertical half of the Sobel gradient. Output: (angle, 1 if the gradient is nonzero).
in vec2 vUV;
out vec2 FragColor;
uniform sampler2D pingTex;
uniform float texelY;
void main() {
    vec2 g1 = texture(pingTex, vUV - vec2(0.0, texelY)).rg;
    vec2 g2 = texture(pingTex, vUV).rg;
    vec2 g3 = texture(pingTex, vUV + vec2(0.0, texelY)).rg;
    float Gx = 3.0 * g1.x + 10.0 * g2.x + 3.0 * g3.x;
    float Gy = 3.0 * g1.y + 0.0 * g2.y - 3.0 * g3.y;
    vec2 G = vec2(Gx, Gy);
    bool valid = dot(G, G) > 1e-12;
    float theta = valid ? atan(normalize(G).y, normalize(G).x) : 0.0;
    FragColor = vec2(theta, valid ? 1.0 : 0.0);
}
