#version 330 core

// ============================================================================
// enemy.frag — освещение врага той же физикой, что и у остальной сцены
// (см. scene.frag): полный тест тени от настенных факелов + дешёвый свет
// в руке игрока + туман. Код теста тени скопирован ОТСЮДА ЖЕ (ShaderLoader
// не поддерживает #include — см. его комментарий), а не переписан заново
// — при правке формул освещения в scene.frag стоит проверить, не разошлось
// ли что-то здесь тоже.
//
// В отличие от стен/пола, у врага нет текстуры — цвет приходит прямо с
// вершины (aColor из glTF-материала, см. SkinnedModel.cpp), поэтому
// baseColor/detail из scene.frag здесь просто заменены на vColor/1.0.
// ============================================================================

in vec3 vNormal;
in vec3 vColor;
in vec3 vWorldPos;
in vec2 vUV;

out vec4 fragColor;

uniform vec3 camPos;
uniform float uTime;

uniform vec3 playerLightPos;
uniform vec3 playerLightDir;
uniform vec3 playerLightColor;
uniform float playerLightIntensity;

// Dev-tools: статичные направленные фонарики (клавиша L) — та же
// формула, что и в scene.frag (см. большой комментарий там про cone/
// затухание/цвет).
uniform vec3 devLightPos[8];
uniform vec3 devLightDir[8];
uniform int devLightCount;

// Dev-tools: перебор цветовых гамм окружения (клавиша G, см. большой
// комментарий у devPaletteOverride в scene.frag) — у врага нет
// стена/пол, так что тут не подмена, а ТОНИРОВАНИЕ: домножаем
// собственный цвет модели на отношение "новая гамма / базовая" — та же
// формула, что и у тонировки текстуры стен в scene.frag, просто вместо
// подмены (нечем — у модели нет отдельной "архитектуры") сдвигаем тон
// в ту же сторону, что и вся остальная сцена, сохраняя собственную
// светотень модели.
uniform int devPaletteOverride;
const vec3 kDevPaletteWall[5] = vec3[5](
    vec3(0.55, 0.35, 0.25),
    vec3(0.30, 0.33, 0.38),
    vec3(0.28, 0.38, 0.24),
    vec3(0.42, 0.40, 0.38),
    vec3(0.50, 0.28, 0.22)
);

uniform vec3 torchPos[32];
uniform vec3 torchColor[32];
uniform float torchIntensity[32];
uniform int torchCount;

uniform sampler2D mapTex;

#define MAX_COLUMNS 16
uniform vec2 columnPos[MAX_COLUMNS];
uniform int columnCount;
uniform float columnRadius;

uniform float renderDistance;

// Диффузная текстура модели (см. SkinnedModel::hasDiffuseTexture()) —
// hasDiffuseTex=false, если модель без текстуры (тогда используется
// только vColor, как раньше).
uniform sampler2D diffuseTex;
uniform float hasDiffuseTex;

const vec2 MAP_SIZE = vec2(128.0);

bool isLocalPointSolidGLSL(float lx, float lz, int cut, float chamferSize)
{
    if (cut == 1) return !(lx + lz < chamferSize);
    if (cut == 2) return !((1.0 - lx) + lz < chamferSize);
    if (cut == 3) return !((1.0 - lx) + (1.0 - lz) < chamferSize);
    if (cut == 4) return !(lx + (1.0 - lz) < chamferSize);
    return true;
}

bool shadowedByWall(
    vec3 from,
    vec3 to
)
{
    vec2 rayStart =
        from.xz;

    vec2 delta =
        to.xz
        - rayStart;

    float dist =
        length(delta);

    if (dist < 0.15)
        return false;

    vec2 dir =
        delta / dist;

    // Небольшой отступ от начала и конца отрезка: не даём лучу
    // самозатенять свою же клетку у поверхности и не спотыкаемся
    // о клетку, в которой сидит сам источник света.
    float startT =
        min(0.05, dist * 0.5);

    float endT =
        max(
            startT,
            dist - 0.05
        );

    vec2 p0 =
        rayStart + dir * startT;

    vec2 p1 =
        rayStart + dir * endT;

    // Колонны (см. комментарий у uniform columnPos выше) — точный
    // ray-vs-circle тест по всему отрезку [p0,p1], независимый от грида
    // mapTex вообще (та же идея, что и circle-vs-square коллизия игрока
    // в PlayerController.cpp — колонна не описывается сеткой, поэтому и
    // тест для неё отдельный, не привязанный к клеткам).
    for (int ci = 0; ci < columnCount; ++ci)
    {
        vec2 seg = p1 - p0;
        float segLenSq = dot(seg, seg);
        float t = segLenSq > 1e-9 ? clamp(dot(columnPos[ci] - p0, seg) / segLenSq, 0.0, 1.0) : 0.0;
        vec2 closest = p0 + seg * t;
        if (length(columnPos[ci] - closest) < columnRadius)
            return true;
    }

    ivec2 mapMax =
        ivec2(MAP_SIZE) - ivec2(1);

    ivec2 cell =
        ivec2(floor(p0));

    ivec2 endCell =
        ivec2(floor(p1));

    // --------------------------------------------------------
    // Грид-трассировка (Amanatides & Woo DDA): вместо того чтобы
    // сэмплировать луч в нескольких точках через равные интервалы
    // (что при малом числе шагов или диагональных углах пропускает
    // тонкие стены и даёт "кубчатые"/рваные границы теней), идём
    // строго от клетки к клетке вдоль луча — так что ни одна
    // клетка на пути от поверхности до источника света не может
    // быть пропущена, независимо от расстояния или угла.
    // --------------------------------------------------------

    ivec2 stepDir =
        ivec2(
            dir.x > 0.0 ? 1 : (dir.x < 0.0 ? -1 : 0),
            dir.y > 0.0 ? 1 : (dir.y < 0.0 ? -1 : 0)
        );

    const float BIG =
        1.0e8;

    float tDeltaX =
        (abs(dir.x) > 1e-6) ? abs(1.0 / dir.x) : BIG;

    float tDeltaY =
        (abs(dir.y) > 1e-6) ? abs(1.0 / dir.y) : BIG;

    float nextBoundaryX =
        (stepDir.x > 0) ? float(cell.x + 1) : float(cell.x);

    float nextBoundaryY =
        (stepDir.y > 0) ? float(cell.y + 1) : float(cell.y);

    float tMaxX =
        (abs(dir.x) > 1e-6)
            ? (nextBoundaryX - p0.x) / dir.x
            : BIG;

    float tMaxY =
        (abs(dir.y) > 1e-6)
            ? (nextBoundaryY - p0.y) / dir.y
            : BIG;

    // Достаточно с запасом клеток по диагонали дальности света
    // (свет ограничен ~8 юнитами -> максимум ~23 клетки по диагонали).
    const int MAX_STEPS = 48;

    // t-параметр входа в ТЕКУЩУЮ клетку вдоль [p0,p1] (0 — старт луча).
    // Обновляется в конце каждой итерации на t выхода из клетки (см.
    // ниже) — нужен только срезанным углам, чтобы найти, в какой именно
    // точке внутри клетки луч её пересекает (см. cutType!=0 ниже).
    float tEnter = 0.0;

    for (
        int i = 0;
        i < MAX_STEPS;
        ++i
    )
    {
        if (
            cell.x < 0 ||
            cell.y < 0 ||
            cell.x > mapMax.x ||
            cell.y > mapMax.y
        )
        {
            return true;
        }

        vec4 cellData =
            texelFetch(
                mapTex,
                cell,
                0
            );

        float tExit = min(tMaxX, tMaxY);

        if (cellData.r > 0.5)
        {
            int cutType = int(round(cellData.g * 255.0));
            if (cutType == 0)
            {
                // Обычная сплошная клетка — как и раньше.
                return true;
            }
            else
            {
                // Срезанный угол (WallShapes) — вся клетка НЕ сплошная,
                // проверяем математикой Шага 1 в той точке, где луч
                // фактически проходит через эту клетку (середина
                // отрезка [tEnter,tExit] внутри неё), а не всю клетку
                // огулом — иначе тень так и осталась бы квадратной (см.
                // историю правок).
                //
                // ВАЖНО: cellData.a — тот же флаг diagonalChainMask,
                // что уже используется на CPU
                // (MinimapFog::uploadMapTexture()) — 1.0, если эта
                // клетка форсирована как часть диагональной цепочки
                // (WallShapes::kChamferSizeChain = 0.92, иначе обычный
                // WallShapes::kChamferSize = 0.35).
                float chamferSize = (cellData.a > 0.5) ? 0.92 : 0.35;

                float tMid = clamp((tEnter + min(tExit, dist)) * 0.5, 0.0, dist);
                vec2 hitPoint = rayStart + dir * tMid;
                vec2 localXZ = hitPoint - vec2(cell);
                if (isLocalPointSolidGLSL(localXZ.x, localXZ.y, cutType, chamferSize))
                    return true;
                // иначе луч прошёл через вырезанный клин — не блокируем,
                // продолжаем трассировку дальше как обычно.
            }
        }

        if (cell == endCell)
            break;

        tEnter = tExit;

        if (tMaxX < tMaxY)
        {
            cell.x += stepDir.x;
            tMaxX += tDeltaX;
        }
        else
        {
            cell.y += stepDir.y;
            tMaxY += tDeltaY;
        }
    }

    return false;
}

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main()
{
    vec3 n = normalize(vNormal);

    // Текстура (если есть) умножается на вершинный цвет — как и
    // обычная PBR-конвенция glTF (baseColorTexture * COLOR_0). Без
    // текстуры — просто вершинный цвет, как было раньше.
    vec3 texColor = texture(diffuseTex, vUV).rgb;
    vec3 baseColor = mix(vColor, vColor * texColor, hasDiffuseTex);

    // Dev-tools: тонирование под текущую гамму (см. uniform
    // devPaletteOverride выше) — ПОСЛЕ выбора между обычной/текстурной
    // окраской модели, ДО освещения ниже, так что вся дальнейшая
    // физика (факелы, дев-фонарики, туман) продолжает работать поверх
    // уже сдвинутого тона без каких-либо других изменений в шейдере.
    if (devPaletteOverride >= 0)
    {
        baseColor *= (kDevPaletteWall[devPaletteOverride] / vec3(0.55, 0.35, 0.25));
    }


    float detail = 1.0;

    float ambient = 0.05;
    vec3 lit = baseColor * ambient * detail;

    for (int i = 0; i < 32; i++)
    {
        if (i >= torchCount)
            break;

        float seed = float(i) * 17.0;

        float flicker =
            0.82
            + 0.13 * sin(uTime * 9.0 + seed)
            + 0.05 * sin(uTime * 23.0 + seed * 1.7);

        vec3 toLight = torchPos[i] - vWorldPos;
        float lightDist = length(toLight);

        if (lightDist > 8.0)
            continue;

        float ndotl = max(dot(n, normalize(toLight)), 0.0);
        if (ndotl <= 0.001)
            continue;

        float attenuation =
            (torchIntensity[i] * flicker)
            / (1.0 + lightDist * lightDist * 0.35);

        if (attenuation * ndotl < 0.0008)
            continue;

        float shadowJitter =
            (hash(vWorldPos.xz * 41.0 + float(i) * 17.0) - 0.5) * 0.3;
        vec3 shadowRayFrom =
            vWorldPos
            + n * 0.04
            + vec3(shadowJitter, 0.0, -shadowJitter);

        if (shadowedByWall(shadowRayFrom, torchPos[i]))
            continue;

        lit += baseColor * torchColor[i] * ndotl * attenuation * detail;
    }

    // ---- Факел в руке игрока (та же формула, что и в scene.frag) ----
    {
        vec3 toFrag = vWorldPos - playerLightPos;
        float pDist = length(toFrag);

        if (pDist < 12.0)
        {
            vec3 toFragDir = toFrag / max(pDist, 0.0001);
            float pNdotl = max(dot(n, -toFragDir), 0.0);

            if (pNdotl > 0.001)
            {
                float facing = clamp(dot(playerLightDir, toFragDir), -1.0, 1.0);
                float forwardBoost = mix(0.85, 1.0, smoothstep(-0.2, 0.9, facing));

                float pFalloff = 1.0 / (1.0 + pDist * pDist * 0.13);
                float pFlicker = 0.9 + 0.1 * sin(uTime * 11.0 + 3.7);

                float pAtten = pFalloff * forwardBoost * playerLightIntensity * pFlicker;

                if (pAtten * pNdotl > 0.0008)
                {
                    lit += baseColor * playerLightColor * pNdotl * pAtten * detail;
                }
            }
        }
    }

    // ---- Dev-tools: статичные направленные фонарики (см. scene.frag) ----
    for (int i = 0; i < 8; i++)
    {
        if (i >= devLightCount)
            break;

        vec3 toFrag = vWorldPos - devLightPos[i];
        float dDist = length(toFrag);
        if (dDist > 40.0)
            continue;

        vec3 toFragDir = toFrag / max(dDist, 0.0001);
        float dNdotl = max(dot(n, -toFragDir), 0.0);
        if (dNdotl <= 0.001)
            continue;

        float facing = clamp(dot(devLightDir[i], toFragDir), -1.0, 1.0);
        float cone = smoothstep(0.55, 0.85, facing);
        if (cone <= 0.001)
            continue;

        float dFalloff = 1.0 / (1.0 + dDist * dDist * 0.03);
        float dAtten = dFalloff * cone * 2.0;

        if (dAtten * dNdotl > 0.0008)
        {
            lit += baseColor * vec3(0.90, 0.95, 1.0) * dNdotl * dAtten * detail;
        }
    }

    // ---- Туман (та же формула, что и в scene.frag) ----
    float dist = length(vWorldPos - camPos);
    float fogNear = 4.0;
    float fogFar = renderDistance;
    float fogVisibility = 1.0 - smoothstep(fogNear, fogFar, dist);

    lit *= fogVisibility;
    lit = clamp(lit, 0.0, 1.0);

    fragColor = vec4(lit, 1.0);
}
