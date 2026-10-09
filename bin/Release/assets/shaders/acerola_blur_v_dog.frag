#version 430 core
// Pass 2/4: vertical half of both blurs, then the Difference of Gaussians thresholded to a 0/1 mask.
in vec2 vUV;
out float FragColor;
uniform sampler2D blurTex;
uniform vec2 texel;
uniform float tau;
uniform float threshold;

const float kWeights1[9] = float[](0.24933894, 0.20510062, 0.11415569, 0.04299143, 0.01095519,
                                   0.00188891, 0.00022037, 0.00001740, 0.00000093);
const float kWeights2[9] = float[](0.15596684, 0.14451011, 0.11494659, 0.07849209, 0.04601375,
                                   0.02315695, 0.01000476, 0.00371077, 0.00118155);

void main()
{
    vec2 blur = vec2(0.0);
    for (int y = -8; y <= 8; ++y)
        blur += texture(blurTex, vUV + vec2(0.0, float(y) * texel.y)).rg * vec2(kWeights1[abs(y)], kWeights2[abs(y)]);
    FragColor = (blur.x - tau * blur.y >= threshold) ? 1.0 : 0.0;
}
