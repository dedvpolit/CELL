#version 430 core
// Pass 1/4: scene luminance blurred horizontally with two Gaussians (sigma 1.6 -> r, 2.56 -> g),
// the input of the Difference of Gaussians. Edges are detected on the unwarped scene; the lens is
// applied only where the result is read (acerola_ascii.comp).
out vec2 FragColor;
uniform sampler2D sceneTex;

// Normalized over the 17 taps, indexed by |offset|. Shared with acerola_blur_v_dog.frag.
const float kWeights1[9] = float[](0.24933894, 0.20510062, 0.11415569, 0.04299143, 0.01095519,
                                   0.00188891, 0.00022037, 0.00001740, 0.00000093);
const float kWeights2[9] = float[](0.15596684, 0.14451011, 0.11494659, 0.07849209, 0.04601375,
                                   0.02315695, 0.01000476, 0.00371077, 0.00118155);

// sceneTex has a mip chain, so it is read with texelFetch to skip LOD selection and filtering.
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    int maxX = textureSize(sceneTex, 0).x - 1;
    vec2 blur = vec2(0.0);
    for (int x = -8; x <= 8; ++x)
    {
        vec3 c = texelFetch(sceneTex, ivec2(clamp(p.x + x, 0, maxX), p.y), 0).rgb;
        blur += dot(c, vec3(0.2126, 0.7152, 0.0722)) * vec2(kWeights1[abs(x)], kWeights2[abs(x)]);
    }
    FragColor = blur;
}
