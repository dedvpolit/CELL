#version 430 core
// AcerolaAscii pass 1/9 (see AcerolaAscii.h): scene luminance. Edge detection (passes 1, 3-8) runs on
// the unwarped scene; the lens is applied only where the result is read (passes 2 and 9).
in vec2 vUV;
out float FragColor;
uniform sampler2D sceneTex;
void main() {
    vec3 c = texture(sceneTex, vUV).rgb;
    FragColor = dot(c, vec3(0.2126, 0.7152, 0.0722));
}
