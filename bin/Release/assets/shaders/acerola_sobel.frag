#version 430 core
// Pass 4/4: Sobel gradient of the edge mask, quantized to a stroke direction.
// Output: 0 = no gradient, otherwise (direction + 1) / 4 with direction 0 = '|', 1 = '-',
// 2 = '\', 3 = '/'.
in vec2 vUV;
out float FragColor;
uniform sampler2D edgesTex;

const float PI = 3.14159265;

void main()
{
    float bl = textureOffset(edgesTex, vUV, ivec2(-1, -1)).r;
    float b  = textureOffset(edgesTex, vUV, ivec2( 0, -1)).r;
    float br = textureOffset(edgesTex, vUV, ivec2( 1, -1)).r;
    float l  = textureOffset(edgesTex, vUV, ivec2(-1,  0)).r;
    float c  = texture(edgesTex, vUV).r;
    float r  = textureOffset(edgesTex, vUV, ivec2( 1,  0)).r;
    float tl = textureOffset(edgesTex, vUV, ivec2(-1,  1)).r;
    float t  = textureOffset(edgesTex, vUV, ivec2( 0,  1)).r;
    float tr = textureOffset(edgesTex, vUV, ivec2( 1,  1)).r;

    // AcerolaFX kernels: [3 10 3] smoothing, [3 0 -3] derivative.
    float gx = 3.0 * (3.0 * (bl - br)) + 10.0 * (3.0 * (l - r)) + 3.0 * (3.0 * (tl - tr));
    float gy = 3.0 * (3.0 * bl + 10.0 * b + 3.0 * br) - 3.0 * (3.0 * tl + 10.0 * t + 3.0 * tr);

    if (gx * gx + gy * gy <= 1e-12)
    {
        FragColor = 0.0;
        return;
    }

    float theta = atan(gy, gx);
    float absTheta = abs(theta) / PI;
    int direction;
    if (absTheta < 0.05 || absTheta > 0.9)       direction = 0;
    else if (absTheta > 0.45 && absTheta < 0.55) direction = 1;
    else if (absTheta < 0.45)                    direction = theta > 0.0 ? 2 : 3;
    else                                         direction = theta > 0.0 ? 3 : 2;
    FragColor = float(direction + 1) / 4.0;
}
