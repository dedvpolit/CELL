#version 430 core
// Pass 4/9: vertical half of the blurs, then Difference of Gaussians thresholded to a 0/1 edge mask.
in vec2 vUV;
out float FragColor;
uniform sampler2D pingTex;
uniform float texelY;
uniform float sigma1;
uniform float sigma2;
uniform float tau;
uniform float threshold;
const float PI = 3.14159265;
float gauss(float s, float x) { return (1.0 / sqrt(2.0 * PI * s * s)) * exp(-(x * x) / (2.0 * s * s)); }
void main() {
    vec2 blur = vec2(0.0);
    vec2 ksum = vec2(0.0);
    for (int y = -8; y <= 8; ++y) {
        vec2 g2 = texture(pingTex, vUV + vec2(0.0, float(y) * texelY)).rg;
        vec2 g = vec2(gauss(sigma1, float(y)), gauss(sigma2, float(y)));
        blur += g2 * g;
        ksum += g;
    }
    blur /= ksum;
    float D = blur.x - tau * blur.y;
    FragColor = (D >= threshold) ? 1.0 : 0.0;
}
