#version 330 core

in vec3 vNormal;
in vec3 vWorldPos;
in float vType;
in vec3 vLocal;

out vec4 fragColor;

uniform sampler2D minimapTex;
uniform float minimapYawDeg;

uniform sampler2D minimapFontTex;
uniform float minimapGlyphCount;

// Тот же переключатель, что и у AsciiEffect (см. AsciiEffect::setColorEnabled()/
// DungeonScene::setColorEnabled()) — компас использует собственный шейдер, не
// проходящий через ASCII-постпроцесс, поэтому дублирует ту же логику
// тонировки, что и мини-карта в AsciiEffect.cpp.
uniform float colorEnabled;
// УЛУЧШЕНИЕ ("на мини-карте отображать врага, если игрок его уже видел",
// "пусть символ врага постепенно тухнет, а не сразу исчезает", "4 врага
// по всей карте") — enemyMinimapOffset[i] уже посчитан на CPU (см.
// Compass::render()/DungeonScene::renderCompassOverlay()) в ТОЙ ЖЕ
// системе координат, что и off ниже (целая клетка врага минус целая
// клетка игрока по X, и наоборот по Z). enemySpottedAlpha[i] — НЕ просто
// 0/1 флаг: см. EnemyAI::spottedMarkerAlpha() — плавно едет от 1 к 0 в
// последние несколько секунд "памяти" игрока об ЭТОМ конкретном враге,
// вместо мгновенного щелчка. При 0 маркер для этого врага не рисуется
// вообще, даже если он физически в радиусе диска. enemyCount — сколько
// элементов массивов реально заполнено (0..kMaxEnemies).
const int kMaxEnemies = 7; // = DungeonScene::kEnemyCount — держать в синхроне при следующем изменении числа врагов
uniform float enemySpottedAlpha[kMaxEnemies];
uniform vec2 enemyMinimapOffset[kMaxEnemies];
uniform int enemyCount;

const float MAP_N = 21.0;
const float MAP_RADIUS = 0.235;

void main()
{
    // Compass top face / ASCII map — same algorithm as the original
    // flat AsciiEffect minimap: disc of glyphs + circular 'O' ring border,
    // player arrow always in the center cell, no extra decoration.
    if (vType > 1.5 && vType < 2.5) {
        vec2 local = vLocal.xz;

        const float halfGrid = 10.0;              // (MAP_N - 1) / 2, MAP_N = 21
        const float ringOuterEdge = halfGrid + 1.0;
        const float marginRadius = ringOuterEdge + 1.0;

        // Fit the whole disc (grid + ring + margin) exactly inside MAP_RADIUS.
        float cellSize = MAP_RADIUS / marginRadius;

        vec2 cellF = local / cellSize;
        vec2 off = floor(cellF + 0.5);
        vec2 localUV = fract(cellF + 0.5);
        localUV.y = 1.0 - localUV.y;

        float dist = length(off);
        if (dist > marginRadius)
            discard;

        float glyphIdx = -1.0;
        float matchedEnemyAlpha = 0.0; // см. блок ниже, где считается outColor — 0, если ни один враг не попал в эту клетку

        if (dist <= halfGrid) {
            if (abs(off.x) < 0.5 && abs(off.y) < 0.5) {
                // Center cell is always the player.
                float yawRad = radians(minimapYawDeg);
                vec2 dir = vec2(cos(yawRad), -sin(yawRad));

                float TWO_PI = 6.2831853;
                float ang = atan(dir.y, dir.x);
                if (ang < 0.0)
                    ang += TWO_PI;

                float sector = floor(mod(ang / (TWO_PI / 8.0) + 0.5, 8.0));

                if (sector < 0.5)
                    glyphIdx = 6.0;      // >
                else if (sector < 1.5)
                    glyphIdx = 5.0;      // /
                else if (sector < 2.5)
                    glyphIdx = 4.0;      // ^
                else if (sector < 3.5)
                    glyphIdx = 11.0;     // NW
                else if (sector < 4.5)
                    glyphIdx = 10.0;     // <
                else if (sector < 5.5)
                    glyphIdx = 9.0;      // /
                else if (sector < 6.5)
                    glyphIdx = 8.0;      // v
                else
                    glyphIdx = 7.0;      // SE
            } else {
                float gx = off.x + halfGrid;
                float gy = halfGrid - off.y;

                // УЛУЧШЕНИЕ ("на мини-карте отображать врага, если игрок
                // его уже видел", "плавное затухание", "4 врага по всей
                // карте") — запоминаем ТОЛЬКО alpha самого подходящего
                // врага здесь; сам маркер рисуется/блендится ПОСЛЕ
                // обычного глифа клетки (см. ниже, после вычисления
                // outColor) — так можно плавно смешать его с тем, что
                // было бы показано без маркера (туман/стена/пол/факел),
                // а не мгновенно подменять один глиф другим. Допуск 0.5
                // — offset тут всегда целый (считается из floor() на
                // CPU), с запасом покрывает погрешность интерполяции
                // между соседними фрагментами. Если случайно совпали
                // клетки двух врагов (редкость) — берём максимальную
                // alpha, чтобы более "свежий" контакт не терялся за
                // уже гаснущим другим.
                for (int ei = 0; ei < kMaxEnemies; ei++) {
                    if (ei >= enemyCount)
                        break;
                    if (distance(off, enemyMinimapOffset[ei]) < 0.5) {
                        matchedEnemyAlpha = max(matchedEnemyAlpha, enemySpottedAlpha[ei]);
                    }
                }

                vec2 mapUV = (vec2(gx, gy) + 0.5) / MAP_N;
                float v = texture(minimapTex, mapUV).r;

                if (v > 0.9)
                    glyphIdx = 3.0;      // *
                else if (v > 0.55)
                    glyphIdx = 1.0;      // #
                else if (v > 0.2)
                    glyphIdx = 2.0;      // .
                else
                    glyphIdx = -1.0;     // unexplored
            }
        } else {
            // Circular ring border, symbol 'O' (index 12).
            float R = ringOuterEdge;
            float ax = abs(off.x);
            float ay = abs(off.y);

            float idealY = sqrt(max(R * R - ax * ax, 0.0));
            float idealX = sqrt(max(R * R - ay * ay, 0.0));

            bool testY = (ax <= R) && (abs(ay - idealY) < 0.55);
            bool testX = (ay <= R) && (abs(ax - idealX) < 0.55);

            if (testY || testX)
                glyphIdx = 12.0;
        }

        vec3 outColor = vec3(0.0);

        if (glyphIdx >= 0.0) {
            vec2 glyphOrigin = vec2(glyphIdx / minimapGlyphCount, 0.0);
            vec2 glyphUV = glyphOrigin + vec2(localUV.x / minimapGlyphCount, localUV.y);
            float mask = texture(minimapFontTex, glyphUV).r;
            vec3 bwColor = vec3(step(0.5, mask));

            // Та же тонировка по типу клетки/символа, что и у плоской
            // мини-карты в AsciiEffect.cpp (см. комментарий там же):
            // факел — оранжевый, стена — камень, пол — земля, стрелка
            // игрока — бирюза, обводка круга — серебро.
            vec3 tint = vec3(1.0);
            if (glyphIdx == 3.0)      tint = vec3(1.00, 0.55, 0.10); // факел
            else if (glyphIdx == 1.0) tint = vec3(0.55, 0.55, 0.62); // стена
            else if (glyphIdx == 2.0) tint = vec3(0.42, 0.34, 0.22); // пол
            else if (glyphIdx >= 4.0 && glyphIdx <= 11.0)
                                        tint = vec3(0.20, 0.90, 0.95); // игрок
            else if (glyphIdx == 12.0) tint = vec3(0.62, 0.62, 0.68); // обводка

            outColor = mix(bwColor, bwColor * tint, colorEnabled);
        }

        // УЛУЧШЕНИЕ ("плавное затухание маркера врага") — блендим ПОВЕРХ
        // уже готового outColor (туман/стена/пол/факел/пусто), а не
        // подменяем glyphIdx заранее — так переход плавный: на
        // matchedEnemyAlpha=1 полностью маркер, на 0 — полностью то, что
        // было бы без него, и любое промежуточное значение — честная
        // смесь обоих цветов, а не мгновенный щелчок между ними.
        if (matchedEnemyAlpha > 0.001) {
            vec2 glyphOrigin = vec2(26.0 / minimapGlyphCount, 0.0);
            vec2 glyphUV = glyphOrigin + vec2(localUV.x / minimapGlyphCount, localUV.y);
            float markerMask = texture(minimapFontTex, glyphUV).r;
            vec3 markerBW = vec3(step(0.5, markerMask));
            vec3 markerTint = vec3(0.95, 0.12, 0.12); // враг — красный, тот же тон, что и раньше у glyphIdx==26
            vec3 markerColor = mix(markerBW, markerBW * markerTint, colorEnabled);
            outColor = mix(outColor, markerColor, matchedEnemyAlpha);
        }

        fragColor = vec4(outColor, 1.0);
        return;
    }

    // Compass body / rim.
    vec3 n = normalize(vNormal);
    float topLight = 0.55 + 0.45 * max(dot(n, vec3(0.0, 1.0, 0.0)), 0.0);
    vec3 metal = vec3(0.12, 0.12, 0.10) * topLight;

    if (vType < 0.5)
        metal += vec3(0.08, 0.07, 0.05);

    fragColor = vec4(clamp(metal, 0.0, 1.0), 1.0);
}
