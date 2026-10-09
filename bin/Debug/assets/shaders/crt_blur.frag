#version 430 core
// One direction of the glow blur; texelStep is one destination texel along that direction.
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D sourceTex;
uniform vec2 texelStep;

const float kWeights[5] = float[](0.227027, 0.194595, 0.121622, 0.054054, 0.016216);

void main()
{
    vec3 sum = texture(sourceTex, vUV).rgb * kWeights[0];
    for (int i = 1; i < 5; ++i)
    {
        sum += texture(sourceTex, vUV + texelStep * float(i)).rgb * kWeights[i];
        sum += texture(sourceTex, vUV - texelStep * float(i)).rgb * kWeights[i];
    }
    FragColor = vec4(sum, 1.0);
}
