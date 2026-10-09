#version 430 core
// Pass 3/9: horizontal half of two Gaussian blurs of the luminance (sigma1 in r, sigma2 in g),
// the input of the Difference of Gaussians in pass 4.
in vec2 vUV;
out vec2 FragColor;
uniform sampler2D lumTex;
uniform float texelX;
uniform float sigma1;
uniform float sigma2;
const float PI = 3.14159265;
float gauss(float s, float x) { return (1.0 / sqrt(2.0 * PI * s * s)) * exp(-(x * x) / (2.0 * s * s)); }
void main() {
    vec2 blur = vec2(0.0);
    vec2 ksum = vec2(0.0);
    for (int x = -8; x <= 8; ++x) {
        float l = texture(lumTex, vUV + vec2(float(x) * texelX, 0.0)).r;
        vec2 g = vec2(gauss(sigma1, float(x)), gauss(sigma2, float(x)));
        blur += l * g;
        ksum += g;
    }
    FragColor = blur / ksum;
}
