#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uMapTex;
uniform int   uMode;  // 0 = sample the map, otherwise = flat color
uniform vec3  uColor;
uniform float uAlpha;

void main()
{
    if (uMode == 0)
    {
        // uMapTex RGBA8: R wall, G corner-cut type (0 None, 1 SW, 2 SE, 3 NE, 4 NW), B widened
        // corridor, A diagonal chain.
        vec4 cellData = texture(uMapTex, vUV);
        float wall = cellData.r;
        int cutType = int(round(cellData.g * 255.0));
        float widened = cellData.b;
        float diagonalChain = cellData.a;

        vec3 floorColor = vec3(0.10, 0.10, 0.13);
        vec3 wallColor  = vec3(1.0, 1.0, 1.0);
        vec3 chamferColor = vec3(1.0, 0.65, 0.15); // orange: a regular (random) chamfer
        // Chain chamfers get their own color.
        vec3 diagonalChainColor = vec3(1.0, 0.15, 0.85); // magenta
        // Widened corridors get their own floor color.
        vec3 widenedFloorColor = vec3(0.30, 0.65, 1.0); // bright cyan

        vec3 c = mix(floorColor, wallColor, wall);
        if (wall > 0.5 && cutType != 0)
        {
            c = (diagonalChain > 0.5) ? diagonalChainColor : chamferColor;
        }
        else if (wall < 0.5 && widened > 0.5)
        {
            c = widenedFloorColor;
        }
        FragColor = vec4(c, uAlpha);
    }
    else
    {
        FragColor = vec4(uColor, uAlpha);
    }
}
