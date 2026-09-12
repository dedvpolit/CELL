#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uMapTex;
uniform int   uMode;  // 0 = сэмплировать карту, иначе = плоский цвет
uniform vec3  uColor;
uniform float uAlpha;

void main()
{
    if (uMode == 0)
    {
        // uMapTex теперь RGBA8 (см. MinimapFog::uploadMapTexture): R —
        // как раньше, 1.0=стена/0.0=пол; G — тип среза угла клетки (Шаг
        // 1, см. WallShapes::CornerCut: 0=None,1=SW,2=SE,3=NE,4=NW); B —
        // стала ли эта floor-клетка полом именно от расширения коридора
        // (CorridorWidth); A — форсирован ли этот срез как часть
        // диагональной "лестницы" (DiagonalCorridors, см. историю правок).
        vec4 cellData = texture(uMapTex, vUV);
        float wall = cellData.r;
        int cutType = int(round(cellData.g * 255.0));
        float widened = cellData.b;
        float diagonalChain = cellData.a;

        vec3 floorColor = vec3(0.10, 0.10, 0.13);
        vec3 wallColor  = vec3(1.0, 1.0, 1.0);
        // Срезанные углы подсвечены отдельным цветом — иначе на
        // debug-карте их не отличить от обычной прямой стены (см.
        // историю правок: раньше карта показывала только бит "стена/не
        // стена", а срезы/колонны были невидимы).
        vec3 chamferColor = vec3(1.0, 0.65, 0.15); // оранжевый — обычный (случайный) срез
        // Срез, форсированный диагональной цепочкой — отдельный, ярче и
        // "холоднее" цвет: без этого цепочку среза не отличить на глаз
        // от рассыпанных по карте случайных одиночных срезов, хотя это
        // качественно другая вещь (см. DiagonalCorridors.h).
        vec3 diagonalChainColor = vec3(1.0, 0.15, 0.85); // маджента
        // Расширенный коридор — своя floor-клетка, но отличная от
        // "изначально было полом" (CorridorWidth, см. историю правок:
        // без этого ширину коридора физически невозможно было бы
        // отличить от штатной ширины лабиринта на debug-карте).
        vec3 widenedFloorColor = vec3(0.30, 0.65, 1.0); // яркий голубой

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
