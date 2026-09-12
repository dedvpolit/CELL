#include "SceneGeometry.h"
#include "WallShapes.h"
#include "Columns.h"
#include "Zoning.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>

// Высота стен во всём файле — было разбросано как повторяющийся магический
// литерал 30.0f в нескольких местах (BuildFloorAndWallsGreedy, AABB чанков);
// вынесено в одну константу, т.к. с добавлением колонн (тоже высотой во всю
// стену) число мест дублирования увеличилось бы дальше.
static constexpr float kWallHeight = 30.0f;

// Палитра по зоне (см. Zoning::ZoneStyle::paletteIndex) — дискретный набор
// преднастроенных пар цветов стена/пол, а не непрерывный множитель по
// каждому каналу (чтобы гарантированно избежать мутных случайных
// оттенков — см. комментарий в Zoning.h). Индекс 0 — тот же цвет, что был
// в движке ДО зонирования (тёплый коричневый камень), остальные —
// заметно другие "темы" региона.
void SceneGeometry::GetZonePalette(int paletteIndex, glm::vec3& outWallColor, glm::vec3& outFloorColor) {
    static const glm::vec3 kPresets[Zoning::kPaletteCount][2] = {
        { glm::vec3(0.55f, 0.35f, 0.25f), glm::vec3(0.18f, 0.16f, 0.13f) }, // 0: тёплый коричневый (исходный)
        { glm::vec3(0.30f, 0.33f, 0.38f), glm::vec3(0.10f, 0.11f, 0.14f) }, // 1: холодный сине-серый камень
        { glm::vec3(0.28f, 0.38f, 0.24f), glm::vec3(0.10f, 0.14f, 0.10f) }, // 2: замшелый (приглушённый) зелёный
        { glm::vec3(0.42f, 0.40f, 0.38f), glm::vec3(0.15f, 0.14f, 0.13f) }, // 3: пепельно-серый
        { glm::vec3(0.50f, 0.28f, 0.22f), glm::vec3(0.17f, 0.12f, 0.10f) }, // 4: красноватая глина
    };
    const int idx = ((paletteIndex % Zoning::kPaletteCount) + Zoning::kPaletteCount) % Zoning::kPaletteCount;
    outWallColor = kPresets[idx][0];
    outFloorColor = kPresets[idx][1];
}

static void AddQuad(std::vector<Vertex>& verts, std::vector<GLuint>& indices,
                            glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                            glm::vec3 normal, glm::vec3 color, float matId = 0.0f) {
    // 4 unique corner vertices + 6 indices (two triangles) instead of the
    // old 6 duplicated vertices — same two triangles, a third less data.
    GLuint base = (GLuint)verts.size();
    verts.push_back({a, normal, color, matId});
    verts.push_back({b, normal, color, matId});
    verts.push_back({c, normal, color, matId});
    verts.push_back({d, normal, color, matId});

    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
    indices.push_back(base + 0);
}

void AddCylinder(std::vector<Vertex>& verts, std::vector<GLuint>& indices, glm::vec3 base, glm::vec3 tip,
                                float radiusBase, float radiusTip, int segments,
                                glm::vec3 color, float matId) {
    glm::vec3 axis = glm::normalize(tip - base);
    glm::vec3 upHint = std::abs(axis.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 side = glm::normalize(glm::cross(axis, upHint));
    glm::vec3 fwd  = glm::normalize(glm::cross(side, axis));

    for (int i = 0; i < segments; i++) {
        float a0 = (float)i / segments * glm::two_pi<float>();
        float a1 = (float)(i + 1) / segments * glm::two_pi<float>();

        glm::vec3 dir0 = side * std::cos(a0) + fwd * std::sin(a0);
        glm::vec3 dir1 = side * std::cos(a1) + fwd * std::sin(a1);

        glm::vec3 rb0 = base + dir0 * radiusBase;
        glm::vec3 rb1 = base + dir1 * radiusBase;
        glm::vec3 rt0 = tip  + dir0 * radiusTip;
        glm::vec3 rt1 = tip  + dir1 * radiusTip;

        // rb0/rb1 have different normals (dir0/dir1) from rt0/rt1's
        // matching pair, so unlike a flat quad we can't share all 4
        // corners between the two triangles of this segment (rb0 is used
        // with normal dir0 in one triangle only). We still dedup the two
        // vertices that ARE shared between the segment's two triangles
        // (rb0+dir0 and rt1+dir1), which is as far as this can go without
        // splitting normals across triangles (i.e. without per-vertex
        // smooth normals, which would change the look of the mesh).
        GLuint base0 = (GLuint)verts.size();
        verts.push_back({rb0, dir0, color, matId}); // base0 + 0
        verts.push_back({rb1, dir1, color, matId}); // base0 + 1
        verts.push_back({rt1, dir1, color, matId}); // base0 + 2
        verts.push_back({rt0, dir0, color, matId}); // base0 + 3

        indices.push_back(base0 + 0);
        indices.push_back(base0 + 1);
        indices.push_back(base0 + 2);
        indices.push_back(base0 + 0);
        indices.push_back(base0 + 2);
        indices.push_back(base0 + 3);
    }
}

void AddSphere(std::vector<Vertex>& verts, std::vector<GLuint>& indices, glm::vec3 center, float radius,
                              int segments, int rings, glm::vec3 color, float matId) {
    auto sphPoint = [](float u, float v) {
        return glm::vec3(std::sin(v) * std::cos(u), std::cos(v), std::sin(v) * std::sin(u));
    };

    for (int r = 0; r < rings; r++) {
        float v0 = (float)r / rings * glm::pi<float>();
        float v1 = (float)(r + 1) / rings * glm::pi<float>();
        for (int s = 0; s < segments; s++) {
            float u0 = (float)s / segments * glm::two_pi<float>();
            float u1 = (float)(s + 1) / segments * glm::two_pi<float>();

            glm::vec3 p00 = sphPoint(u0, v0), p10 = sphPoint(u1, v0);
            glm::vec3 p01 = sphPoint(u0, v1), p11 = sphPoint(u1, v1);

            // Sphere normals are per-vertex-position (p00/p10/p01/p11 are
            // each used as both position and normal direction), so all 4
            // corners of this quad-shaped patch ARE fully shareable
            // between its two triangles — same dedup as addQuad.
            GLuint base0 = (GLuint)verts.size();
            verts.push_back({center + p00 * radius, p00, color, matId}); // 0
            verts.push_back({center + p10 * radius, p10, color, matId}); // 1
            verts.push_back({center + p11 * radius, p11, color, matId}); // 2
            verts.push_back({center + p01 * radius, p01, color, matId}); // 3

            indices.push_back(base0 + 0);
            indices.push_back(base0 + 1);
            indices.push_back(base0 + 2);
            indices.push_back(base0 + 0);
            indices.push_back(base0 + 2);
            indices.push_back(base0 + 3);
        }
    }
}

static void AddTorchMesh(
    std::vector<Vertex>& verts,
    std::vector<GLuint>& indices,
    glm::vec3 wallBase,
    glm::vec3 normal,
    int torchIndex)
{
    // --------------------------------------------------
    // Ручка факела
    // --------------------------------------------------
    // Высота и вылет от стены ЗДЕСЬ должны совпадать с расчётом flamePos
    // в tryAddTorch() выше (см. комментарий там же про баг "факел висит
    // в воздухе") — иначе пламя (сфера ниже) и источник света/частицы
    // (используют flamePos) окажутся в разных местах.
    //
    // Вылет ОТРИЦАТЕЛЬНЫЙ (-0.05) — низ держателя намеренно уходит ЗА
    // лицевую поверхность стены, вглубь её тела, где его перекрывает
    // собственно меш стены. Это и даёт эффект "рукоять растёт прямо из
    // камня", а не висит перед стеной в воздухе (положительное смещение
    // здесь давало именно такой "висящий" эффект, см. историю правок).

    glm::vec3 base =
        wallBase
        + normal * -0.015f
        + glm::vec3(
            0.0f,
            0.50f,
            0.0f
        );

    // --------------------------------------------------
    // Верхушка факела
    // --------------------------------------------------
    // Вылет отсюда должен быть ЗАМЕТНО больше радиуса сферы пламени
    // (0.065, см. addSphere ниже) — иначе центр сферы окажется ближе к
    // стене, чем её собственный радиус, и ближняя половина огонька будет
    // физически "утоплена" в стене (визуально — пламя касается/врастает
    // в камень). 0.14 даёт сфере ~0.075 чистого зазора от поверхности
    // стены — держатель по-прежнему прижат к стене (см. base выше), а
    // пламя аккуратно "висит" перед ней, не проваливаясь внутрь.
    glm::vec3 tip =
        wallBase
        + normal * 0.14f
        + glm::vec3(
            0.0f,
            0.62f,
            0.0f
        );

    glm::vec3 handleColor(
        0.34f,
        0.25f,
        0.16f
    );

    glm::vec3 flameColor(
        1.0f,
        0.60f,
        0.15f
    );

    // Держатель сужен и укорочен (радиус 0.055->0.038 у основания,
    // 0.035->0.022 у верха) — раньше на тонкой "спичке" сидел непропорц
    // ионально крупный шар пламени (см. addSphere ниже, радиус там тоже
    // уменьшен), из-за чего факел выглядел как воздушный шарик на палочке.
    AddCylinder(
        verts,
        indices,
        base,
        tip,
        0.038f,
        0.022f,
        8,
        handleColor,
        1.0f
    );

    // --------------------------------------------------
    // Пламя
    //
    // ID цвета для шейдера:
    //     2.0 + torchIndex / 100
    //
    // Само шейдер использует ID пламени.
    // --------------------------------------------------

    // --------------------------------------------------
    // Вычисление ID пламени.
    //
    // Итоговое значение 2.0 .. <2.25,
    // чтобы даже при MAX_TORCHES = 1024
    // не залезать за границу материала 2.5.
    // --------------------------------------------------


    // addTorchMesh
    const float FLAME_ID_SCALE = 4096.0f;

    float flameMatId =
        2.0f
        + static_cast<float>(torchIndex)
          / FLAME_ID_SCALE;

    // Сфера пламени уменьшена (0.10 -> 0.065) и опущена ближе к верхушке
    // держателя (0.06 -> 0.045), чтобы пропорции факела читались как
    // единое целое — тонкий держатель с компактным огоньком, а не
    // маленькая палка с непропорционально огромным шаром сверху.
    AddSphere(
        verts,
        indices,
        tip
        + glm::vec3(
            0.0f,
            0.045f,
            0.0f
        ),
        0.065f,
        8,
        5,
        flameColor,
        flameMatId
    );
}

// [comment corrupted in source file - original text lost/unrecoverable]
//
// [comment corrupted in source file - original text lost/unrecoverable]
// [comment corrupted in source file - original text lost/unrecoverable]
// [comment corrupted in source file - original text lost/unrecoverable]
// [comment corrupted in source file - original text lost/unrecoverable]
static void AddWinButtonMesh(std::vector<Vertex>& verts, std::vector<GLuint>& indices,
                              const glm::vec3& winButtonPos)
{
    const glm::vec3 base = winButtonPos;

    const glm::vec3 pedestalColor(0.55f, 0.42f, 0.12f);  // [comment corrupted in source file - original text lost/unrecoverable]
    const glm::vec3 capColor(0.25f, 0.95f, 0.35f);  // [comment corrupted in source file - original text lost/unrecoverable]

    // [comment corrupted in source file - original text lost/unrecoverable]
    AddCylinder(
        verts,
        indices,
        base,
        base + glm::vec3(0.0f, 0.9f, 0.0f),
        0.35f,
        0.30f,
        16,
        pedestalColor,
        0.0f
    );

    // [comment corrupted in source file - original text lost/unrecoverable]
    AddCylinder(
        verts,
        indices,
        base + glm::vec3(0.0f, 0.9f, 0.0f),
        base + glm::vec3(0.0f, 1.05f, 0.0f),
        0.30f,
        0.22f,
        16,
        pedestalColor,
        0.0f
    );

    // [comment corrupted in source file - original text lost/unrecoverable]
    AddSphere(
        verts,
        indices,
        base + glm::vec3(0.0f, 1.18f, 0.0f),
        0.16f,
        14,
        10,
        capColor,
        0.0f
    );
}

// Простой пропс "раскрытый дневник" на полу кармана (см. Diaries.h) —
// плоская обложка + две "страницы" по бокам от корешка, всё на floor-
// высоте (небольшой y-оффсет от 0, чтобы не z-fight'ить с полом). Не
// текстурировано — плоские цвета, как и весь остальной меш в этом файле
// (matId=0.0f — обычная непрозрачная геометрия, без спецэффекта).
// Игрок не поднимает и не убирает эту геометрию — она печётся один раз в
// build() и остаётся в мире и после того, как дневник прочитан (см.
// DungeonScene::m_diariesRead — это только UI-флаг, не геометрия).
// Маленький самостоятельный бокс (верх + 4 борта, низ не нужен — всё
// стоит на полу либо зажато другими частями книги, см. AddDiaryMesh()
// ниже) с одним цветом на все грани. Та же вершинная развёртка
// (лево-право-право-лево по каждой грани), что и у стен/срезов в
// остальном файле (см. AddChamferedWallCorner выше).
static void AddBoxNoBottom(std::vector<Vertex>& verts, std::vector<GLuint>& indices,
                            float x0, float x1, float y0, float y1, float z0, float z1,
                            const glm::vec3& color)
{
    AddQuad(verts, indices, // верх
            glm::vec3(x0, y1, z0), glm::vec3(x1, y1, z0),
            glm::vec3(x1, y1, z1), glm::vec3(x0, y1, z1),
            glm::vec3(0, 1, 0), color, 0.0f);
    AddQuad(verts, indices, // юг
            glm::vec3(x0, y0, z0), glm::vec3(x1, y0, z0),
            glm::vec3(x1, y1, z0), glm::vec3(x0, y1, z0),
            glm::vec3(0, 0, -1), color, 0.0f);
    AddQuad(verts, indices, // север
            glm::vec3(x0, y0, z1), glm::vec3(x1, y0, z1),
            glm::vec3(x1, y1, z1), glm::vec3(x0, y1, z1),
            glm::vec3(0, 0, 1), color, 0.0f);
    AddQuad(verts, indices, // запад
            glm::vec3(x0, y0, z0), glm::vec3(x0, y0, z1),
            glm::vec3(x0, y1, z1), glm::vec3(x0, y1, z0),
            glm::vec3(-1, 0, 0), color, 0.0f);
    AddQuad(verts, indices, // восток
            glm::vec3(x1, y0, z0), glm::vec3(x1, y0, z1),
            glm::vec3(x1, y1, z1), glm::vec3(x1, y1, z0),
            glm::vec3(1, 0, 0), color, 0.0f);
}

static void AddDiaryMesh(std::vector<Vertex>& verts, std::vector<GLuint>& indices,
                          const glm::vec3& diaryPos)
{
    // БАГФИКС 2 ("модель — просто кирпич, нет читаемого силуэта обложки/
    // корешка") — прошлая версия была одним сплошным боксом одного
    // размера: цвет менялся по граням, но геометрически это был ровный
    // параллелепипед, никакого настоящего перепада силуэта. Теперь —
    // три РАЗНЫХ по размеру объёма, как у настоящей книги:
    //   - корешок — сплошной, во всю высоту, вдоль западного края;
    //   - обложка — верхняя и нижняя "крышки" переплёта, БОЛЬШИЕ (во всю
    //     ширину/глубину книги, кроме корешка), нависают над листами;
    //   - листы — блок МЕНЬШЕ обложки (утоплен внутрь на 3 стороны, не
    //     со стороны корешка), зажат между крышками по высоте.
    // Разница размеров между обложкой и утопленными листами и даёт
    // настоящий видимый перепад силуэта — не просто смену цвета на
    // плоской грани, как раньше.
    //
    // Размер общего объёма — на ~15% меньше предыдущей версии (по
    // отзыву: "уменьшить модельку на 10-20%"; было halfW=0.17/halfD=
    // 0.12/thickness=0.09).
    const float halfW = 0.145f;        // половина ширины книги вдоль X (включая корешок)
    const float halfD = 0.10f;         // половина глубины вдоль Z
    const float totalThickness = 0.075f;
    const float coverPlate = 0.010f;   // толщина каждой "крышки" переплёта (верх/низ)
    const float spineWidth = 0.022f;   // ширина корешка вдоль X
    const float pageInset = 0.016f;    // насколько листы утоплены от края обложки (E/S/N — не со стороны корешка)

    // БАГФИКС 4 ("выглядит как альбом первоклассника, а не книга") —
    // раньше корешок шёл вдоль КОРОТКОЙ стороны (halfD=0.10, глубина
    // 0.20), а книга "раскрывалась" вдоль ДЛИННОЙ (halfW=0.145, ширина
    // 0.29) — у настоящей книги наоборот: корешок идёт вдоль ДЛИННОЙ
    // грани страницы, а раскрытие — вдоль короткой. С перепутанными
    // осями силуэт читался как landscape-блокнот/альбом, а не книга.
    // Переставил корешок на грань Z (вдоль длинной X-стороны).
    //
    // Плюс раньше корешок был ТЕМ ЖЕ цветом, что и обложка — сверху (а
    // игрок чаще всего смотрит на книгу именно сверху-сбоку) это давало
    // один сплошной прямоугольник без единой контрастной детали,
    // усиливая впечатление "просто цветной блокнот". Теперь у корешка
    // отдельный, заметно более тёмный оттенок — видимая полоса-акцент.
    const glm::vec3 coverColor(0.58f, 0.22f, 0.14f); // обложка — выцветший бордовый
    const glm::vec3 spineColor(0.28f, 0.08f, 0.05f); // корешок — заметно темнее, отдельная деталь
    const glm::vec3 pageColor(0.82f, 0.75f, 0.58f);  // утопленный срез листов

    const float xOuter0 = diaryPos.x - halfW;
    const float xOuter1 = diaryPos.x + halfW;
    const float zOuter0 = diaryPos.z - halfD;
    const float zOuter1 = diaryPos.z + halfD;
    const float yBase   = diaryPos.y;
    const float yTop    = yBase + totalThickness;

    // Корешок — сплошной, во всю высоту книги, вдоль южной грани (по
    // ДЛИННОЙ X-стороне — см. комментарий выше).
    AddBoxNoBottom(verts, indices,
                    xOuter0, xOuter1, yBase, yTop, zOuter0, zOuter0 + spineWidth,
                    spineColor);

    // Обложка: верхняя и нижняя "крышки" переплёта — во всю ширину, по
    // оставшейся глубине (корешок уже занял свою полосу выше), накрывают
    // блок листов и нависают над ним по трём открытым сторонам — именно
    // этот нависающий край и читается как силуэт закрытой книги.
    AddBoxNoBottom(verts, indices,
                    xOuter0, xOuter1, yTop - coverPlate, yTop, zOuter0 + spineWidth, zOuter1,
                    coverColor);
    AddBoxNoBottom(verts, indices,
                    xOuter0, xOuter1, yBase, yBase + coverPlate, zOuter0 + spineWidth, zOuter1,
                    coverColor);

    // Листы — утоплены внутрь от обложки на 3 стороны (запад/восток и
    // северный "обрез"; со стороны корешка утапливать нечего — там
    // сплошной клеевой блок), зажаты между крышками ровно по высоте.
    AddBoxNoBottom(verts, indices,
                    xOuter0 + pageInset, xOuter1 - pageInset,
                    yBase + coverPlate, yTop - coverPlate,
                    zOuter0 + spineWidth, zOuter1 - pageInset,
                    pageColor);
}


// ---------------- Шаг 1: срезанные углы стен (см. WallShapes.h) ----------------
//
// Одна клетка со срезанным углом — НЕ часть greedy-меша (её грани не
// одной длины со всей "простынёй" соседних клеток), поэтому она строится
// целиком отдельно, до/вместо основных 4 проходов ниже. Greedy-проходы
// просто пропускают такие клетки (см. cornerCuts[...] != None в условиях
// runов ниже) — эта функция уже эмитит все их грани сама.
//
// Эмитятся ровно те же 4 боковые грани, что построил бы обычный
// прямоугольный код (с той же проверкой открытости соседа), только две из
// них (примыкающие к срезанному углу) укорочены до точки среза, плюс
// одна новая диагональная грань клина — она всегда экспонирована, т.к.
// BuildCornerCuts() гарантирует, что оба ортогональных соседа угла — пол
// (см. комментарий в WallShapes.h про eligibility).
//
// ВАЖНО (найдено визуальной проверкой рендера против аналитической
// коллизии — см. историю правок): сам клин, вырезанный из клетки стены,
// не покрыт floor-геометрией — floor-проход ниже строится ЧИСТО по
// map[]==2 клеткам и ничего не знает про срезы. Без явного патча игрок
// мог физически зайти в клин (коллизия это разрешает), но увидел бы под
// ногами дыру без пола. Поэтому здесь же, вместе со стеновыми гранями
// клетки, добавляется и floor-треугольник самого клина (pFrom-pTo-угол),
// на y=0, тем же floorColor — держим "рендер по клетке" целиком в одном
// месте, а не размазываем полы срезанных клеток по отдельному проходу.
static void AddChamferedWallCell(
    std::vector<Vertex>& verts, std::vector<GLuint>& indices,
    int x, int z, WallShapes::CornerCut cut, float chamferSize,
    const std::function<bool(int, int)>& isWall,
    const glm::vec3& wallColor, const glm::vec3& floorColor, float y0, float y1)
{
    using WallShapes::CornerCut;

    WallShapes::ChamferPoints cp;
    if (!WallShapes::GetChamferPoints(cut, chamferSize, cp)) {
        return; // cut == None не должно сюда попадать (см. вызывающий код)
    }

    const float xf = (float)x, zf = (float)z, z1 = zf + 1.0f, x1 = xf + 1.0f;

    // Точки среза в мировых координатах (см. комментарии в WallShapes.h:
    // onEdgeCcwFrom/onEdgeCcwTo — какая точка на какой исходной грани
    // лежит для каждого cut, разобрано там же).
    const glm::vec3 pFrom(xf + cp.onEdgeCcwFrom.x, 0.0f, zf + cp.onEdgeCcwFrom.y);
    const glm::vec3 pTo(xf + cp.onEdgeCcwTo.x,   0.0f, zf + cp.onEdgeCcwTo.y);

    // -------- Южная грань (z = zf), укорочена для SW/SE --------
    if (isWall(x, z) && !isWall(x, z - 1)) {
        const float xStart = (cut == CornerCut::SW) ? pTo.x   : xf;
        const float xEnd   = (cut == CornerCut::SE) ? pFrom.x : x1;
        if (xEnd > xStart) {
            AddQuad(verts, indices,
                glm::vec3(xStart, y0, zf), glm::vec3(xEnd, y0, zf),
                glm::vec3(xEnd, y1, zf), glm::vec3(xStart, y1, zf),
                glm::vec3(0, 0, -1), wallColor);
        }
    }
    // -------- Северная грань (z = zf+1), укорочена для NW/NE --------
    if (isWall(x, z) && !isWall(x, z + 1)) {
        const float xStart = (cut == CornerCut::NW) ? pFrom.x : xf;
        const float xEnd   = (cut == CornerCut::NE) ? pTo.x   : x1;
        if (xEnd > xStart) {
            AddQuad(verts, indices,
                glm::vec3(xStart, y0, z1), glm::vec3(xEnd, y0, z1),
                glm::vec3(xEnd, y1, z1), glm::vec3(xStart, y1, z1),
                glm::vec3(0, 0, 1), wallColor);
        }
    }
    // -------- Западная грань (x = xf), укорочена для SW/NW --------
    if (isWall(x, z) && !isWall(x - 1, z)) {
        const float zStart = (cut == CornerCut::SW) ? pFrom.z : zf;
        const float zEnd   = (cut == CornerCut::NW) ? pTo.z   : z1;
        if (zEnd > zStart) {
            AddQuad(verts, indices,
                glm::vec3(xf, y0, zStart), glm::vec3(xf, y0, zEnd),
                glm::vec3(xf, y1, zEnd), glm::vec3(xf, y1, zStart),
                glm::vec3(-1, 0, 0), wallColor);
        }
    }
    // -------- Восточная грань (x = xf+1), укорочена для SE/NE --------
    if (isWall(x, z) && !isWall(x + 1, z)) {
        const float zStart = (cut == CornerCut::SE) ? pTo.z   : zf;
        const float zEnd   = (cut == CornerCut::NE) ? pFrom.z : z1;
        if (zEnd > zStart) {
            AddQuad(verts, indices,
                glm::vec3(x1, y0, zStart), glm::vec3(x1, y0, zEnd),
                glm::vec3(x1, y1, zEnd), glm::vec3(x1, y1, zStart),
                glm::vec3(1, 0, 0), wallColor);
        }
    }

    // -------- Диагональная грань среза — всегда есть (см. шапку функции) --------
    glm::vec3 diagNormal(0.0f);
    switch (cut) {
        case CornerCut::SW: diagNormal = glm::vec3(-1, 0, -1); break;
        case CornerCut::SE: diagNormal = glm::vec3(1, 0, -1);  break;
        case CornerCut::NE: diagNormal = glm::vec3(1, 0, 1);   break;
        case CornerCut::NW: diagNormal = glm::vec3(-1, 0, 1);  break;
        default: break;
    }
    diagNormal = glm::normalize(diagNormal);

    AddQuad(verts, indices,
        glm::vec3(pFrom.x, y0, pFrom.z), glm::vec3(pTo.x, y0, pTo.z),
        glm::vec3(pTo.x, y1, pTo.z), glm::vec3(pFrom.x, y1, pFrom.z),
        diagNormal, wallColor);

    // -------- Floor-патч клина (см. комментарий в шапке функции) --------
    // Треугольник pFrom-pTo-corner, y=0, лицом вверх — тот самый кусок
    // площади, что WallShapes::IsLocalPointSolid() считает открытым для
    // этой клетки. AddQuad() эмитит квад из двух треугольников; повторяя
    // corner дважды (3-я и 4-я вершины совпадают), второй треугольник
    // вырождается в нулевую площадь и ничего не рисует — простой способ
    // получить ровно один треугольник, не заводя отдельную функцию
    // AddTriangle() ради одного места использования.
    glm::vec3 corner(0.0f);
    switch (cut) {
        case CornerCut::SW: corner = glm::vec3(xf, 0.0f, zf);       break;
        case CornerCut::SE: corner = glm::vec3(x1, 0.0f, zf);       break;
        case CornerCut::NE: corner = glm::vec3(x1, 0.0f, z1);       break;
        case CornerCut::NW: corner = glm::vec3(xf, 0.0f, z1);       break;
        default: break;
    }
    AddQuad(verts, indices,
        pFrom, pTo, corner, corner,
        glm::vec3(0, 1, 0), floorColor);
}

// ---------------- Шаг 2: свободностоящие колонны (см. Columns.h) ----------------
//
// Простой цилиндр (AddCylinder уже используется для ручки факела/
// пьедестала кнопки, см. выше) — тут radiusBase==radiusTip (прямая
// колонна, не сужающаяся кверху). Клетка колонны к этому моменту УЖЕ
// пол в map[] (см. Columns::BuildColumns — мутирует грид ДО вызова
// build()), поэтому обычный floor-проход ниже сам построит пол под
// колонной — здесь эмитится только сам цилиндр.
static void AddColumnMesh(
    std::vector<Vertex>& verts, std::vector<GLuint>& indices,
    const glm::vec2& centerXZ, const glm::vec3& wallColor,
    float y0, float y1)
{
    const glm::vec3 base(centerXZ.x, y0, centerXZ.y);
    const glm::vec3 tip(centerXZ.x, y1, centerXZ.y);
    AddCylinder(verts, indices, base, tip,
                Columns::kColumnRadius, Columns::kColumnRadius,
                Columns::kColumnSegments, wallColor, 0.0f);
}

static void BuildFloorAndWallsGreedy(
    int mapW, int mapH, const std::vector<int>& map,
    const std::function<bool(int, int)>& isWall,
    const std::function<bool(int, int)>& isFloor,
    const std::vector<WallShapes::CornerCut>& cornerCuts,
    const std::vector<float>& chamferSizes,
    const std::vector<glm::vec3>& wallColors,
    const std::vector<glm::vec3>& floorColors,
    std::vector<std::vector<Vertex>>& chunkMainVerts,
    std::vector<std::vector<GLuint>>& chunkMainIndices,
    int chunksX)
{
    // Цвет стены/пола ПОКЛЕТОЧНО, уже готовый (см. build()/.h — вызывающий
    // код смешивает соседние зоны сам, здесь просто читаем результат) — на
    // клетку без данных (массив пуст/не хватает размера) используется
    // палитра 0 (исходный тёплый коричневый), т.е. поведение без
    // зонирования не меняется вообще.
    glm::vec3 fallbackWallColor, fallbackFloorColor;
    SceneGeometry::GetZonePalette(0, fallbackWallColor, fallbackFloorColor);
    auto wallColorAt = [&](int x, int z) {
        const size_t idx = (size_t)z * mapW + x;
        return idx < wallColors.size() ? wallColors[idx] : fallbackWallColor;
    };
    auto floorColorAt = [&](int x, int z) {
        const size_t idx = (size_t)z * mapW + x;
        return idx < floorColors.size() ? floorColors[idx] : fallbackFloorColor;
    };
    auto chunkIdx = [&](int cx, int cz) {
        return (cz / SceneGeometry::kChunkSize) * chunksX + (cx / SceneGeometry::kChunkSize);
    };

    // Item 6 (review): reserve() each per-chunk bucket up front using a
    // rough upper-bound estimate (cells-per-chunk * a few verts/indices
    // per quad face), instead of letting these vectors grow one push_back()
    // at a time while quads are appended below. This runs once at load
    // time (not per frame), so it's a startup-time win, not a runtime one
    // — but it's the same reasoning that made Quake's Hunk allocator
    // linear/non-copying: avoiding repeated reallocation+copy while
    // building level geometry. The estimate only needs to be a reasonable
    // upper bound, not exact — worst case a chunk still grows past it and
    // reallocates once or twice, same as before this change.
    {
        const size_t cellsPerChunk = (size_t)SceneGeometry::kChunkSize * (size_t)SceneGeometry::kChunkSize;
        const size_t estVertsPerChunk = cellsPerChunk * 4;   // ~4 verts/quad face, upper bound
        const size_t estIndicesPerChunk = cellsPerChunk * 6; // 6 indices/quad (2 tris), upper bound
        for (std::vector<Vertex>& v : chunkMainVerts)
            v.reserve(estVertsPerChunk);
        for (std::vector<GLuint>& idxVec : chunkMainIndices)
            idxVec.reserve(estIndicesPerChunk);
    }

    const float y0 = 0.0f;
    const float y1 = kWallHeight;

    // -------- Срезанные углы (WallShapes) — отдельная целиковая клетка --------
    // Идёт ДО greedy run-проходов ниже, т.к. те читают cornerCuts[...] чтобы
    // пропустить эти клетки (см. условия runов в 4 циклах ниже).
    for (int z = 0; z < mapH; z++) {
        for (int x = 0; x < mapW; x++) {
            const WallShapes::CornerCut cut = cornerCuts[(size_t)z * mapW + x];
            if (cut == WallShapes::CornerCut::None) continue;
            const int idx = chunkIdx(x, z);
            const float chamferSize = ((size_t)z * mapW + x) < chamferSizes.size()
                ? chamferSizes[(size_t)z * mapW + x]
                : WallShapes::kChamferSize;
            glm::vec3 cellWallColor = wallColorAt(x, z);
            glm::vec3 cellFloorColor = floorColorAt(x, z);
            AddChamferedWallCell(
                chunkMainVerts[idx], chunkMainIndices[idx],
                x, z, cut, chamferSize, isWall, cellWallColor, cellFloorColor, y0, y1);
        }
    }

    // -------- Floor: merge runs of cell==2 along x, per row z --------
    for (int z = 0; z < mapH; z++) {
        int x = 0;
        while (x < mapW) {
            if (map[z * mapW + x] != 2) { x++; continue; }

            const int runStart = x;
            const int curChunkX = x / SceneGeometry::kChunkSize;
            const glm::vec3 curFloorColor = floorColorAt(x, z);
            while (x < mapW &&
                   map[z * mapW + x] == 2 &&
                   floorColorAt(x, z) == curFloorColor &&
                   (x / SceneGeometry::kChunkSize) == curChunkX) {
                x++;
            }
            const int runEnd = x; // exclusive

            const int idx = chunkIdx(runStart, z);
            AddQuad(
                chunkMainVerts[idx], chunkMainIndices[idx],
                glm::vec3((float)runStart, 0.0f, (float)z),
                glm::vec3((float)runEnd,   0.0f, (float)z),
                glm::vec3((float)runEnd,   0.0f, (float)z + 1.0f),
                glm::vec3((float)runStart, 0.0f, (float)z + 1.0f),
                glm::vec3(0, 1, 0),
                curFloorColor
            );
        }
    }

    // -------- South-facing walls (normal 0,0,-1): merge along x, per row z --------
    for (int z = 0; z < mapH; z++) {
        int x = 0;
        while (x < mapW) {
            // Срезанные клетки уже полностью построены в отдельном проходе
            // выше (AddChamferedWallCell) — здесь они просто "разрывают"
            // run, не попадая ни в него, ни получая собственный квад.
            if (!(isWall(x, z) && !isWall(x, z - 1)) ||
                cornerCuts[(size_t)z * mapW + x] != WallShapes::CornerCut::None) {
                x++; continue;
            }

            const int runStart = x;
            const int curChunkX = x / SceneGeometry::kChunkSize;
            const glm::vec3 curWallColor = wallColorAt(x, z);
            while (x < mapW &&
                   isWall(x, z) && !isWall(x, z - 1) &&
                   cornerCuts[(size_t)z * mapW + x] == WallShapes::CornerCut::None &&
                   wallColorAt(x, z) == curWallColor &&
                   (x / SceneGeometry::kChunkSize) == curChunkX) {
                x++;
            }
            const int runEnd = x;

            const int idx = chunkIdx(runStart, z);
            AddQuad(
                chunkMainVerts[idx], chunkMainIndices[idx],
                glm::vec3((float)runStart, y0, (float)z),
                glm::vec3((float)runEnd,   y0, (float)z),
                glm::vec3((float)runEnd,   y1, (float)z),
                glm::vec3((float)runStart, y1, (float)z),
                glm::vec3(0, 0, -1),
                curWallColor
            );
        }
    }

    // -------- North-facing walls (normal 0,0,1): merge along x, per row z --------
    for (int z = 0; z < mapH; z++) {
        int x = 0;
        while (x < mapW) {
            if (!(isWall(x, z) && !isWall(x, z + 1)) ||
                cornerCuts[(size_t)z * mapW + x] != WallShapes::CornerCut::None) {
                x++; continue;
            }

            const int runStart = x;
            const int curChunkX = x / SceneGeometry::kChunkSize;
            const glm::vec3 curWallColor = wallColorAt(x, z);
            while (x < mapW &&
                   isWall(x, z) && !isWall(x, z + 1) &&
                   cornerCuts[(size_t)z * mapW + x] == WallShapes::CornerCut::None &&
                   wallColorAt(x, z) == curWallColor &&
                   (x / SceneGeometry::kChunkSize) == curChunkX) {
                x++;
            }
            const int runEnd = x;
            const float z1 = (float)z + 1.0f;

            const int idx = chunkIdx(runStart, z);
            AddQuad(
                chunkMainVerts[idx], chunkMainIndices[idx],
                glm::vec3((float)runStart, y0, z1),
                glm::vec3((float)runEnd,   y0, z1),
                glm::vec3((float)runEnd,   y1, z1),
                glm::vec3((float)runStart, y1, z1),
                glm::vec3(0, 0, 1),
                curWallColor
            );
        }
    }

    // -------- West-facing walls (normal -1,0,0): merge along z, per column x --------
    for (int x = 0; x < mapW; x++) {
        int z = 0;
        while (z < mapH) {
            if (!(isWall(x, z) && !isWall(x - 1, z)) ||
                cornerCuts[(size_t)z * mapW + x] != WallShapes::CornerCut::None) {
                z++; continue;
            }

            const int runStart = z;
            const int curChunkZ = z / SceneGeometry::kChunkSize;
            const glm::vec3 curWallColor = wallColorAt(x, z);
            while (z < mapH &&
                   isWall(x, z) && !isWall(x - 1, z) &&
                   cornerCuts[(size_t)z * mapW + x] == WallShapes::CornerCut::None &&
                   wallColorAt(x, z) == curWallColor &&
                   (z / SceneGeometry::kChunkSize) == curChunkZ) {
                z++;
            }
            const int runEnd = z;
            const float xf = (float)x;

            const int idx = chunkIdx(x, runStart);
            AddQuad(
                chunkMainVerts[idx], chunkMainIndices[idx],
                glm::vec3(xf, y0, (float)runStart),
                glm::vec3(xf, y0, (float)runEnd),
                glm::vec3(xf, y1, (float)runEnd),
                glm::vec3(xf, y1, (float)runStart),
                glm::vec3(-1, 0, 0),
                curWallColor
            );
        }
    }

    // -------- East-facing walls (normal 1,0,0): merge along z, per column x --------
    for (int x = 0; x < mapW; x++) {
        int z = 0;
        while (z < mapH) {
            if (!(isWall(x, z) && !isWall(x + 1, z)) ||
                cornerCuts[(size_t)z * mapW + x] != WallShapes::CornerCut::None) {
                z++; continue;
            }

            const int runStart = z;
            const int curChunkZ = z / SceneGeometry::kChunkSize;
            const glm::vec3 curWallColor = wallColorAt(x, z);
            while (z < mapH &&
                   isWall(x, z) && !isWall(x + 1, z) &&
                   cornerCuts[(size_t)z * mapW + x] == WallShapes::CornerCut::None &&
                   wallColorAt(x, z) == curWallColor &&
                   (z / SceneGeometry::kChunkSize) == curChunkZ) {
                z++;
            }
            const int runEnd = z;
            const float x1 = (float)x + 1.0f;

            const int idx = chunkIdx(x, runStart);
            AddQuad(
                chunkMainVerts[idx], chunkMainIndices[idx],
                glm::vec3(x1, y0, (float)runStart),
                glm::vec3(x1, y0, (float)runEnd),
                glm::vec3(x1, y1, (float)runEnd),
                glm::vec3(x1, y1, (float)runStart),
                glm::vec3(1, 0, 0),
                curWallColor
            );
        }
    }
}


void SceneGeometry::build(int mapW, int mapH, const std::vector<int>& map,
                          const glm::vec3& winButtonPos,
                          const std::vector<glm::vec3>& torchWallBase,
                          const std::vector<glm::vec3>& torchNormal,
                          const std::vector<glm::vec3>& torchFlamePos,
                          const std::vector<WallShapes::CornerCut>& cornerCuts,
                          const std::vector<glm::vec2>& columnCentersXZ,
                          const std::vector<float>& chamferSizes,
                          const std::vector<glm::vec3>& wallColors,
                          const std::vector<glm::vec3>& floorColors,
                          const std::vector<glm::vec3>& diaryPositions)
{
    auto isWall = [&](int x, int z) {
        if (x < 0 || x >= mapW || z < 0 || z >= mapH) return true;
        return map[z * mapW + x] == 1;
    };
    auto isFloor = [&](int x, int z) {
        if (x < 0 || x >= mapW || z < 0 || z >= mapH) return false;
        return map[z * mapW + x] == 2;
    };

    // ==================================================
    // Chunked geometry (see DungeonScene.h: m_chunks, kChunkSize)
    // ==================================================
    // Geometry is bucketed per-chunk while being generated, instead of
    // going straight into one flat vector, so that render() can later
    // skip whole chunks that are outside the camera frustum / render
    // distance. The final GPU buffers still contain ALL chunks laid out
    // contiguously (one static upload, same as before) — only which
    // *ranges* get drawn each frame changes.
    const int chunksX = (mapW + kChunkSize - 1) / kChunkSize;
    const int chunksZ = (mapH + kChunkSize - 1) / kChunkSize;
    const int numChunks = chunksX * chunksZ;

    // Cached for buildChunkPVS() and render()'s PVS lookup (turning the
    // camera's current cell into a chunk index) — see DungeonScene.h.
    m_chunksX = chunksX;

    auto chunkIndexForCell = [&](int cx, int cz) {
        cx = std::clamp(cx, 0, mapW - 1);
        cz = std::clamp(cz, 0, mapH - 1);
        return (cz / kChunkSize) * chunksX + (cx / kChunkSize);
    };

    std::vector<std::vector<Vertex>> chunkMainVerts(numChunks);
    std::vector<std::vector<GLuint>> chunkMainIndices(numChunks);
    std::vector<std::vector<Vertex>> chunkParticleVerts(numChunks);

    // ==================================================
    // [comment corrupted in source file - original text lost/unrecoverable]
    // ==================================================

    BuildFloorAndWallsGreedy(mapW, mapH, map, isWall, isFloor, cornerCuts, chamferSizes, wallColors, floorColors, chunkMainVerts, chunkMainIndices, chunksX);

    // ==================================================
    // [comment corrupted in source file - original text lost/unrecoverable]
    // ==================================================

    for (size_t i = 0;
         i < torchWallBase.size();
         ++i)
    {
        // Torch mesh (handle + flame) and its flicker particles belong
        // to the chunk that contains the torch's wall position.
        //
        // ВАЖНО: torchWallBase[i] лежит РОВНО на границе клетки стены и
        // соседней клетки пола (см. tryAddTorch()), поэтому floor(x)/
        // floor(z) от неё напрямую даёт правильную клетку стены только
        // для половины направлений (см. тот же баг, что был исправлен в
        // buildTorchCellLookup() выше по файлу). Если взять "не ту"
        // клетку, факел может попасть в чанк, отличный от чанка стены,
        // на которой он висит — и тогда при фрустум-куллинге по чанкам
        // факел иногда будет пропадать из вида, хотя камера смотрит
        // прямо на его стену. Откатываем обратно на normal*0.5, чтобы
        // получить исходную клетку стены (x, z) однозначно.
        const glm::vec3& torchN = torchNormal[i];
        const int torchChunk = chunkIndexForCell(
            (int)std::floor(torchWallBase[i].x - torchN.x * 0.5f),
            (int)std::floor(torchWallBase[i].z - torchN.z * 0.5f)
        );
        std::vector<Vertex>& verts = chunkMainVerts[torchChunk];
        std::vector<GLuint>& indices = chunkMainIndices[torchChunk];
        std::vector<Vertex>& particleVerts = chunkParticleVerts[torchChunk];

        AddTorchMesh(
            verts,
            indices,
            torchWallBase[i],
            torchNormal[i],
            (int)i
        );

        // ------------------------------------------------
        // [comment corrupted in source file - original text lost/unrecoverable]
        //
        // [comment corrupted in source file - original text lost/unrecoverable]
        // [comment corrupted in source file - original text lost/unrecoverable]
        // ------------------------------------------------

        const glm::vec3 origin =
            torchFlamePos[i]
            + glm::vec3(
                0.0f,
                0.025f,
                0.0f
            );

        const int torchIndex =
            static_cast<int>(i);

        // [comment corrupted in source file - original text lost/unrecoverable]
        // [comment corrupted in source file - original text lost/unrecoverable]
        // [comment corrupted in source file - original text lost/unrecoverable]
        const float PARTICLE_ID_SCALE = 16384.0f;

        // ==================================================
        // PARTICLE SLOT 0
        // ==================================================

        const float particleMatId0 =
            3.0f
            +
            (
                static_cast<float>(
                    torchIndex * 10 + 0
                )
                /
                PARTICLE_ID_SCALE
            );

        particleVerts.push_back(
            {
                origin,

                glm::vec3(
                    -0.030f,
                    0.40f,
                    0.010f
                ),

                glm::vec3(
                    1.0f,
                    0.48f,
                    0.08f
                ),

                particleMatId0
            }
        );

        // ==================================================
        // PARTICLE SLOT 1
        // ==================================================

        const float particleMatId1 =
            3.0f
            +
            (
                static_cast<float>(
                    torchIndex * 10 + 1
                )
                /
                PARTICLE_ID_SCALE
            );

        particleVerts.push_back(
            {
                origin,

                glm::vec3(
                    0.020f,
                    0.48f,
                    -0.025f
                ),

                glm::vec3(
                    1.0f,
                    0.58f,
                    0.10f
                ),

                particleMatId1
            }
        );

        // ==================================================
        // PARTICLE SLOT 2
        // ==================================================

        const float particleMatId2 =
            3.0f
            +
            (
                static_cast<float>(
                    torchIndex * 10 + 2
                )
                /
                PARTICLE_ID_SCALE
            );

        particleVerts.push_back(
            {
                origin,

                glm::vec3(
                    -0.045f,
                    0.43f,
                    -0.020f
                ),

                glm::vec3(
                    1.0f,
                    0.52f,
                    0.07f
                ),

                particleMatId2
            }
        );
    }

    // ==================================================
    // [comment corrupted in source file - original text lost/unrecoverable]
    // ==================================================

    {
        const int winChunk = chunkIndexForCell(
            (int)std::floor(winButtonPos.x),
            (int)std::floor(winButtonPos.z)
        );
        AddWinButtonMesh(chunkMainVerts[winChunk], chunkMainIndices[winChunk], winButtonPos);
    }

    // ==================================================
    // Дневники (см. Diaries.h) — один пропс на карман, тот же принцип
    // отнесения к чанку, что и у кнопки победы выше: по floor() мировых
    // координат находим клетку, а через неё — чанк, чтобы фрустум-куллинг
    // по чанкам (см. render()) не терял дневник, если камера смотрит
    // прямо на него, но чанк формально "не тот".
    // ==================================================
    for (size_t i = 0; i < diaryPositions.size(); ++i) {
        const glm::vec3& diaryPos = diaryPositions[i];
        const int diaryChunk = chunkIndexForCell(
            (int)std::floor(diaryPos.x),
            (int)std::floor(diaryPos.z)
        );
        AddDiaryMesh(chunkMainVerts[diaryChunk], chunkMainIndices[diaryChunk], diaryPos);

        // Пара тихих "мотыльков"-частиц над книгой — так игрок замечает
        // дневник издалека и понимает, что именно ЭТО нужно подобрать/
        // прочитать, а не просто декорация. Переиспользует систему
        // факельных частиц (см. torchWallBase-цикл выше, matId>=3.0,
        // анимация в scene.vert) — тот же spawn/lifetime/drift, но:
        //  - ЦВЕТ приглушённый тёплый бледно-золотой, а не огненно-
        //    оранжевый — чтобы не путать с настоящим факельным светом;
        //  - id (torchWallBase.size() + i) продолжается ПОСЛЕ диапазона
        //    факелов, чтобы не делить фазу мерцания с факелом того же
        //    порядкового номера (см. hash(torchId,...) в scene.vert —
        //    matId лишь seed для псевдослучайности, коллизия id не
        //    ломает рендер, но даёт заметно одинаковую анимацию).
        std::vector<Vertex>& diaryParticleVerts = chunkParticleVerts[diaryChunk];
        const glm::vec3 particleOrigin = diaryPos + glm::vec3(0.0f, 0.16f, 0.0f);
        const int diaryParticleId = (int)torchWallBase.size() + (int)i;
        const glm::vec3 diaryGlowColor(0.95f, 0.88f, 0.62f);
        const float PARTICLE_ID_SCALE = 16384.0f;

        const float slot0MatId = 3.0f + (float)(diaryParticleId * 10 + 0) / PARTICLE_ID_SCALE;
        diaryParticleVerts.push_back({
            particleOrigin, glm::vec3(-0.020f, 0.30f, 0.015f), diaryGlowColor, slot0MatId
        });

        const float slot1MatId = 3.0f + (float)(diaryParticleId * 10 + 1) / PARTICLE_ID_SCALE;
        diaryParticleVerts.push_back({
            particleOrigin, glm::vec3(0.018f, 0.34f, -0.020f), diaryGlowColor, slot1MatId
        });
    }

    // ==================================================
    // Шаг 2: свободностоящие колонны (см. Columns.h)
    // ==================================================
    // Позиции уже мировые XZ-центры клеток (Columns::BuildColumns
    // отдаёт (cellX+0.5, cellZ+0.5)) — та же клетка в map[] к этому
    // моменту уже пол (мутация случилась ДО generateMap() передал map
    // сюда), так что floor-квад под колонной уже построен обычным
    // floor-проходом в BuildFloorAndWallsGreedy() выше — здесь только
    // сам цилиндр.
    for (const glm::vec2& col : columnCentersXZ) {
        const int colCellX = (int)std::floor(col.x);
        const int colCellZ = (int)std::floor(col.y);
        const int colChunk = chunkIndexForCell(colCellX, colCellZ);
        const size_t colIdx = (size_t)colCellZ * mapW + colCellX;
        glm::vec3 fallbackWallColor, fallbackFloorColorUnused;
        GetZonePalette(0, fallbackWallColor, fallbackFloorColorUnused);
        const glm::vec3 colWallColor = colIdx < wallColors.size() ? wallColors[colIdx] : fallbackWallColor;
        AddColumnMesh(chunkMainVerts[colChunk], chunkMainIndices[colChunk], col, colWallColor, 0.0f, kWallHeight);
    }

    // ==================================================
    // Flatten per-chunk buckets into contiguous buffers + build
    // per-chunk metadata (draw ranges + AABB) used by render() for
    // frustum/distance culling.
    // ==================================================

    std::vector<Vertex> verts;
    std::vector<GLuint> indices;
    std::vector<Vertex> particleVerts;

    m_chunks.clear();
    m_chunks.resize(numChunks);

    for (int cz = 0; cz < chunksZ; ++cz)
    {
        for (int cxi = 0; cxi < chunksX; ++cxi)
        {
            const int idx = cz * chunksX + cxi;
            GeoChunk& chunk = m_chunks[idx];

            const std::vector<Vertex>& mv = chunkMainVerts[idx];
            const std::vector<GLuint>& mi = chunkMainIndices[idx];
            const std::vector<Vertex>& pv = chunkParticleVerts[idx];

            // Indices in mi are local to mv (start at 0). When we append
            // mv's vertices onto the end of the global 'verts' array they
            // land at vertexBase..vertexBase+mv.size(), so every local
            // index needs vertexBase added to stay correct.
            const GLuint vertexBase = (GLuint)verts.size();
            verts.insert(verts.end(), mv.begin(), mv.end());

            chunk.mainIndexFirst = (GLint)indices.size();
            chunk.mainIndexCount = (GLsizei)mi.size();
            indices.reserve(indices.size() + mi.size());
            for (GLuint localIndex : mi)
                indices.push_back(localIndex + vertexBase);

            chunk.particleFirst = (GLint)particleVerts.size();
            chunk.particleCount = (GLsizei)pv.size();
            particleVerts.insert(particleVerts.end(), pv.begin(), pv.end());

            // Cell-grid-based AABB (with a small margin for torch/flame
            // meshes that poke slightly past a cell's edge) — cheap and
            // always conservative (never smaller than the real geometry),
            // which is all that's required for correct frustum culling.
            const float margin = 0.5f;
            const int cellX0 = cxi * kChunkSize;
            const int cellZ0 = cz * kChunkSize;
            const int cellX1 = std::min(mapW, cellX0 + kChunkSize);
            const int cellZ1 = std::min(mapH, cellZ0 + kChunkSize);

            chunk.aabbMin = glm::vec3((float)cellX0 - margin, -margin, (float)cellZ0 - margin);
            chunk.aabbMax = glm::vec3((float)cellX1 + margin, kWallHeight + margin, (float)cellZ1 + margin);
        }
    }

    // ==================================================
    // [comment corrupted in source file - original text lost/unrecoverable]
    // ==================================================

    m_vertexCount =
        (int)verts.size();

    // Item 1: index buffer element count (used by render() for the
    // glMultiDrawElements calls, and by the per-chunk mainIndexFirst/
    // mainIndexCount offsets computed above).
    m_indexCount =
        (int)indices.size();

    glGenVertexArrays(
        1,
        &m_vao
    );

    glGenBuffers(
        1,
        &m_vbo
    );

    glGenBuffers(
        1,
        &m_ebo
    );

    glBindVertexArray(
        m_vao
    );

    glBindBuffer(
        GL_ARRAY_BUFFER,
        m_vbo
    );

    glBufferData(
        GL_ARRAY_BUFFER,
        verts.size()
        * sizeof(Vertex),
        verts.data(),
        GL_STATIC_DRAW
    );

    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            pos
        )
    );

    glEnableVertexAttribArray(1);

    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            normal
        )
    );

    glEnableVertexAttribArray(2);

    glVertexAttribPointer(
        2,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            color
        )
    );

    glEnableVertexAttribArray(3);

    glVertexAttribPointer(
        3,
        1,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            matId
        )
    );

    // The EBO binding is stored as part of the VAO's state, so it must be
    // bound while m_vao is still bound (before the glBindVertexArray(0)
    // below) — same pattern as the VBO/attribute setup above.
    glBindBuffer(
        GL_ELEMENT_ARRAY_BUFFER,
        m_ebo
    );

    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        indices.size()
        * sizeof(GLuint),
        indices.data(),
        GL_STATIC_DRAW
    );

    glBindVertexArray(0);

    // ==================================================
    // [comment corrupted in source file - original text lost/unrecoverable]
    // ==================================================

    m_particleVertexCount =
        (int)particleVerts.size();

    glGenVertexArrays(
        1,
        &m_particleVao
    );

    glGenBuffers(
        1,
        &m_particleVbo
    );

    glBindVertexArray(
        m_particleVao
    );

    glBindBuffer(
        GL_ARRAY_BUFFER,
        m_particleVbo
    );

    glBufferData(
        GL_ARRAY_BUFFER,
        particleVerts.size()
        * sizeof(Vertex),
        particleVerts.data(),
        GL_STATIC_DRAW
    );

    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            pos
        )
    );

    glEnableVertexAttribArray(1);

    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            normal
        )
    );

    glEnableVertexAttribArray(2);

    glVertexAttribPointer(
        2,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            color
        )
    );

    glEnableVertexAttribArray(3);

    glVertexAttribPointer(
        3,
        1,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(
            Vertex,
            matId
        )
    );

    glBindVertexArray(0);
}

// ---------------- Item 3 (review): precomputed chunk PVS ----------------
//
// See the m_chunkPvsMask comment in DungeonScene.h for the full rationale.
// Short version: for every chunk, flood-fill outward through floor cells
// only (walls block the flood) starting from that chunk's own footprint,
// and record every chunk touched as a bit in a 64-bit mask. Run once,
// right after buildGeometry() builds m_chunks/m_map, before the first
// frame — this is the "offline vis pass" equivalent for a procedurally
// generated maze.

void SceneGeometry::buildPVS(int mapW, int mapH, const std::function<bool(int, int)>& isWall)
{
    const size_t numChunks = m_chunks.size();
    m_chunkPvsMask.assign(numChunks, 0ull);

    // The mask is a single uint64_t (bit-per-chunk), so this only works
    // for up to 64 chunks. That's exactly what the current 128x128 /
    // kChunkSize(16) maze produces (8x8 = 64), but if that ever changes,
    // fail safe rather than silently truncate: disable the prefilter and
    // let render() fall back to frustum+distance culling alone, as if
    // this feature didn't exist.
    if (numChunks == 0 || numChunks > 64 || m_chunksX <= 0 || mapW <= 0 || mapH <= 0)
    {
        m_chunkPvsEnabled = false;
        return;
    }

    m_chunkPvsEnabled = true;

    std::vector<unsigned char> visitedCell((size_t)mapW * (size_t)mapH, 0);
    std::vector<int> stack;
    stack.reserve(visitedCell.size());

    const int dx[4] = { 1, -1, 0, 0 };
    const int dz[4] = { 0, 0, 1, -1 };

    for (size_t srcChunk = 0; srcChunk < numChunks; ++srcChunk)
    {
        std::fill(visitedCell.begin(), visitedCell.end(), (unsigned char)0);
        stack.clear();

        const int cxi = (int)srcChunk % m_chunksX;
        const int cz = (int)srcChunk / m_chunksX;
        const int cellX0 = cxi * kChunkSize;
        const int cellZ0 = cz * kChunkSize;
        const int cellX1 = std::min(mapW, cellX0 + kChunkSize);
        const int cellZ1 = std::min(mapH, cellZ0 + kChunkSize);

        uint64_t mask = (1ull << srcChunk); // a chunk is always potentially visible from itself

        // Seed the flood with every non-wall cell inside the source
        // chunk's own footprint (a corridor cell right at the chunk's
        // edge still needs to start propagating outward).
        for (int z = cellZ0; z < cellZ1; ++z)
        {
            for (int x = cellX0; x < cellX1; ++x)
            {
                if (isWall(x, z)) continue;
                const int cell = z * mapW + x;
                if (!visitedCell[(size_t)cell])
                {
                    visitedCell[(size_t)cell] = 1;
                    stack.push_back(cell);
                }
            }
        }

        while (!stack.empty())
        {
            const int cell = stack.back();
            stack.pop_back();

            const int x = cell % mapW;
            const int z = cell / mapW;

            const int touchedChunk = (z / kChunkSize) * m_chunksX + (x / kChunkSize);
            mask |= (1ull << touchedChunk);

            for (int d = 0; d < 4; ++d)
            {
                const int nx = x + dx[d];
                const int nz = z + dz[d];
                if (nx < 0 || nz < 0 || nx >= mapW || nz >= mapH) continue;
                if (isWall(nx, nz)) continue;

                const int ncell = nz * mapW + nx;
                if (visitedCell[(size_t)ncell]) continue;

                visitedCell[(size_t)ncell] = 1;
                stack.push_back(ncell);
            }
        }

        m_chunkPvsMask[srcChunk] = mask;
    }
}

void SceneGeometry::destroy()
{
    if (m_particleVbo) {
        glDeleteBuffers(1, &m_particleVbo);
        m_particleVbo = 0;
    }
    if (m_particleVao) {
        glDeleteVertexArrays(1, &m_particleVao);
        m_particleVao = 0;
    }
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_ebo) {
        glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    m_particleVertexCount = 0;
    m_vertexCount = 0;
    m_indexCount = 0;
    m_chunks.clear();
    m_chunksX = 0;
    m_chunkPvsMask.clear();
    m_chunkPvsEnabled = false;
}
