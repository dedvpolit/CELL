#version 330 core
in vec2 vUV;
out vec4 fragColor;

uniform sampler2D sceneTex;
uniform sampler2D fontTex;
uniform vec2  screenResolution;
uniform float cellSize;
uniform float rampLength;

// ---- Мини-карта ----
uniform sampler2D minimapTex;      // NxN: 0=пустота, 85=пол, 170=стена, 255=факел
uniform sampler2D minimapFontTex;  // фиксированный (не по яркости) атлас символов
uniform float minimapGridSize;     // сторона minimapTex в клетках (0 = мини-карта выключена)
uniform float minimapGlyphCount;   // кол-во символов в minimapFontTex
uniform float minimapYawDeg;       // yaw игрока, градусы (для поворота "^")

// ---- UI-текст (заголовок/кнопки меню и т.п., см. AsciiEffect::setUIOverlay()) ----
// Отдельный слой поверх ВСЕГО остального (сцены, мини-карты, полосы
// стамины) — рисуется первым в main() и делает return, если попал в
// клетку с текстом. uiTex — R8-текстура uiCols x uiRows, один тексель
// на символьную клетку экрана; 0 = пусто (сквозь видна сцена), иначе
// (glyphIndex+1), чтобы отличить "пусто" от индекса 0 в minimapFontTex.
uniform sampler2D uiTex;
uniform float uiCols;
uniform float uiRows;
uniform float uiEnabled;

// Плавное затемнение всего экрана (0 = не затемнено, 1 = чёрный) —
// переход между стартовым меню и игрой (см. main.cpp). Применяется
// единообразно во ВСЕХ путях выхода из main() ниже (UI-текст,
// мини-карта, обычный ASCII-рендер сцены+стамина), а не как отдельный
// проход поверх — так не нужен второй draw call/шейдер только под фейд.
uniform float fadeAlpha;

// ---- Полоса энергии/стамины ----
uniform float staminaFrac;    // 0..1, доля заполнения (см. DungeonScene::getStaminaFraction())
uniform float staminaAlpha;   // 0..1, непрерывная видимость полосы (плавный fade, см. AsciiEffect::end())
uniform float healthFrac;     // 0..1, доля здоровья (см. DungeonScene::getHealthFraction()) —
                               // управляет тем, насколько сильно "каплет" вся рамка полосы.

// ---- Цветной режим (см. AsciiEffect::setColorEnabled(), Settings -> COLOR) ----
// 0 = классический чёрно-белый ASCII (как раньше), 1 = все элементы
// тонируются цветом. ВАЖНО: это исключительно тонировка уже выбранного
// ASCII-глифа/маски — набор символов и их расположение НЕ меняются,
// цвет лишь умножает готовую 0/1-маску глифа, поэтому картинка
// остаётся строго ASCII в любом режиме.
uniform float colorEnabled;

// ---- Линза (Settings -> LENS, см. AsciiEffect::setLensEffectEnabled()) ----
// Едва заметная "бочкообразная" дисторсия картинки сцены + лёгкое
// круглое затемнение по краям экрана (виньетка). Искажается ТОЛЬКО
// сэмплирование sceneTex (сама "картинка" внутри ASCII-клеток) — сетка
// символов, UI-текст и миникарта остаются идеально ровными, виньетка
// же затемняет финальный цвет везде одинаково (см. все return-ветки
// ниже и конец main()).
uniform float lensEffectEnabled;
// УЛУЧШЕНИЕ ("капли крови по экрану, снова и снова") — см. большой
// комментарий у AsciiEffect::setTime() — секунды с момента старта
// приложения, впервые понадобились этому шейдеру именно для анимации
// падения капель (см. bloodDamage/headRow ближе к концу main()).
uniform float uTime;

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// Простой детерминированный псевдослучайный хэш по одному числу —
// используется, чтобы решить, "капает" ли конкретная клетка рамки
// полосы стамины, не завися от кадра/времени (иначе капли бы дёргались).
float hash11(float p) {
    return fract(sin(p * 12.9898) * 43758.5453);
}

// УЛУЧШЕНИЕ ("капли крови по экрану" + "сделать некоторые капли больше,
// а то как в Матрице") — проверяет, идёт ли ПРЯМО СЕЙЧАС капля,
// ПОРОЖДЁННАЯ колонкой sourceCol (не обязательно та же колонка, где
// находится фрагмент — см. вызов с cellIndex.x-1.0 в main(), для правой
// половины крупных капель). requireBig — если true, засчитывается,
// только если ЭТА sourceCol сама оказалась "крупной" (нужно для
// проверки соседней колонки: обычная соседняя капля не должна вылезать
// в чужую клетку, а вот крупная — ровно на одну клетку вправо).
bool evaluateBloodDrop(
    float sourceCol, float row, float totalRowsF, float bloodDamage, bool requireBig,
    vec2 fragPixelIn, float lensR2In, out vec3 outColor
) {
    // Хэш КОЛОНКИ — решает, идёт ли в ней капля вообще (не каждая
    // колонка сразу — иначе получится сплошная кровавая стена вместо
    // отдельных капель), не зависит от времени/кадра (детерминирован
    // по номеру колонки, тот же приём, что и у рамки стамины).
    float colHash = hash11(sourceCol + 517.0);
    const float kMaxActiveColumnFraction = 0.10; // доля активных колонок при максимальном уроне
    if (colHash >= bloodDamage * kMaxActiveColumnFraction)
        return false;

    // ~30% активных капель — крупные (толще и шире, на 2 клетки), 70% —
    // обычные (1 клетка) — именно это разнообразие и было нужно, чтобы
    // не выглядело однородным "дождём символов", как в Матрице.
    float bigHash = hash11(sourceCol + 3301.0);
    bool isBig = bigHash < 0.30;
    if (requireBig && !isBig)
        return false;

    float speedJitter = 0.6 + 0.8 * hash11(sourceCol + 991.0);
    float phase = hash11(sourceCol + 233.0);
    // Крупные капли падают чуть медленнее (тяжелее) и с более длинным
    // хвостом — обычная разница в физике между маленькой и большой
    // каплей, а не только размер головы.
    const float kTrailLengthCells = 5.0;
    const float kFallSpeedCellsPerSec = 7.0;
    float trailLen = isBig ? kTrailLengthCells * 1.4 : kTrailLengthCells;
    float fallSpeed = isBig ? kFallSpeedCellsPerSec * 0.75 : kFallSpeedCellsPerSec;

    float cycleLen = totalRowsF + trailLen;
    float t = fract(uTime * fallSpeed * speedJitter / cycleLen + phase);
    // Голова стартует НАД верхним краем экрана (totalRowsF) и убывает к
    // нижнему (-trailLen) — падает вниз (см. БАГФИКС "текли снизу
    // вверх" в истории правок).
    float headRow = totalRowsF - t * cycleLen;

    // >=0, если ЭТА клетка ВЫШЕ головы капли — капля, падая вниз, уже
    // прошла этот уровень раньше (хвост тянется ВВЕРХ от текущей
    // головы).
    float distBehindHead = row - headRow;
    if (distBehindHead < 0.0 || distBehindHead >= trailLen)
        return false;

    // Крупные капли — "толще": заметно больше рядов рисуются крупным
    // глифом (16) перед переходом на мелкий (17), не только один ряд.
    float headZoneRows = isBig ? 2.4 : 1.3;
    float glyphIdx = (distBehindHead < headZoneRows) ? 16.0 : 17.0;

    vec2 localUV = fract(fragPixelIn / cellSize);
    localUV.y = 1.0 - localUV.y;
    vec2 glyphOrigin = vec2(glyphIdx / minimapGlyphCount, 0.0);
    vec2 glyphUV = glyphOrigin + vec2(localUV.x / minimapGlyphCount, localUV.y);
    float mask = texture(minimapFontTex, glyphUV).r;
    if (mask <= 0.5)
        return false;

    // БАГФИКС ("кровь слишком тёмная, практически не видно") — было
    // 0.80 базового цвета, ×0.5 в хвосте, ×0.30 у края — в худшем
    // случае (хвост капли у самого края экрана) итоговая яркость
    // схлопывалась до 0.12, то есть почти не отличалась от чёрного
    // фона. Смягчил обе кривые затемнения (хвост — максимум ×0.75
    // вместо ×0.5, край — максимум ×0.55 вместо ×0.30) и поднял базовый
    // цвет до полной яркости (1.0 вместо 0.80) — в худшем случае теперь
    // ~0.41 вместо 0.12, то есть капли остаются ЗАМЕТНО темнее у края/
    // в хвосте (эффект сохранён), но уже не растворяются в фоне.
    float trailFade = 1.0 - (distBehindHead / trailLen) * 0.25;
    float edgeDist = clamp(sqrt(lensR2In), 0.0, 1.0);
    float edgeDarken = mix(1.0, 0.55, edgeDist);

    outColor = vec3(1.0, 0.04, 0.04) * trailFade * edgeDarken;
    return true;
}

void main() {
    vec2 fragPixel  = vUV * screenResolution;
    vec2 cellIndex  = floor(fragPixel / cellSize);
    vec2 cellOrigin = cellIndex * cellSize;

    // ---- Линза: общие величины, посчитанные один раз для всего
    // фрагмента, переиспользуются во всех ветках ниже (UI-текст,
    // миникарта, обычный ASCII-рендер сцены). ----
    // LENS_DISTORT_STRENGTH/LENS_VIGNETTE_STRENGTH намеренно небольшие —
    // по ТЗ эффект должен быть едва заметным, а не откровенным "рыбьим
    // глазом".
    const float LENS_DISTORT_STRENGTH  = 0.05;
    // 0.16 оказалось незаметно, 0.55 — слишком сильно (почти весь край
    // экрана чёрный). 0.5 с диапазоном 0.78..1.25 — финальное значение,
    // подобранное вручную (см. историю правок) и подтверждённое как
    // идеально подходящее — НЕ менять без явного запроса.
    const float LENS_VIGNETTE_STRENGTH = 0.5;
    vec2  lensCenterPx   = screenResolution * 0.5;
    // Нормализация по половине высоты (а не по screenResolution целиком)
    // — так круг остаётся кругом независимо от соотношения сторон окна,
    // а не превращается в эллипс на широких мониторах.
    // Нормализация ОТДЕЛЬНО по X и по Y (по половине ширины/высоты
    // экрана каждая, а не обе оси одним числом) — так r=1 приходится
    // ровно на середину любого края экрана (лево/право/верх/низ), а не
    // только на верх/низ. Раньше обе оси делились на screenResolution.y,
    // и на широком мониторе половина ширины оказывалась БОЛЬШЕ половины
    // высоты — то есть край экрана слева/справа находился на r>1 уже
    // намного раньше, чем сверху/снизу, отсюда сильное лишнее
    // затемнение по бокам. Теперь получается овал, вписанный точно по
    // пропорциям окна, а не круг, обрезанный по бокам.
    vec2  lensFromCenter = (fragPixel - lensCenterPx) / (screenResolution * 0.5);
    float lensR2         = dot(lensFromCenter, lensFromCenter);
    float lensVignette   = (lensEffectEnabled > 0.5)
        ? (1.0 - LENS_VIGNETTE_STRENGTH * smoothstep(0.78, 1.25, sqrt(lensR2)))
        : 1.0;

    // УЛУЧШЕНИЕ ("почернение по бокам экрана при получении урона —
    // видимость ХП") — та же радиальная форма, что и у LENS выше
    // (переиспользуем уже посчитанный lensR2 — тот же центр экрана,
    // та же per-axis нормализация под пропорции окна), но сила и
    // радиус зависят от здоровья, а не от фиксированной настройки.
    // healthFrac — уже существующий в этом шейдере uniform (см. его
    // объявление выше и AsciiEffect::setHealth()/Application.cpp) —
    // до этой правки использовался только для капель на рамке полосы
    // стамины (см. dripDensity ниже); теперь та же величина управляет
    // ещё и этой виньеткой — не заводили отдельный uniform-дубликат.
    // При полном HP (1.0) — эффекта нет вообще (healthDamage=0). Чем
    // меньше HP, тем СИЛЬНЕЕ (темнее кайма) и тем БЛИЖЕ К ЦЕНТРУ она
    // начинается — на грани смерти уже заметно "съедает" боковые
    // области экрана, а не только самый край.
    const float HEALTH_VIGNETTE_MAX_STRENGTH = 0.85;
    float healthDamage    = 1.0 - clamp(healthFrac, 0.0, 1.0);
    float healthInnerR    = mix(0.95, 0.35, healthDamage);
    float healthOuterR    = mix(1.25, 0.85, healthDamage);
    float healthVignette  = 1.0 - HEALTH_VIGNETTE_MAX_STRENGTH * healthDamage
        * smoothstep(healthInnerR, healthOuterR, sqrt(lensR2));

    // Обе виньетки (LENS-настройка и HP-индикатор) независимы друг от
    // друга и просто перемножаются — так они складываются естественно,
    // не заменяя одна другую (полное HP + включённый LENS = только
    // лёгкая лязовая виньетка; низкое HP + выключенный LENS = только
    // тревожное затемнение от урона; и то, и другое разом — сильнее).
    float screenVignette = lensVignette * healthVignette;

    // ==================================================
    // UI-текст (заголовок/кнопки меню) — рисуется ПЕРВЫМ и целиком
    // перекрывает всё остальное в своих клетках (return), поэтому
    // всегда оказывается поверх сцены/мини-карты/полосы стамины.
    //
    // БАГФИКС ("sharpness ломает меню") — раньше клетка UI-оверлея
    // (cellIndex) считалась через ЖИВОЙ cellSize сцены (тот же
    // totalCols/totalRows, что и у ASCII-рендера подземелья) — то есть
    // меню было жёстко привязано к тому же размеру клетки, которым
    // теперь управляет слайдер SHARPNESS. Раз меню строится на CPU в
    // СВОЕЙ фиксированной сетке (см. AsciiEffect::kMenuReferenceCellSize),
    // здесь нужна СВОЯ клетка экрана — через нормализованные UV
    // (screenUV * uiCols/uiRows), а не через totalCols/totalRows живой
    // сцены. Теперь оверлей всегда растягивается на весь экран ровно
    // так же, при любом cellSize — полностью развязано.
    // ==================================================
    vec2 screenUV = fragPixel / screenResolution;
    vec2 uiCellF = screenUV * vec2(uiCols, uiRows);
    vec2 uiCellIndex = floor(uiCellF);

    if (uiEnabled > 0.5 &&
        uiCellIndex.x >= 0.0 && uiCellIndex.x < uiCols &&
        uiCellIndex.y >= 0.0 && uiCellIndex.y < uiRows) {

        vec2 localUV = fract(uiCellF);
        localUV.y = 1.0 - localUV.y;

        // uiTex строится на CPU сверху вниз (row 0 = верх экрана), а UV
        // текстур растёт снизу вверх, поэтому строку переворачиваем.
        vec2 uiTexel = (uiCellIndex + 0.5) / vec2(uiCols, uiRows);
        uiTexel.y = 1.0 - uiTexel.y;

        float raw = floor(texture(uiTex, uiTexel).r * 255.0 + 0.5);

        if (raw > 0.5) {
            float glyphIdx = raw - 1.0;
            vec2 glyphOrigin = vec2(glyphIdx / minimapGlyphCount, 0.0);
            vec2 glyphUV = glyphOrigin + vec2(localUV.x / minimapGlyphCount, localUV.y);
            float mask = texture(minimapFontTex, glyphUV).r;
            vec3 bwColor = vec3(step(0.5, mask));

            // Тонировка UI-текста (заголовок/кнопки меню) в цветном режиме —
            // тёплый "состаренный пергамент", подходящий dark-fantasy стилю.
            // Маска (bwColor) остаётся 0/1 — тонировка не добавляет новых
            // "пикселей", только красит уже нарисованный ASCII-символ.
            const vec3 UI_TINT = vec3(0.88, 0.72, 0.38);
            vec3 outColor = mix(bwColor, bwColor * UI_TINT, colorEnabled);

            outColor = mix(outColor, vec3(0.0), fadeAlpha);
            outColor *= screenVignette;
            fragColor = vec4(outColor, 1.0);
            return;
        }
        // raw == 0 -> пусто в этой клетке, показываем сцену как обычно
        // (продолжаем выполнение main() ниже).
    }

    // Цвет/непрозрачность HUD-полосы стамины, если фрагмент попадает в
    // неё (см. блок ниже). Не применяется сразу через return — вместо
    // этого смешивается с обычным ASCII-рендером сцены в самом конце
    // main(), что и даёт плавный fade полосы при входе/выходе из noclip
    // (staminaAlpha меняется непрерывно, а не 0/1, см. AsciiEffect::end()).
    vec3  hudColor = vec3(0.0);
    float hudAlpha = 0.0;

    // ==================================================
    // Мини-карта — рисуется поверх обычного ASCII-рендера
    // 3D-сцены, отдельным (нелуминантным) набором символов,
    // в правом верхнем углу экрана. Карта сама НЕ вращается —
    // вращается только символ игрока внутри неё.
    // ==================================================
    if (minimapGridSize > 0.5) {
        float totalCols = floor(screenResolution.x / cellSize);
        float totalRows = floor(screenResolution.y / cellSize);

        // 1 клетка карты == 1 символьная клетка экрана,
        // поэтому радиус видимого круга — половина стороны окна карты.
        float halfGrid = floor(minimapGridSize * 0.5);
        float interiorRadius = halfGrid;          // сама карта (диск)
        float ringOuterEdge  = halfGrid + 1.0;    // геометрический край обводки
                                                    // (используется для раскладки/отступов)

        // Пустая кайма-буфер вокруг обводки (в 2 раза тоньше,
        // чем было: 1 клетка вместо 2).
        float marginRadius = ringOuterEdge + 1.0;

        // Отступы от краёв экрана: вправо/вверх — до обводки круга.
        // Немного левее и на 2 символа ниже угла, чем раньше.
        const float EDGE_PAD_RIGHT = 2.0; // было 2.0 — сдвиг влево
        const float EDGE_PAD_TOP   = 4.0; // было 2.0 — сдвиг вниз на 2 клетки

        float centerCol = totalCols - EDGE_PAD_RIGHT - ringOuterEdge;
        float centerRow = totalRows - EDGE_PAD_TOP   - ringOuterEdge;

        vec2  off  = cellIndex - vec2(centerCol, centerRow);
        float dist = length(off);

        if (dist <= marginRadius) {
            vec2 localUV = fract(fragPixel / cellSize);
            localUV.y = 1.0 - localUV.y;

            float glyphIdx = -1.0; // -1 = пустота (ничего не рисуем)

            if (dist <= interiorRadius) {
                if (abs(off.x) < 0.5 && abs(off.y) < 0.5) {
                    // Центр круга — здесь всегда символ игрока,
                    // независимо от содержимого карты под ним.
                    // Экран: вправо = +X мира, вверх = -Z мира
                    // (т.к. -Z — начальное направление взгляда игрока).
                    float yawRad = radians(minimapYawDeg);
                    vec2  dir    = vec2(cos(yawRad), -sin(yawRad));

                    float TWO_PI = 6.2831853;
                    float ang = atan(dir.y, dir.x);
                    if (ang < 0.0) ang += TWO_PI;

                    float sector = floor(mod(ang / (TWO_PI / 8.0) + 0.5, 8.0));

                    // Индексы — см. s_minimapGlyphs[] в AsciiEffect.cpp
                    if (sector < 0.5)      glyphIdx = 6.0;  // восток  '>'
                    else if (sector < 1.5) glyphIdx = 5.0;  // СВ      '/'
                    else if (sector < 2.5) glyphIdx = 4.0;  // север   '^'
                    else if (sector < 3.5) glyphIdx = 11.0; // СЗ      '\'
                    else if (sector < 4.5) glyphIdx = 10.0; // запад   '<'
                    else if (sector < 5.5) glyphIdx = 9.0;  // ЮЗ      '/'
                    else if (sector < 6.5) glyphIdx = 8.0;  // юг      'v'
                    else                    glyphIdx = 7.0; // ЮВ      '\'
                } else {
                    // Клетка карты под этой точкой круга. "Вверх на экране"
                    // соответствует УМЕНЬШЕНИЮ мировой Z, поэтому по вертикали
                    // индекс строки текстуры инвертирован относительно off.y.
                    float gx = off.x + halfGrid;
                    float gy = halfGrid - off.y;

                    vec2 mapUV = (vec2(gx, gy) + 0.5) / minimapGridSize;
                    float v = texture(minimapTex, mapUV).r;

                    if (v > 0.9)        glyphIdx = 3.0; // факел
                    else if (v > 0.55)  glyphIdx = 1.0; // стена
                    else if (v > 0.2)   glyphIdx = 2.0; // пол
                    else                 glyphIdx = -1.0; // не разведано / пусто
                }
            } else {
                // Обводка круга рисуется классическим приёмом растеризации
                // окружности: около верха/низа круга берём ближайшую по Y
                // точку идеальной окружности для данного столбца, около
                // левого/правого края — ближайшую по X точку для данной
                // строки. Раньше выбирался ТОЛЬКО ОДИН из этих двух тестов
                // (if ax<=ay ... else ...), и ровно на 0/90/180/270°, где одна
                // из координат (ax или ay) равна нулю, могло получиться так,
                // что срабатывал не тот тест из-за погрешности sqrt() на
                // конкретном GPU — отсюда пропущенный символ ровно на
                // полюсах круга. Фикс: считаем ОБА теста всегда и берём их
                // OR — точке достаточно совпасть хотя бы с одним, поэтому ни
                // одна точка окружности не может остаться непокрытой. На
                // диагоналях оба теста эквивалентны (idealX == idealY при
                // ax == ay), так что линия остаётся толщиной ровно в 1
                // символ, без задвоений.
                float R  = ringOuterEdge;
                float ax = abs(off.x);
                float ay = abs(off.y);

                // Тест по Y осмысленен только пока ax находится в пределах
                // радиуса круга (иначе R*R-ax*ax уходит в отрицательные числа,
                // max(...,0.0) обнуляет его, и idealY=0 может ложно совпасть
                // с ay≈0 у клеток ЗА пределами круга на горизонтальной оси —
                // это и давало паразитные лишние символы точно на 0°/180°).
                // Аналогично тест по X осмысленен только при ay <= R.
                float idealY = sqrt(max(R * R - ax * ax, 0.0));
                float idealX = sqrt(max(R * R - ay * ay, 0.0));
                // Порог чуть шире 0.5 — запас на погрешность sqrt() на разных GPU.
                bool testY = (ax <= R) && (abs(ay - idealY) < 0.55);
                bool testX = (ay <= R) && (abs(ax - idealX) < 0.55);
                bool isBorder = testY || testX;

                if (isBorder) {
                    glyphIdx = 12.0; // обводка круга — символ 'O' (см. s_minimapGlyphs[12])
                }
                // иначе остаётся -1.0 — защитная пустота вокруг круга
            }

            vec3 outColor = vec3(0.0);

            if (glyphIdx >= 0.0) {
                vec2 glyphOrigin = vec2(glyphIdx / minimapGlyphCount, 0.0);
                vec2 glyphUV = glyphOrigin + vec2(localUV.x / minimapGlyphCount, localUV.y);
                float mask = texture(minimapFontTex, glyphUV).r;
                vec3 bwColor = vec3(step(0.5, mask));

                // Тонировка мини-карты по типу клетки/символа в цветном
                // режиме (glyphIdx — см. s_minimapGlyphs[] в .cpp): факел —
                // тёплый оранжевый, стена — холодный камень, пол — тёмная
                // земля, стрелка игрока — яркий бирюзовый, обводка круга —
                // серебро. bwColor остаётся 0/1-маской символа.
                vec3 tint = vec3(1.0);
                if (glyphIdx == 3.0)      tint = vec3(1.00, 0.55, 0.10); // факел
                else if (glyphIdx == 1.0) tint = vec3(0.55, 0.55, 0.62); // стена
                else if (glyphIdx == 2.0) tint = vec3(0.42, 0.34, 0.22); // пол
                else if (glyphIdx >= 4.0 && glyphIdx <= 11.0)
                                            tint = vec3(0.20, 0.90, 0.95); // игрок
                else if (glyphIdx == 12.0) tint = vec3(0.62, 0.62, 0.68); // обводка

                outColor = mix(bwColor, bwColor * tint, colorEnabled);
            }

            outColor = mix(outColor, vec3(0.0), fadeAlpha);
            outColor *= screenVignette;
            fragColor = vec4(outColor, 1.0);
            return;
        }
    }

    // ==================================================
    // Полоса энергии/стамины — растягивается почти на всю
    // ширину низа экрана (65-70% колонок символьной сетки),
    // с рамкой из +/=/| и заполнением из "#".
    // Вся рамка (все 4 стороны) — "кровавый подтёк": рваная линия
    // из капель. Плотность капель зависит от ЗДОРОВЬЯ игрока —
    // при 100% здоровья рамка ровная, чем меньше здоровья, тем
    // сильнее "каплет" по всему периметру (dark fantasy стиль).
    // Заполнение самой полосы (#...) по-прежнему отражает стамину.
    // Рисуется, как и мини-карта, напрямую по cellIndex, поверх
    // обычного ASCII-рендера сцены.
    // ==================================================
    if (staminaAlpha > 0.001) {
        float totalCols = floor(screenResolution.x / cellSize);
        float totalRows = floor(screenResolution.y / cellSize);

        // Внутренняя (заполняемая) ширина полосы — 65-70% ширины экрана.
        float barInnerCols = floor(totalCols * 0.675);
        if (barInnerCols < 4.0) barInnerCols = 4.0;

        float barTotalCols = barInnerCols + 2.0; // + левая/правая граница рамки
        float barStartCol  = floor((totalCols - barTotalCols) * 0.5);
        float barEndCol    = barStartCol + barTotalCols - 1.0;

        // Небольшой отступ от самого низа экрана, чтобы полоса не
        // упиралась в край окна. Высота полосы — 4 строки символов:
        // верхняя/нижняя граница рамки + ДВЕ строки заполнения (чтобы
        // "###" было заметнее, чем при одной тонкой строке).
        const float BOTTOM_PAD = 2.0;
        float rowBottomBorder  = BOTTOM_PAD;
        float rowContentLow    = BOTTOM_PAD + 1.0;
        float rowContentHigh   = BOTTOM_PAD + 2.0;
        float rowTopBorder     = BOTTOM_PAD + 3.0;

        float col = cellIndex.x;
        float row = cellIndex.y;

        if (col >= barStartCol && col <= barEndCol &&
            row >= rowBottomBorder && row <= rowTopBorder) {

            vec2 localUV = fract(fragPixel / cellSize);
            localUV.y = 1.0 - localUV.y;

            bool atLeftEdge  = (col == barStartCol);
            bool atRightEdge = (col == barEndCol);
            bool atTopEdge    = (row == rowTopBorder);
            bool atBottomEdge = (row == rowBottomBorder);

            // Чем меньше здоровья — тем гуще капает по всей рамке.
            // При healthFrac >= 1.0 плотность = 0 (рамка идеально ровная).
            float dripDensity = clamp((1.0 - healthFrac) * 0.9, 0.0, 0.9);

            float glyphIdx;
            if ((atLeftEdge || atRightEdge) && (atTopEdge || atBottomEdge)) {
                // Углы остаются чёткими всегда — иначе рамка "расползается"
                // и перестаёт читаться как прямоугольник.
                glyphIdx = 15.0; // угол рамки '+'
            } else if (atTopEdge || atBottomEdge) {
                // Горизонтальные стороны: каждая колонка — либо ровный
                // кусок линии, либо капля. Разный сдвиг сида у верхней
                // и нижней стороны, чтобы капли не совпадали зеркально.
                float seedOffset = atTopEdge ? 401.0 : 733.0;
                float rnd = hash11(col + seedOffset);

                if (rnd < dripDensity * 0.6) {
                    glyphIdx = 16.0; // крупная капля
                } else if (rnd < dripDensity) {
                    glyphIdx = 17.0; // мелкая капля
                } else {
                    glyphIdx = 13.0; // ровный кусок линии
                }
            } else if (atLeftEdge || atRightEdge) {
                // Вертикальные стороны: то же самое, но по строкам, со
                // своим сдвигом сида для левой/правой стороны.
                float seedOffset = atLeftEdge ? 157.0 : 911.0;
                float rnd = hash11(row + seedOffset);

                if (rnd < dripDensity * 0.6) {
                    glyphIdx = 16.0; // крупная капля
                } else if (rnd < dripDensity) {
                    glyphIdx = 17.0; // мелкая капля
                } else {
                    glyphIdx = 14.0; // ровный кусок вертикальной линии '|'
                }
            } else {
                // Содержимое полосы: заполненные клетки слева направо.
                // Заполнение по-прежнему отражает стамину, а не здоровье.
                float innerIndex  = col - (barStartCol + 1.0); // 0..barInnerCols-1
                float filledCols  = floor(clamp(staminaFrac, 0.0, 1.0) * barInnerCols + 0.5);
                glyphIdx = (innerIndex < filledCols) ? 1.0 : 0.0; // '#' или пусто
            }

            vec3 outColor = vec3(0.0);
            if (glyphIdx >= 0.0) {
                vec2 glyphOrigin = vec2(glyphIdx / minimapGlyphCount, 0.0);
                vec2 glyphUV = glyphOrigin + vec2(localUV.x / minimapGlyphCount, localUV.y);
                float mask = texture(minimapFontTex, glyphUV).r;
                vec3 bwColor = vec3(step(0.5, mask));

                // Тонировка полосы стамины/здоровья в цветном режиме:
                // рамка — тёмно-кровавый красный (усиливается вместе с
                // "капаньем", см. dripDensity выше), заполнение — прямой
                // двухцветный переход красный (почти пустая стамина) ->
                // синий (полная), в холодной гамме, близкой к бирюзовой
                // стрелке игрока на компасе/мини-карте. Раньше здесь была
                // ещё и янтарная середина, но прямая линейная интерполяция
                // RGB между жёлтым и синим на середине пути даёт паразитный
                // блёкло-зелёный оттенок — поэтому средний цвет убран, и
                // остался только чистый переход красный->синий.
                bool isFrameGlyph = (atLeftEdge || atRightEdge || atTopEdge || atBottomEdge);
                vec3 tint;
                if (isFrameGlyph) {
                    tint = vec3(0.55, 0.05, 0.05);
                } else {
                    vec3 lowColor  = vec3(0.80, 0.12, 0.10); // почти пусто — красный
                    vec3 highColor = vec3(0.20, 0.65, 0.95); // полно — синий
                    float s = clamp(staminaFrac, 0.0, 1.0);
                    tint = mix(lowColor, highColor, s);
                }

                outColor = mix(bwColor, bwColor * tint, colorEnabled);
            }

            // Не возвращаемся сразу: сохраняем цвет/альфу полосы и даём
            // шейдеру ниже как обычно посчитать ASCII-рендер сцены под
            // ней — так при staminaAlpha < 1 сквозь полосу будет видно
            // (плавно) то, что обычно рисуется в этой клетке.
            hudColor = outColor;
            hudAlpha = staminaAlpha;
        }
    }

    // Perf note: this used to be a 3x3 (S=3) box filter of sceneTex
    // computed by hand, INLINE, for every single screen pixel — but its
    // result only depends on cellOrigin/lens state, both constant across
    // every pixel inside the same glyph cell (cellSize x cellSize of
    // them). That meant e.g. with cellSize=8 every 64-pixel cell was
    // redoing the identical 9 texture fetches 64 times over (~576 fetches
    // per cell instead of 9). Replaced with a single hardware-mipmapped
    // lookup: sceneTex gets its mip chain generated once per frame right
    // after the 3D pass (see AsciiEffect::end(), glGenerateMipmap), and
    // sampling at the mip level matching cellSize gives the same "average
    // color of this region" a box filter computes, but built by the GPU's
    // dedicated downsampling hardware once, not by this shader 64x over.
    vec2 samplePx = cellOrigin + vec2(0.5) * cellSize; // cell center

    // Линза: сдвигаем ТОЧКУ СЭМПЛИРОВАНИЯ картинки сцены наружу от центра
    // пропорционально квадрату расстояния — обычная формула баррел-
    // дисторсии. Сетка ASCII-ячеек (cellIndex/cellOrigin) при этом не
    // трогается: смещается только то, ЧТО показывается внутри клетки, а
    // не сама клетка. Раньше это смещение считалось для каждой из 9
    // подвыборок отдельно; теперь — один раз для центра клетки, что даёт
    // ту же дисторсию картинки на глаз (сама дисторсия почти не меняется
    // в пределах одной маленькой ASCII-клетки).
    if (lensEffectEnabled > 0.5) {
        vec2 fromCenter = (samplePx - lensCenterPx) / (screenResolution * 0.5);
        float k = 1.0 + LENS_DISTORT_STRENGTH * dot(fromCenter, fromCenter);
        samplePx = lensCenterPx + fromCenter * k * (screenResolution * 0.5);
    }

    vec2 uv = samplePx / screenResolution;
    float mipLevel = log2(max(cellSize, 1.0));
    vec3 c = textureLod(sceneTex, uv, mipLevel).rgb;

    float lum = luminance(c);
    vec3 colorSum = c;

    lum = pow(clamp(lum, 0.0, 1.0), 0.75);

    // Упорядоченный дизеринг (Bayer 4x4) — разбивает большие плоские
    // зоны яркости на локально varying узор символов, как в референсе.
    const float bayer[16] = float[](
        0.0,  8.0,  2.0, 10.0,
       12.0,  4.0, 14.0,  6.0,
        3.0, 11.0,  1.0,  9.0,
       15.0,  7.0, 13.0,  5.0
    );
    int bx = int(mod(fragPixel.x, 4.0));
    int by = int(mod(fragPixel.y, 4.0));
    float dither = (bayer[by * 4 + bx] / 16.0) - 0.5;
    lum = clamp(lum + dither * (1.0 / rampLength) * 1.5, 0.0, 1.0);

    float glyphIndex = floor(clamp(lum, 0.0, 0.999) * rampLength);

    // Локальные координаты фрагмента внутри ячейки (0..1) —
    // именно это даёт "нарезку" глифа по границе физического пиксельного
    // блока, а не по сетке терминала: cellOrigin взят из непрерывной,
    // перспективно-корректной позиции фрагмента на экране.
    vec2 localUV = fract(fragPixel / cellSize);
    localUV.y = 1.0 - localUV.y;

    vec2 glyphOrigin = vec2(glyphIndex / rampLength, 0.0);
    vec2 glyphUV = glyphOrigin + vec2(localUV.x / rampLength, localUV.y);

    float mask = texture(fontTex, glyphUV).r;
    float bw = step(0.5, mask); // жёсткий порог -> строгий 1-bit

    // Цвет ASCII-глифа в цветном режиме: берём ОТТЕНОК (hue) отфильтрованного
    // цвета сцены — нормализуем по максимальному каналу, а не по самой
    // яркости, потому что яркость УЖЕ выражена выбором самого символа
    // (glyphIndex/плотность глифа, см. выше). Если цвет нормализовать ещё
    // и по яркости, то тёмные (но цветные, например факелы вдалеке) зоны
    // рисовались бы блёкло-серыми, теряя цвет — а так glyph остаётся
    // тёмным/редким там, где сцена тёмная, но всё равно правильного оттенка.
    float maxChannel = max(max(colorSum.r, colorSum.g), colorSum.b);
    vec3 hueColor = maxChannel > 0.001 ? (colorSum / maxChannel) : vec3(1.0);
    // Лёгкая примесь белого — иначе чистые оттенки выглядят слишком
    // "кислотно" на однобитных ASCII-символах.
    hueColor = mix(hueColor, vec3(1.0), 0.12);

    vec3 glyphTint = mix(vec3(1.0), hueColor, colorEnabled);
    vec3 sceneColor = vec3(bw) * glyphTint;

    // Смешиваем обычный ASCII-рендер сцены с HUD-полосой стамины (если
    // она вообще что-то нарисовала в этой клетке — см. начало main()).
    // hudAlpha меняется плавно (не 0/1), поэтому и переход виден плавно.
    vec3 finalColor = mix(sceneColor, hudColor, hudAlpha);
    finalColor = mix(finalColor, vec3(0.0), fadeAlpha);
    finalColor *= screenVignette;

    // ---- УЛУЧШЕНИЕ ("капли крови по экрану, снова и снова, пока не
    // восстановит ХП" + "сделать некоторые капли больше, а то как в
    // Матрице") — переиспользует те же глифы (16/17, "крупная"/"мелкая
    // капля") и тот же hash11(), что уже были заведены для капель на
    // рамке полосы стамины выше (см. dripDensity) — только теперь на
    // весь экран, не только на рамке, и не статично: капли ДВИЖУТСЯ
    // вниз (см. uTime) и зацикливаются — как только дошли до низа
    // экрана, тут же падают заново сверху, бесконечно, пока healthFrac
    // не вернётся к 1.0.
    float bloodDamage = 1.0 - clamp(healthFrac, 0.0, 1.0);
    if (bloodDamage > 0.001) {
        float totalRowsF = floor(screenResolution.y / cellSize);
        vec3 bloodColor;
        // Сначала — обычная проверка "не идёт ли капля прямо в МОЕЙ
        // колонке" (см. evaluateBloodDrop() выше, requireBig=false —
        // подходит и обычная, и крупная капля). Если нет — вторая
        // попытка: а вдруг я являюсь ПРАВОЙ половиной КРУПНОЙ капли из
        // колонки СЛЕВА (requireBig=true — считается только если та
        // колонка сама оказалась крупной) — так крупные капли получают
        // реальную ширину в 2 клетки, а не просто другой глиф в той же
        // одной клетке (иначе выглядело бы одинаково "по-матрице",
        // независимо от глифа).
        if (evaluateBloodDrop(cellIndex.x, cellIndex.y, totalRowsF, bloodDamage, false, fragPixel, lensR2, bloodColor)) {
            finalColor = bloodColor;
        } else if (evaluateBloodDrop(cellIndex.x - 1.0, cellIndex.y, totalRowsF, bloodDamage, true, fragPixel, lensR2, bloodColor)) {
            finalColor = bloodColor;
        }
    }

    fragColor = vec4(finalColor, 1.0);
}
