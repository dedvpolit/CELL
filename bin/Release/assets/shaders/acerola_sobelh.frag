#version 430 core
// Pass 7/9: horizontal half of the Sobel gradient over the edge mask.
in vec2 vUV;
out vec2 FragColor;
uniform sampler2D edgesTex;
uniform float texelX;
void main() {
    float l1 = texture(edgesTex, vUV - vec2(texelX, 0.0)).r;
    float l2 = texture(edgesTex, vUV).r;
    float l3 = texture(edgesTex, vUV + vec2(texelX, 0.0)).r;
    float Gx = 3.0 * l1 + 0.0 * l2 - 3.0 * l3;
    float Gy = 3.0 * l1 + 10.0 * l2 + 3.0 * l3;
    FragColor = vec2(Gx, Gy);
}
