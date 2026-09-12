#version 330 core

in vec3 vNormal;
in vec3 vColor;
in vec3 vWorldPos;
in float vMatId;
in float vParticleLife;

out vec4 fragColor;

uniform vec3 camPos;
uniform float uTime;

// ---- Dev-tools: перебор цветовых гамм окружения (клавиша G) ----
// -1 — выключено, обычная запечённая по вершинам палитра зон (см.
// Zoning::ZoneStyle::paletteIndex/SceneGeometry::GetZonePalette — там же
// плавный блендинг МЕЖДУ зонами, которого этот принудительный оверрайд
// НЕ даёт: он глобальный и резкий, для тестирования "как будет выглядеть
// вся сцена целиком в такой-то гамме", а не часть обычного зонирования).
// 0..4 — конкретная гамма из тех же 5 пресетов, ПРОДУБЛИРОВАННЫХ здесь
// (см. kDevPaletteWall/Floor ниже) — не читаем их из C++ каждый кадр
// уже потому, что стены/пол закрашиваются вершинным цветом при генерации
// геометрии, а не смотрят в шейдере в текстуру/буфер палитр вообще;
// проще держать 5 констант тут же, чем городить дополнительный uniform-
// массив ради 5 давно фиксированных значений.
uniform int devPaletteOverride;
const vec3 kDevPaletteWall[5] = vec3[5](
    vec3(0.55, 0.35, 0.25),
    vec3(0.30, 0.33, 0.38),
    vec3(0.28, 0.38, 0.24),
    vec3(0.42, 0.40, 0.38),
    vec3(0.50, 0.28, 0.22)
);
const vec3 kDevPaletteFloor[5] = vec3[5](
    vec3(0.18, 0.16, 0.13),
    vec3(0.10, 0.11, 0.14),
    vec3(0.10, 0.14, 0.10),
    vec3(0.15, 0.14, 0.13),
    vec3(0.17, 0.12, 0.10)
);

// ---- Факел в руке игрока ----
// Отдельный, ДЕШЁВЫЙ источник света: не входит в цикл настенных факелов
// ниже и НЕ проходит через shadowedByWall() вообще — это сознательный
// компромисс, не баг. Настенные факелы статичны, поэтому их взаимное
// расположение со стенами не меняется, и полный тест тени там оправдан.
// Факел игрока движется каждый кадр вместе с камерой — его физически
// невозможно запечь заранее, а полный per-pixel raymarch ещё для одного
// источника света на КАЖДЫЙ кадр (не важно, стоит игрок на месте или
// нет) заметно дороже, чем того стоит: это "фонарик под ногами", а не
// точный физический источник, ему не обязательно правильно прятаться
// за углами на расстоянии в пару клеток от игрока.
uniform vec3 playerLightPos;
uniform vec3 playerLightDir;   // нормализованное направление взгляда
uniform vec3 playerLightColor;
uniform float playerLightIntensity;

// ---- Dev-tools: статичные направленные "фонарики" (клавиша L) ----
// В отличие от playerLightPos/Dir выше (двигаются вместе с игроком
// каждый кадр), эти застывают там и с тем направлением, с которым были
// созданы — см. DungeonScene::spawnDevLightAtPlayerView(). Настоящий
// конус (в отличие от почти всенаправленного playerLight выше, см. его
// комментарий про "ощущается как фонарик, а не факел" — тут наоборот,
// это и должен быть узкий направленный луч, не мягкая подсветка вокруг
// себя), без теста тени (как и playerLight — дёшево, не задание точной
// физики, а инструмент для тестирования сцены/освещения).
uniform vec3 devLightPos[8];
uniform vec3 devLightDir[8];
uniform int devLightCount;

uniform vec3 torchPos[32];
uniform vec3 torchColor[32];
uniform float torchIntensity[32];
uniform int torchCount;

uniform sampler2D mapTex;

// Колонны (Шаг 2, см. src/scene/Columns.h) — для ray-vs-circle теста в
// shadowedByWall() ниже: клетка колонны в mapTex — уже пол (см.
// Columns::BuildColumns), поэтому сеточный тест её не видит вообще, а
// без отдельной проверки колонна не отбрасывала бы тень совсем, хотя
// физически перекрывает свет. MAX_COLUMNS=16 — колонны редки (обычно
// 0-2 на карту), см. C++-сторону (DungeonScene::render()) — там же
// защита от переполнения этого массива.
#define MAX_COLUMNS 16
uniform vec2 columnPos[MAX_COLUMNS];
uniform int columnCount;
uniform float columnRadius; // см. Columns::kColumnRadius (C++)

// ---- Тени от врагов (см. большой комментарий в shadowedByWall() ниже) ----
// Та же схема, что и у колонн выше — маленький массив uniform'ов,
// проверяется точным аналитическим тестом внутри shadowedByWall(), без
// какой-либо отдельной текстуры/прохода рендера.
#define MAX_ENEMY_OCCLUDERS 8
uniform vec2 enemyOccluderPosXZ[MAX_ENEMY_OCCLUDERS];
uniform int enemyOccluderCount;
uniform float enemyOccluderRadius;
uniform float enemyOccluderHeight;

// ---- Текстура стен (см. DungeonScene::loadWallTexture()) ----
// wallTexEnabled=0.0, если файл не нашёлся/не загрузился при старте —
// тогда стены рисуются старым процедурным "кирпичом" (fallback ниже),
// чтобы отсутствие файла не превращалось в чёрные/битые стены.
uniform sampler2D wallTex;
uniform float wallTexEnabled;
uniform float wallTexContrast; // см. DungeonScene::m_wallTexContrast

// Сколько раз текстура повторяется на 1 игровую клетку/юнит высоты.
// Подобрано под 256x256 текстуры пака Torment Textures — если поставить
// текстуру с другим "родным" масштабом кладки, можно поправить эти два
// числа, не трогая остальной шейдер.
// texUV = uv * WALL_TEX_TILE, где uv — мировые координаты (в юнитах).
// ЧЕМ БОЛЬШЕ множитель, тем ЧАЩЕ текстура повторяется на той же стене
// -> кирпичи МЕЛЬЧЕ (предыдущая правка 1.0->2.0 была ошибкой в обратную
// сторону). ЧЕМ МЕНЬШЕ множитель, тем реже повтор -> кирпичи КРУПНЕЕ.
// 0.5 = один повтор текстуры на 2 юнита мира — кладка вдвое крупнее,
// чем при исходных 1.0.
const vec2 WALL_TEX_TILE = vec2(0.5, 0.5);

// Дальность прорисовки/тумана — обычно 16.0 у игрока, но может быть
// увеличена извне (см. DungeonScene::render(), DevTools.h) для
// кинематографичных кадров в noclip. Раньше это была локальная const
// внутри main() (RENDER_DISTANCE = 16.0) — вынесена в uniform, чтобы
// её можно было менять с CPU без перекомпиляции шейдера.
uniform float renderDistance;

const vec2 MAP_SIZE =
    vec2(128.0);

// ==================================================
// �������� ����� ����� ����� �������
// ==================================================

// Зеркало WallShapes::IsLocalPointSolid (CPU, src/scene/WallShapes.cpp) —
// при изменении формулы среза там держать в синхроне и здесь. cut: 0=None,
// 1=SW, 2=SE, 3=NE, 4=NW (совпадает с WallShapes::CornerCut).
bool isLocalPointSolidGLSL(float lx, float lz, int cut, float chamferSize)
{
    if (cut == 1) return !(lx + lz < chamferSize);
    if (cut == 2) return !((1.0 - lx) + lz < chamferSize);
    if (cut == 3) return !((1.0 - lx) + (1.0 - lz) < chamferSize);
    if (cut == 4) return !(lx + (1.0 - lz) < chamferSize);
    return true; // None — не должно сюда попадать (см. вызывающий код)
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

    // Враги (см. EnemyAI.h/EnemyCharacter.h) — тот же точный ray-vs-circle
    // тест, что и у колонн выше, ПЛЮС проверка по высоте: враг, в
    // отличие от колонны, не бесконечно высокий столб (см.
    // enemyOccluderHeight, C++ DungeonScene::render()) — блокирует луч,
    // только если тот в точке наибольшего сближения проходит НИЖЕ
    // макушки врага, а не просто "где-то рядом по XZ". Без этой
    // проверки невысокий враг блокировал бы свет от факела, висящего
    // высоко на стене, даже когда луч физически проходит у него над
    // головой.
    //
    // ИСТОРИЯ: первая версия этой фичи считала тень через отдельную
    // растеризованную карту глубины (общий "shadow map" 1024x1024,
    // ортокамера сверху, следующая за игроком) и сэмплировала луч в
    // нескольких точках вдоль отрезка. На практике тонкий (радиус
    // ~0.3) силуэт врага регулярно "проскакивал" между редкими
    // сэмплами — тень получалась едва заметными беспорядочными пятнами,
    // а не связной формой. Точный аналитический тест, как и у колонн,
    // не пропускает тонкую геометрию НИКОГДА, независимо от длины
    // отрезка — и заодно, раз он теперь часть shadowedByWall(),
    // работает и для стен, не только для пола (эта функция вызывается
    // для ВСЕХ поверхностей, см. общий цикл по факелам ниже).
    for (int ei = 0; ei < enemyOccluderCount; ++ei)
    {
        vec2 seg = p1 - p0;
        float segLenSq = dot(seg, seg);
        float t = segLenSq > 1e-9 ? clamp(dot(enemyOccluderPosXZ[ei] - p0, seg) / segLenSq, 0.0, 1.0) : 0.0;
        vec2 closest = p0 + seg * t;
        if (length(enemyOccluderPosXZ[ei] - closest) < enemyOccluderRadius)
        {
            // t выше — параметр вдоль ТРИММИРОВАННОГО [p0,p1], не вдоль
            // всего [rayStart,to] (см. startT/endT выше) — переводим
            // обратно в долю полной длины луча, чтобы взять высоту в
            // ПРАВИЛЬНОЙ точке между from.y и to.y, а не в точке,
            // слегка сдвинутой отступами самозатенения.
            float distAlongRay = startT + t * (endT - startT);
            float heightFrac = clamp(distAlongRay / dist, 0.0, 1.0);
            float rayY = mix(from.y, to.y, heightFrac);
            if (rayY < enemyOccluderHeight)
                return true;
        }
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

// ==================================================
// Hash
// ==================================================

float hash(vec2 p)
{
    return fract(
        sin(
            dot(
                p,
                vec2(
                    127.1,
                    311.7
                )
            )
        )
        * 43758.5453123
    );
}

// ==================================================
// FBM
// ==================================================

float fbmNoise(vec2 p)
{
    float n = 0.0;

    n +=
        hash(
            floor(
                p * 3.0
            )
        )
        * 0.5;

    n +=
        hash(
            floor(
                p * 11.0
            )
        )
        * 0.3;

    n +=
        hash(
            floor(
                p * 37.0
            )
        )
        * 0.2;

    return n;
}

void main()
{
    float cameraDistance =
        length(
            vWorldPos
            - camPos
        );

    // RENDER_DISTANCE вынесена в uniform "renderDistance" (см. объявление
    // выше) — 16.0 остаётся дефолтом обычного игрока (см. render()),
    // но теперь её можно увеличить с CPU для кинематографичных кадров.
    float FADE_START =
        renderDistance
        - 5.0;

    if (
        cameraDistance
        >
        renderDistance
    )
    {
        discard;
    }

    // ==================================================
    // �������� �������
    // ==================================================

    if (vMatId > 2.5)
    {
        vec2 centered =
            gl_PointCoord
            - vec2(0.5);

        float pointDistance =
            length(centered);

        if (
            pointDistance > 0.5
        )
        {
            discard;
        }

        float particleAlpha =
            1.0
            -
            smoothstep(
                0.15,
                0.5,
                pointDistance
            );

        // --------------------------------------------------
        // ������� ��������� �������.
        //
        // ������ ~60% �����:
        //     ����� ������ �������.
        //
        // ��������� ~40%:
        //     ������ ������.
        //
        // ��� life = 1.0:
        //     ����������� ��������� ��������.
        // --------------------------------------------------

        float lifeFade =
            1.0
            -
            smoothstep(
                0.58,
                1.0,
                vParticleLife
            );

        float lifeGlow =
            lifeFade;

        vec3 particleColor =
            mix(
                vec3(
                    1.0,
                    0.25,
                    0.025
                ),
                vec3(
                    1.0,
                    0.88,
                    0.32
                ),
                1.0 - vParticleLife
            );

        float particleFade =
            1.0
            -
            smoothstep(
                FADE_START,
                renderDistance,
                cameraDistance
            );

        fragColor =
            vec4(
                particleColor,
                particleAlpha
                * lifeGlow
                * particleFade
            );

        return;
    }

    // ==================================================
    // �����
    // ==================================================

    if (vMatId > 1.5)
    {
        const float FLAME_ID_SCALE =
            4096.0;

        float torchId =
            floor(
                (vMatId - 2.0)
                * FLAME_ID_SCALE
                + 0.5
            );

        float seed =
            hash(
                vec2(
                    torchId + 4.0,
                    torchId * 7.1
                )
            );

        float flicker =
            1.12
            +
            0.15
            * sin(
                uTime * 13.0
                + seed * 17.0
            )
            +
            0.08
            * sin(
                uTime * 23.0
                + seed * 31.0
            )
            +
            0.045
            * sin(
                uTime * 37.0
                + seed * 7.0
            );

        float verticalFire =
            clamp(
                vNormal.y * 0.5
                + 0.5,
                0.0,
                1.0
            );

        vec3 lowerColor =
            vec3(
                1.0,
                0.82,
                0.20
            );

        vec3 upperColor =
            vec3(
                1.0,
                0.30,
                0.035
            );

        vec3 fireColor =
            mix(
                lowerColor,
                upperColor,
                verticalFire
            );

        vec3 glow =
            fireColor
            * flicker
            * vColor.r;

        float flameFade =
            1.0
            -
            smoothstep(
                FADE_START,
                renderDistance,
                cameraDistance
            );

        glow *=
            flameFade;

        fragColor =
            vec4(
                clamp(
                    glow,
                    0.0,
                    1.0
                ),
                1.0
            );

        return;
    }

    // ==================================================
    // �������� ������
    // ==================================================

    if (vMatId > 0.5)
    {
        vec3 handle =
            vColor
            * 1.15;

        float handleFade =
            1.0
            -
            smoothstep(
                FADE_START,
                renderDistance,
                cameraDistance
            );

        handle *=
            handleFade;

        fragColor =
            vec4(
                clamp(
                    handle,
                    0.0,
                    1.0
                ),
                1.0
            );

        return;
    }

    // ==================================================
    // ������� ���������
    // ==================================================

    vec3 n =
        normalize(
            vNormal
        );

    vec2 uv;

    if (abs(n.y) > 0.5)
    {
        uv =
            vWorldPos.xz;
    }
    else if (abs(n.x) > 0.5)
    {
        uv =
            vWorldPos.zy;
    }
    else
    {
        uv =
            vWorldPos.xy;
    }

    vec3 baseColor =
        vColor;

    // Dev-tools: принудительная гамма (см. uniform devPaletteOverride
    // выше) — ПОСЛЕ обычного vColor, чтобы полностью его перекрыть, но
    // ДО текстуры/деталей ниже — вся дальнейшая тонировка текстуры,
    // грязи, факелов и т.д. продолжает работать поверх новой baseColor
    // ровно так же, как если бы это был обычный запечённый цвет зоны:
    // ничего в остальном шейдере трогать не пришлось.
    if (devPaletteOverride >= 0)
    {
        baseColor =
            (abs(n.y) < 0.5)
                ? kDevPaletteWall[devPaletteOverride]
                : kDevPaletteFloor[devPaletteOverride];
    }

    float detail =
        1.0;

    // ==================================================
    // �����
    // ==================================================

    if (abs(n.y) < 0.5)
    {
        if (wallTexEnabled > 0.5)
        {
            // ---- Настоящая текстура (str_*.bmp, Torment Textures) ----
            vec2 texUV = uv * WALL_TEX_TILE;
            vec3 texColor = texture(wallTex, texUV).rgb;

            // Контраст-стретч ВОКРУГ серой точки, ДО умножения на тон
            // стены и свет. Без этого деталь текстуры (перепады
            // кладки/трещин) сжимается тусклым ambient/факельным
            // светом ниже по шейдеру в узкий диапазон яркости, а
            // ASCII-квантователь в AsciiEffect.cpp тогда использует
            // лишь горстку самых тёмных символов рампы из полусотни
            // доступных — картинка "разливается" в однородный серый.
            // wallTexContrast=1.0 — без изменений, см.
            // DungeonScene::m_wallTexContrast.
            texColor = clamp(
                (texColor - vec3(0.5)) * wallTexContrast + vec3(0.5),
                0.0,
                1.0
            );

            // Тонируем сэмпл базовым цветом стены (baseColor — либо
            // обычный запечённый vColor, либо, если включён dev-tools
            // оверрайд гаммы (см. devPaletteOverride выше), уже
            // подменённый принудительный цвет; раньше здесь было именно
            // vColor напрямую — из-за этого клавиша G перекрашивала
            // пол, но НЕ стены: этот блок тут же переписывал baseColor
            // обратно из необновлённого vColor, стирая оверрайд), а не
            // заменяем его целиком — так освещение факелами/дальний
            // туман ниже по шейдеру продолжают работать без изменений:
            // текстура даёт форму (кладка/трещины/ржавчина), baseColor —
            // общий тон.
            baseColor =
                texColor
                * (
                    baseColor
                    / vec3(0.55, 0.35, 0.25)
                );

            // Та же процедурная плесень/грязь поверх настоящей
            // текстуры — прячет регулярность тайлинга и держит общее
            // "сырое подземелье" настроение независимо от того, какую
            // именно картинку сюда подставили.
            float mossNoise =
                hash(
                    floor(
                        uv * 10.0
                    )
                );

            float mossZone =
                clamp(
                    1.0
                    - (
                        uv.y * 1.3
                    ),
                    0.0,
                    1.0
                );

            float moss =
                step(
                    0.55,
                    mossNoise
                )
                * mossZone;

            baseColor =
                mix(
                    baseColor,
                    baseColor
                    * vec3(
                        0.35,
                        0.45,
                        0.30
                    ),
                    moss * 0.8
                );

            detail = 1.0;
        }
        else
        {
            // ---- Fallback: старый процедурный "кирпич" ----
            // Срабатывает, только если wallTex не загрузился (файл не
            // найден и т.п.) — см. DungeonScene::loadWallTexture().
            vec2 scaled =
                uv
                * vec2(
                    4.0,
                    6.0
                );

            float row =
                floor(
                    scaled.y
                );

            float rowOffset =
                mod(
                    row,
                    2.0
                )
                * 0.5;

            vec2 scaledOffset =
                vec2(
                    scaled.x
                    + rowOffset,
                    scaled.y
                );

            vec2 brickId =
                floor(
                    scaledOffset
                );

            vec2 b =
                fract(
                    scaledOffset
                );

            float mortarW =
                0.06;

            float mortar =
                step(
                    b.x,
                    mortarW
                )
                +
                step(
                    b.y,
                    mortarW * 1.5
                );

            mortar =
                clamp(
                    mortar,
                    0.0,
                    1.0
                );

            float brickShade =
                hash(
                    brickId
                    + 3.0
                )
                * 0.6
                + 0.35;

            vec3 brickColor =
                baseColor
                * brickShade;

            baseColor =
                mix(
                    brickColor,
                    brickColor
                    * 0.35,
                    mortar
                );

            float mossNoise =
                hash(
                    floor(
                        uv * 10.0
                    )
                );

            float mossZone =
                clamp(
                    1.0
                    - (
                        uv.y * 1.3
                    ),
                    0.0,
                    1.0
                );

            float moss =
                step(
                    0.55,
                    mossNoise
                )
                * mossZone;

            baseColor =
                mix(
                    baseColor,
                    baseColor
                    * vec3(
                        0.35,
                        0.45,
                        0.30
                    ),
                    moss * 0.8
                );

            detail =
                1.0
                - mortar * 0.5;
        }
    }

    // ==================================================
    // ���
    // ==================================================

    else
    {
        vec2 tileUv =
            uv * 3.0;

        vec2 tileId =
            floor(
                tileUv
            );

        vec2 tileFrac =
            fract(
                tileUv
            );

        float groutW =
            0.02;

        float grout =
            step(
                tileFrac.x,
                groutW
            )
            +
            step(
                1.0 - groutW,
                tileFrac.x
            )
            +
            step(
                tileFrac.y,
                groutW
            )
            +
            step(
                1.0 - groutW,
                tileFrac.y
            );

        grout =
            clamp(
                grout,
                0.0,
                1.0
            );

        float tileShade =
            hash(
                tileId
                + 50.0
            )
            * 0.30
            + 0.75;

        baseColor *=
            tileShade;

        baseColor =
            mix(
                baseColor,
                baseColor * 0.25,
                grout
            );

        float mossNoise =
            hash(
                tileId
                + 500.0
            );

        float moss =
            step(
                0.45,
                mossNoise
            );

        vec3 darkMoss =
            baseColor
            * vec3(
                0.30,
                0.42,
                0.28
            );

        baseColor =
            mix(
                baseColor,
                darkMoss,
                moss * 0.7
            );

        float fineNoise =
            hash(
                floor(
                    uv * 25.0
                )
                + 200.0
            )
            * 0.15
            + 0.92;

        baseColor *=
            fineNoise;

        detail =
            1.0
            - grout * 0.6;
    }

    // ==================================================
    // �������������� �����������
    // ==================================================

    float grain =
        fbmNoise(
            uv * 2.0
            + vColor.xy * 0.01
        )
        * 0.30
        + 0.85;

    baseColor *=
        grain;

    // ==================================================
    // Ambient
    // ==================================================

    // Вернул исходные 0.05 — 0.14 давало слишком светлую картинку и
    // убивало тёмную атмосферу подземелья. За читаемость текстуры в
    // тени теперь отвечают wallTexContrast и WALL_TEX_TILE выше, а не
    // общая яркость сцены.
    float ambient =
        0.05;

    vec3 lit =
        baseColor
        * ambient
        * detail;

    // ==================================================
    // ������
    // ==================================================

    for (
        int i = 0;
        i < 32;
        i++
    )
    {
        if (i >= torchCount)
            break;

        float seed =
            float(i)
            * 17.0;

        float flicker =
            0.82
            + 0.13
            * sin(
                uTime * 9.0
                + seed
            )
            + 0.05
            * sin(
                uTime * 23.0
                + seed * 1.7
            );

        vec3 toLight =
            torchPos[i]
            - vWorldPos;

        float lightDist =
            length(
                toLight
            );

        if (
            lightDist > 8.0
        )
        {
            continue;
        }

        float ndotl =
            max(
                dot(
                    n,
                    normalize(
                        toLight
                    )
                ),
                0.0
            );

        if (
            ndotl <= 0.001
        )
        {
            continue;
        }

        float attenuation =
            (
                torchIntensity[i]
                * flicker
            )
            /
            (
                1.0
                +
                lightDist
                * lightDist
                * 0.35
            );

        // Perf: skip torches whose contribution is already far below what
        // a person could ever perceive (near-black either way, shadowed
        // or not) before paying for the expensive shadow raymarch below.
        // Kept deliberately extreme/conservative (not the earlier 0.004)
        // so the flicker term (±~0.18 over time, see `flicker` above)
        // can never push a torch back and forth across this threshold —
        // that was producing a visible "light snapping on/off" bug for
        // torches sitting near the old cutoff.
        if (attenuation * ndotl < 0.0008)
        {
            continue;
        }

        // NOTE: a per-rank cutoff ("only the nearest N torches get a
        // shadow test, the rest are always unshadowed") was tried here
        // and reverted — as the player moves, a torch's distance rank
        // can flip from inside to outside that cutoff between one frame
        // and the next, which makes an occluded torch instantly jump
        // from "correctly dark behind the wall" to "fully lit" (or back)
        // — a visible pop/flash. Every torch that reaches this point now
        // gets the same, consistent shadow test; see DungeonScene.h for
        // the real fix (MAX_ACTIVE_TORCHES lowered) instead.
        //
        // Фикс бага "линии от факелов накладываются друг на друга,
        // выглядит странно на полу": shadowedByWall() — это ОДИН
        // геометрический луч на факел, поэтому его граница тень/свет —
        // идеально резкая линия. Там, где такие резкие линии от ДВУХ
        // РАЗНЫХ факелов пересекаются на полу, это читается как
        // отчётливый крест/шов, а не как естественный переход. Вместо
        // дорогого софтшадоу (несколько лучей на факел — на слабом
        // железе не вариант, см. DungeonScene.h::MAX_ACTIVE_TORCHES)
        // размываем саму точку старта луча небольшим шумом,
        // привязанным к мировым координатам (стабилен во времени — не
        // мерцает при неподвижной камере) И к индексу факела i (шум
        // разных факелов НЕ коррелирован — иначе все резкие границы
        // просто сдвинулись бы синхронно на одну и ту же величину,
        // ничего не решив). Даёт узкую зашумлённую (dithered) полосу
        // вместо чёткой линии — тот же приём, что и Bayer-дизеринг в
        // ascii_post.frag, только не в экранном, а в мировом пространстве.
        float shadowJitter =
            (hash(vWorldPos.xz * 41.0 + float(i) * 17.0) - 0.5) * 0.3;
        vec3 shadowRayFrom =
            vWorldPos
            + n * 0.04
            + vec3(shadowJitter, 0.0, -shadowJitter);

        if (
            shadowedByWall(
                shadowRayFrom,
                torchPos[i]
            )
        )
        {
            continue;
        }

        lit +=
            baseColor
            * torchColor[i]
            * ndotl
            * attenuation
            * detail;
    }

    // ==================================================
    // Факел в руке игрока — см. комментарий у uniform playerLightPos
    // выше: без shadowedByWall(), поэтому дешёвый (не масштабируется с
    // MAX_ACTIVE_TORCHES и не растёт по стоимости от количества
    // настенных факелов вообще).
    // ==================================================

    {
        vec3 toFrag = vWorldPos - playerLightPos;
        float pDist = length(toFrag);

        // Жёсткая внешняя граница дальности — просто чтобы не тратить
        // остаток формулы там, где вклад и так уже пренебрежимо мал.
        if (pDist < 12.0)
        {
            vec3 toFragDir = toFrag / max(pDist, 0.0001);

            float pNdotl =
                max(
                    dot(n, -toFragDir),
                    0.0
                );

            if (pNdotl > 0.001)
            {
                // Мягкий "конус фонарика" -> "ощущается как фонарик"
                // (жалоба) — сделали ПОЧТИ всенаправленным, как и
                // положено настоящему факелу: было mix(0.35, 1.0, ...),
                // теперь разница между "вокруг" и "куда смотрю" совсем
                // небольшая (0.85 -> 1.0), просто лёгкий акцент вперёд,
                // а не выраженный луч.
                float facing =
                    clamp(
                        dot(playerLightDir, toFragDir),
                        -1.0,
                        1.0
                    );
                float forwardBoost =
                    mix(
                        0.85,
                        1.0,
                        smoothstep(-0.2, 0.9, facing)
                    );

                // Коэффициент затухания (у настенных факелов — 0.35);
                // 0.13 — было 0.09 ("слишком ярко", уменьшили и дальность
                // тоже, не только интенсивность) — играть с этим числом,
                // если нужно короче/длиннее в среднем по всем направлениям.
                float pFalloff =
                    1.0
                    / (1.0 + pDist * pDist * 0.13);

                // Небольшое мерцание для атмосферы, не синхронное с
                // настенными факелами (свой сдвиг фазы 3.7).
                float pFlicker =
                    0.9 + 0.1 * sin(uTime * 11.0 + 3.7);

                float pAtten =
                    pFalloff
                    * forwardBoost
                    * playerLightIntensity
                    * pFlicker;

                if (pAtten * pNdotl > 0.0008)
                {
                    lit +=
                        baseColor
                        * playerLightColor
                        * pNdotl
                        * pAtten
                        * detail;
                }
            }
        }
    }

    // ==================================================
    // Dev-tools: статичные направленные фонарики (см. uniform devLightPos
    // выше) — та же формула, что и у факела в руке, но: настоящий узкий
    // конус (cone ниже, а не мягкий forwardBoost 0.85->1.0), без
    // мерцания (это инструмент, не огонь) и с более пологим затуханием
    // по дистанции (0.03 вместо 0.13 у ручного факела) — рассчитан на
    // то, чтобы дотягиваться через часть комнаты/коридора, а не только
    // на пару шагов вокруг игрока.
    // ==================================================

    for (
        int i = 0;
        i < 8;
        i++
    )
    {
        if (i >= devLightCount)
            break;

        vec3 toFrag =
            vWorldPos
            - devLightPos[i];

        float dDist =
            length(
                toFrag
            );

        if (
            dDist > 40.0
        )
        {
            continue;
        }

        vec3 toFragDir =
            toFrag
            / max(dDist, 0.0001);

        float dNdotl =
            max(
                dot(n, -toFragDir),
                0.0
            );

        if (dNdotl <= 0.001)
        {
            continue;
        }

        float facing =
            clamp(
                dot(devLightDir[i], toFragDir),
                -1.0,
                1.0
            );

        // Настоящий конус: снаружи ~0 (не 0.85, как у мягкого ручного
        // факела) — за пределами примерно 56° от оси луч уже не даёт
        // ничего, полная яркость — примерно в пределах 32° от оси,
        // между ними — мягкий, но заметный край.
        float cone =
            smoothstep(0.55, 0.85, facing);

        if (cone <= 0.001)
        {
            continue;
        }

        float dFalloff =
            1.0
            / (1.0 + dDist * dDist * 0.03);

        float dAtten =
            dFalloff
            * cone
            * 2.0; // интенсивность — заметно ярче ручного факела, это же "прожектор"/инструмент, а не фитилёк

        if (dAtten * dNdotl > 0.0008)
        {
            lit +=
                baseColor
                * vec3(0.90, 0.95, 1.0) // холодный тон — специально отличается от тёплого факела/фонарика, чтобы дев-свет визуально не путался с обычным освещением сцены
                * dNdotl
                * dAtten
                * detail;
        }
    }

    float dist =
        length(
            vWorldPos
            - camPos
        );

    float fogNear =
        4.0;

    float fogFar =
        renderDistance;

    float fogVisibility =
        1.0
        -
        smoothstep(
            fogNear,
            fogFar,
            dist
        );

    lit *=
        fogVisibility;

    lit =
        clamp(
            lit,
            0.0,
            1.0
        );

    fragColor =
        vec4(
            lit,
            1.0
        );
}
