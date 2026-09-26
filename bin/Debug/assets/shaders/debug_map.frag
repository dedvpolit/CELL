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
        // uMapTex is RGBA8: R is 1.0 = wall / 0.0 = floor; G is the cell's corner-cut type
        // (WallShapes::CornerCut: 0 = None, 1 = SW, 2 = SE, 3 = NE, 4 = NW); B is whether this
        // floor cell became floor through corridor widening; A is whether this cut was forced as
        // part of a diagonal chain.
        vec4 cellData = texture(uMapTex, vUV);
        float wall = cellData.r;
        int cutType = int(round(cellData.g * 255.0));
        float widened = cellData.b;
        float diagonalChain = cellData.a;

        vec3 floorColor = vec3(0.10, 0.10, 0.13);
        vec3 wallColor  = vec3(1.0, 1.0, 1.0);
        vec3 chamferColor = vec3(1.0, 0.65, 0.15); // orange — a regular (random) chamfer
        // A chamfer forced by a diagonal chain gets its own brighter, cooler color: otherwise a
        // chain would be indistinguishable from scattered random chamfers although it is a
        // qualitatively different thing.
        vec3 diagonalChainColor = vec3(1.0, 0.15, 0.85); // magenta
        // A widened corridor is its own floor kind, distinct from originally-floor cells: otherwise
        // corridor width could not be told apart from the maze's normal width on the debug map.
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
