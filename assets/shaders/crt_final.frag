#version 430 core
// The CRT screen: curved glass with rounded corners, glow, scanlines and a little noise.
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D phosphorTex;
uniform sampler2D bloomTex;
uniform vec2 resolution;
uniform float time;

const float kCurvature = 0.04;      // strength of the barrel bulge
const float kCornerRadius = 0.06;   // in normalized screen units
const float kGlowStrength = 0.55;
const float kScanlinePeriod = 3.0;  // pixels
const float kScanlineDepth = 0.18;
const float kNoise = 0.025;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    // Barrel warp, scaled so the corners of the frame land on the corners of the screen. Toward
    // the middle of each edge the outermost kCurvature / (1 + kCurvature) of the half-width
    // (about 2%) is not shown; the rounded corners cut a little more.
    vec2 c = vUV * 2.0 - 1.0;
    c *= (1.0 + kCurvature * c.yx * c.yx) / (1.0 + kCurvature);
    vec2 uv = c * 0.5 + 0.5;

    // Rounded rectangle mask, antialiased over about one pixel.
    vec2 q = abs(c) - (1.0 - kCornerRadius);
    float cornerDist = length(max(q, 0.0)) - kCornerRadius;
    float inside = 1.0 - smoothstep(-1.5 / resolution.y, 0.0, cornerDist);
    if (inside <= 0.0 || any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
    {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 color = texture(phosphorTex, uv).rgb + texture(bloomTex, uv).rgb * kGlowStrength;

    float line = 0.5 + 0.5 * cos(6.2831853 * uv.y * resolution.y / kScanlinePeriod);
    color *= 1.0 - kScanlineDepth * line;

    color += (hash(gl_FragCoord.xy + fract(time) * 913.0) - 0.5) * kNoise;

    // Slightly darker toward the edges, as on a real tube.
    color *= mix(0.82, 1.0, 1.0 - smoothstep(0.55, 1.4, length(c)));

    FragColor = vec4(clamp(color, 0.0, 1.0) * inside, 1.0);
}
