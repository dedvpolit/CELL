#include "AsciiEffect.h"
#include "ShaderLoader.h"
#include "ShaderProgram.h"
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

// ---------------- Шейдеры ----------------
// Исходники GLSL вынесены в assets/shaders/ascii_post.{vert,frag} и
// читаются с диска при инициализации (см. init()/ShaderLoader.h) —
// раньше они были встроены здесь как raw-string литералы.

// ---------------- Вспомогательные ----------------

void AsciiEffect::createFBO(int w, int h) {
    m_fboW = w; m_fboH = h;

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_sceneTex);
    glBindTexture(GL_TEXTURE_2D, m_sceneTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    // MIPMAP_LINEAR (not plain LINEAR): the post-process pass now reads
    // this texture via textureLod() at a mip level matching cellSize
    // (see ascii_post.frag) instead of hand-averaging a 3x3 neighborhood
    // per pixel — that only works if this texture actually has a mip
    // chain (built once per frame via glGenerateMipmap(), see end() below).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneTex, 0);

    // БЫЛ второй render target здесь (маска "это факел" для
    // TorchAsciiEffect, GL_R8 полного разрешения экрана) — убран вместе
    // с самим эффектом (см. историю правок, жалоба "игра жрёт слишком
    // много ОЗУ"): при 1920x1080 эта маска сама по себе занимала ~2 МБ
    // VRAM, а больше её никто не читал. Обратно к одному render target'у
    // (GL_COLOR_ATTACHMENT0) — glDrawBuffers() с одним элементом не
    // нужен вообще, он и есть дефолт для FBO с одним attachment'ом.

    // Настоящая depth-ТЕКСТУРА вместо renderbuffer оставлена как есть
    // (исторически была нужна TorchAsciiEffect'у, теперь просто рабочий
    // depth-attachment для 3D-прохода — байтов на GPU это не экономит и
    // не тратит по сравнению с renderbuffer, менять не было смысла).
    glGenTextures(1, &m_depthTex);
    glBindTexture(GL_TEXTURE_2D, m_depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "AsciiEffect: FBO incomplete!\n");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void AsciiEffect::destroyFBO() {
    if (m_sceneTex)     glDeleteTextures(1, &m_sceneTex);
    if (m_depthTex)     glDeleteTextures(1, &m_depthTex);
    if (m_fbo)          glDeleteFramebuffers(1, &m_fbo);
    m_sceneTex = m_depthTex = m_fbo = 0;
}

// Процедурная генерация "шрифта": на каждом уровне яркости рисуем
// растущий закруглённый блок в центре ячейки. Уровень 0 = пусто (чёрный),
// уровень RAMP_LENGTH-1 = почти сплошной квадрат. Никакие внешние
// картинки/шрифты не нужны.
// 8x8 битмап-шрифт: набор символов разной "плотности чернил" —
// ASCII, цифры, буквы латиницы + пара плотных "иероглифоподобных"
// глифов для самых ярких участков. Порядок в массиве не важен —
// сортируем по плотности автоматически.
struct GlyphDef { unsigned char rows[8]; };

static const GlyphDef s_glyphs[] = {
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}}, // ' '
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18}}, // '.'
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x0C}}, // ','
    {{0x18,0x18,0x10,0x00,0x00,0x00,0x00,0x00}}, // '`'
    {{0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}}, // ':'
    {{0x00,0x18,0x18,0x00,0x18,0x18,0x0C,0x00}}, // ';'
    {{0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}}, // '-'
    {{0x00,0x66,0x66,0x00,0x00,0x00,0x00,0x00}}, // '"'
    {{0x18,0x24,0x42,0x00,0x00,0x00,0x00,0x00}}, // '^'
    {{0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}}, // '='
    {{0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}}, // '+'
    {{0x00,0x06,0x18,0x60,0x60,0x18,0x06,0x00}}, // '<'
    {{0x00,0x60,0x18,0x06,0x06,0x18,0x60,0x00}}, // '>'
    {{0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x00}}, // '/'
    {{0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x00}}, // '\'
    {{0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00}}, // '*'
    {{0x08,0x18,0x28,0x08,0x08,0x08,0x3E,0x00}}, // '1'
    {{0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}}, // '|'
    {{0x00,0x42,0x42,0x24,0x24,0x18,0x00,0x00}}, // 'v'
    {{0x00,0x3C,0x66,0x60,0x66,0x3C,0x00,0x00}}, // 'c'
    {{0x00,0x3C,0x66,0x66,0x66,0x3C,0x00,0x00}}, // 'o'
    {{0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00,0x00}}, // 's'
    {{0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00,0x00}}, // 'z'
    {{0x00,0x66,0x3C,0x18,0x3C,0x66,0x00,0x00}}, // 'x'
    {{0x00,0x6C,0x76,0x66,0x66,0x66,0x00,0x00}}, // 'n'
    {{0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00,0x00}}, // 'a'
    {{0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00,0x00}}, // 'e'
    {{0x00,0x6C,0x76,0x60,0x60,0x60,0x00,0x00}}, // 'r'
    {{0x00,0x24,0x7E,0x24,0x24,0x7E,0x24,0x00}}, // '#'
    {{0x00,0x62,0x64,0x08,0x10,0x26,0x46,0x00}}, // '%'
    {{0x00,0x3C,0x66,0x3C,0x38,0x67,0x3E,0x00}}, // '&'
    {{0x00,0x3C,0x66,0x3C,0x66,0x66,0x3C,0x00}}, // '8'
    {{0x00,0x66,0x3C,0x18,0x3C,0x66,0xC3,0x00}}, // 'X'
    {{0x00,0xC3,0xC3,0xDB,0xFF,0x66,0x66,0x00}}, // 'W'
    {{0x00,0xC3,0xE7,0xFF,0xDB,0xC3,0xC3,0x00}}, // 'M'
    {{0x00,0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0x00}}, // 'N'
    {{0x00,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}}, // 'H'
    {{0x00,0x66,0x6C,0x78,0x78,0x6C,0x66,0x00}}, // 'K'
    {{0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x62,0x3C}}, // '@'
    {{0xFF,0x99,0xFF,0x99,0xFF,0x99,0xFF,0x99}}, // плотный "иероглиф"-паттерн
    {{0xFF,0xE7,0xFF,0xE7,0xFF,0xE7,0xFF,0xE7}}, // ещё плотнее
    {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}}, // сплошной блок
    {{0x00,0x18,0x3C,0x3C,0x18,0x00,0x00,0x00}}, // ромб
    {{0x00,0x66,0x00,0x18,0x00,0x66,0x00,0x00}}, // редкие точки
    {{0x3C,0x42,0x99,0xA5,0xA5,0x99,0x42,0x3C}}, // "иероглиф"-паттерн 1
    {{0x66,0xFF,0xDB,0xFF,0xFF,0xDB,0xFF,0x66}}, // "иероглиф"-паттерн 2
    {{0x00,0x6E,0x11,0x11,0x11,0x11,0x6E,0x00}}, // 'D'-подобный
    {{0x7E,0x81,0xA5,0x81,0xA5,0x99,0x81,0x7E}}, // сложный узор (лицо/маска)
    {{0xF0,0x0F,0xF0,0x0F,0xF0,0x0F,0xF0,0x0F}}, // диагональные полосы
    {{0x0F,0xF0,0x0F,0xF0,0x0F,0xF0,0x0F,0xF0}}, // диагональные полосы (инверс)

        // --- Очень разреженные (для самых тёмных зон) ---
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00}}, // одна точка
    {{0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00}}, // точка выше
    {{0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00}}, // точка сбоку
    {{0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00}}, // точка в углу

    // --- "Кириллица/иероглифы"-стиль (штрихи под азиатские символы) ---
    {{0x00,0x7E,0x18,0x18,0x18,0x18,0x7E,0x00}}, // 'Ш'-подобный
    {{0x00,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}}, // 'Д'-подобный
    {{0x00,0x18,0x3C,0x66,0x66,0x3C,0x18,0x00}}, // ромб-иероглиф
    {{0x18,0x18,0x7E,0x18,0x18,0x00,0x7E,0x00}}, // '木'-подобный (дерево)
    {{0x24,0x24,0xFF,0x24,0xFF,0x24,0x24,0x00}}, // '井'-подобный (решётка-колодец)
    {{0x00,0x3C,0x24,0x24,0x24,0x24,0x3C,0x00}}, // 'П'-рамка
    {{0x66,0x66,0x24,0x18,0x24,0x66,0x66,0x00}}, // 'Ж'-подобный
    {{0x7E,0x40,0x40,0x7C,0x40,0x40,0x7E,0x00}}, // 'Е'-подобный жирный
    {{0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x18}}, // 'Q'/'Ω'-подобный

    // --- Очень плотные (для самых ярких зон, ближе к камере) ---
    {{0xFF,0x81,0xBD,0xA5,0xA5,0xBD,0x81,0xFF}}, // сложный плотный узор
    {{0xEF,0xDB,0xBD,0x7E,0x7E,0xBD,0xDB,0xEF}}, // почти сплошной с ромбом
    {{0xFF,0xFF,0xE7,0xC3,0xC3,0xE7,0xFF,0xFF}}, // почти блок с прорезью
    {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE}}, // максимально плотный

    // --- Уникальная кириллица (не дублирующая по форме латиницу) ---
    {{0x00,0x66,0x66,0x66,0x3E,0x06,0x06,0x00}}, // 'Ц' (упрощённо)
    {{0x66,0x66,0x24,0x18,0x24,0x66,0x66,0x00}}, // 'Ж'
    {{0x3C,0x66,0x0C,0x18,0x0C,0x66,0x3C,0x00}}, // 'З'
    {{0x66,0x66,0x6E,0x76,0x66,0x66,0x66,0x00}}, // 'И'
    {{0x00,0x66,0x66,0x66,0x66,0x66,0x3E,0x06}}, // 'Щ' упрощённо
    {{0x7E,0x66,0x66,0x66,0x66,0x66,0x66,0x00}}, // 'П'
    {{0x18,0x3C,0x66,0x7E,0x66,0x66,0x66,0x00}}, // 'Ф'-приближение
    {{0x66,0x66,0x66,0x3C,0x18,0x3C,0x66,0x00}}, // 'Ю'-приближение
    {{0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x18}}, // 'Я'-приближение




};
static const int s_glyphCount = sizeof(s_glyphs) / sizeof(s_glyphs[0]);

// ---- Отдельный набор символов для мини-карты ----
// В отличие от s_glyphs[] выше, этот атлас НЕ сортируется по
// "плотности чернил" — каждому смыслу (стена/пол/факел/направление
// игрока/рамка круга) соответствует фиксированный, заранее известный
// индекс, см. использование индексов в шейдере (glyphIdx).
static const GlyphDef s_minimapGlyphs[] = {
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}}, // 0:  пусто
    {{0x24,0x24,0x7E,0x24,0x7E,0x24,0x24,0x00}}, // 1:  стена      '#'
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18}}, // 2:  пол        '.'
    {{0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00}}, // 3:  факел      '*'
    {{0x18,0x3C,0x7E,0x18,0x18,0x18,0x18,0x00}}, // 4:  игрок С    '^'
    {{0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x00}}, // 5:  игрок СВ   '/'
    {{0x08,0x0C,0xFE,0x0C,0x08,0x00,0x00,0x00}}, // 6:  игрок В    '>'
    {{0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x00}}, // 7:  игрок ЮВ   '\'
    {{0x18,0x18,0x18,0x18,0x7E,0x3C,0x18,0x00}}, // 8:  игрок Ю    'v'
    {{0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x00}}, // 9:  игрок ЮЗ   '/' (тот же бланк, что СВ)
    {{0x10,0x30,0x7F,0x30,0x10,0x00,0x00,0x00}}, // 10: игрок З    '<'
    {{0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x00}}, // 11: игрок СЗ   '\' (тот же бланк, что ЮВ)
    {{0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}}, // 12: рамка круга 'O'

    // ---- Рамка полосы энергии/стамины (см. AsciiEffect::end() / setStamina()) ----
    {{0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0x00}}, // 13: горизонтальная граница '='
    {{0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18}}, // 14: вертикальная граница   '|'
    {{0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}}, // 15: угол рамки             '+'
    {{0x00,0x18,0x3C,0x3C,0x18,0x18,0x08,0x00}}, // 16: крупная капля (кровавый подтёк)
    {{0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00}}, // 17: мелкая капля (кровавый подтёк)

    // ---- Буквы для меню (заголовок "CELL", кнопки "START"/"EXIT") ----
    // Обычный блочный 8x8-шрифт, тот же стиль, что и остальной атлас.
    // Индексы стабильны — используются в main.cpp при раскладке текста
    // (см. charToGlyphIndex() там).
    {{0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00}}, // 18: 'A'
    {{0x00,0x3C,0x66,0x60,0x66,0x3C,0x00,0x00}}, // 19: 'C'
    {{0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00}}, // 20: 'E'
    {{0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00}}, // 21: 'I'
    {{0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}}, // 22: 'L'
    {{0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00}}, // 23: 'R'
    {{0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}}, // 24: 'S'
    {{0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}}, // 25: 'T'
    {{0xC3,0x66,0x3C,0x18,0x3C,0x66,0xC3,0x00}}, // 26: 'X'

    // ---- "Плотные" варианты для ASCII-арт текста (см. MainMenu.h) ----
    // НЕ буквы — просто разные по рисунку, но одинаково "тёмные"/плотные
    // узоры (переиспользованы из основного растрового атласа s_glyphs[]
    // выше, они уже проверены визуально). Используются вперемешку вместо
    // одного повторяющегося '#', чтобы крупные ASCII-буквы заголовка
    // состояли из разных символов, а не выглядели сплошным блоком.
    {{0xFF,0x99,0xFF,0x99,0xFF,0x99,0xFF,0x99}}, // 27
    {{0xFF,0xE7,0xFF,0xE7,0xFF,0xE7,0xFF,0xE7}}, // 28
    {{0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x62,0x3C}}, // 29 ('@'-подобный)
    {{0x66,0xFF,0xDB,0xFF,0xFF,0xDB,0xFF,0x66}}, // 30
    {{0xF0,0x0F,0xF0,0x0F,0xF0,0x0F,0xF0,0x0F}}, // 31 (диагональные полосы)
    {{0x0F,0xF0,0x0F,0xF0,0x0F,0xF0,0x0F,0xF0}}, // 32 (диагональные полосы, инверс)
    {{0xEF,0xDB,0xBD,0x7E,0x7E,0xBD,0xDB,0xEF}}, // 33

    // ---- Остальные буквы латиницы (для связного текста, см. дневники) ----
    // Тот же простой блочный 8x8-стиль, что и A/C/E/I/L/R/S/T/X выше —
    // без декоративных "плотных" вариантов, обычные читаемые буквы.
    {{0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}}, // 34: 'B'
    {{0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}}, // 35: 'D'
    {{0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00}}, // 36: 'F'
    {{0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00}}, // 37: 'G'
    {{0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}}, // 38: 'H'
    {{0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00}}, // 39: 'J'
    {{0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}}, // 40: 'K'
    {{0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}}, // 41: 'M'
    {{0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00}}, // 42: 'N'
    {{0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}}, // 43: 'O'
    {{0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}}, // 44: 'P'
    {{0x3C,0x66,0x66,0x66,0x66,0x6C,0x36,0x00}}, // 45: 'Q'
    {{0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}}, // 46: 'U'
    {{0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}}, // 47: 'V'
    {{0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}}, // 48: 'W'
    {{0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}}, // 49: 'Y'
    {{0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}}, // 50: 'Z'

    // ---- Цифры ----
    {{0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}}, // 51: '0'
    {{0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}}, // 52: '1'
    {{0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00}}, // 53: '2'
    {{0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}}, // 54: '3'
    {{0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00}}, // 55: '4'
    {{0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}}, // 56: '5'
    {{0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}}, // 57: '6'
    {{0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}}, // 58: '7'
    {{0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}}, // 59: '8'
    {{0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}}, // 60: '9'

    // ---- Базовая пунктуация для связного текста (дневники) ----
    {{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}}, // 61: '.'
    {{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}}, // 62: ','
    {{0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}}, // 63: '\'' (апостроф)
    {{0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}}, // 64: '-'
    {{0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}}, // 65: ':'
    {{0x3C,0x66,0x0C,0x18,0x18,0x00,0x18,0x00}}, // 66: '?'
    {{0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00}}, // 67: '!'
};
static const int s_minimapGlyphCount = sizeof(s_minimapGlyphs) / sizeof(s_minimapGlyphs[0]);

// Именованные индексы полосы стамины внутри общего UI-атласа
// (s_minimapGlyphs[]) — используются и в C++, и подразумеваются
// (как литералы) в GLSL-шейдере ниже, т.к. GLSL-строка не может
// #include-ить эти константы напрямую.
//   STAMINA_GLYPH_EMPTY     = 0  (пусто, см. индекс 0 выше)
//   STAMINA_GLYPH_FILL      = 1  ('#', см. индекс 1 — тот же символ, что стена)
//   STAMINA_GLYPH_HLINE     = 13
//   STAMINA_GLYPH_VLINE     = 14
//   STAMINA_GLYPH_CORNER    = 15
//   STAMINA_GLYPH_DRIP_BIG  = 16 (кровавый подтёк, нижняя граница)
//   STAMINA_GLYPH_DRIP_SMALL= 17 (кровавый подтёк, нижняя граница)
//
// Буквы для UI-текста — см. s_minimapGlyphs[] индексы 18-26 (изначально
// заведены только под слова "CELL"/"START"/"EXIT") и 34-50 (остальные
// буквы латиницы, добавлены под связный текст дневников — см.
// Diaries.h). Вместе индексы 18-26+34-50 покрывают полный алфавит A-Z.
//   GLYPH_A = 18   GLYPH_R = 23   GLYPH_B = 34   GLYPH_M = 41  GLYPH_W = 48
//   GLYPH_C = 19   GLYPH_S = 24   GLYPH_D = 35   GLYPH_N = 42  GLYPH_Y = 49
//   GLYPH_E = 20   GLYPH_T = 25   GLYPH_F = 36   GLYPH_O = 43  GLYPH_Z = 50
//   GLYPH_I = 21   GLYPH_X = 26   GLYPH_G = 37   GLYPH_P = 44
//   GLYPH_L = 22                  GLYPH_H = 38   GLYPH_Q = 45
//                                 GLYPH_J = 39   GLYPH_U = 46
//                                 GLYPH_K = 40   GLYPH_V = 47
//
// "Плотные" узоры-заполнители для ASCII-арт текста (см. MainMenu.h) —
// индексы 27-33, без фиксированного смысла, используются вперемешку.
//
// Цифры 0-9 — индексы 51-60. Пунктуация для связного текста (точка,
// запятая, апостроф, тире, двоеточие, '?', '!') — индексы 61-67.
// Всё в том же простом 8x8-стиле, без "плотных" декоративных вариантов —
// это буквы для чтения, а не для крупного ASCII-арт заголовка.

void AsciiEffect::generateMinimapFontAtlas() {
    m_minimapGlyphCount = s_minimapGlyphCount;

    const int glyphSize = m_cellSize;
    const int atlasW = glyphSize * m_minimapGlyphCount;
    const int atlasH = glyphSize;
    std::vector<unsigned char> pixels(atlasW * atlasH, 0);

    for (int level = 0; level < m_minimapGlyphCount; level++) {
        const GlyphDef& g = s_minimapGlyphs[level];
        for (int y = 0; y < glyphSize; y++) {
            int srcRow = (y * 8) / glyphSize;
            unsigned char rowBits = g.rows[srcRow];
            for (int x = 0; x < glyphSize; x++) {
                int srcCol = (x * 8) / glyphSize;
                bool on = (rowBits >> (7 - srcCol)) & 1;
                pixels[y * atlasW + (level * glyphSize + x)] = on ? 255 : 0;
            }
        }
    }

    glGenTextures(1, &m_minimapFontTex);
    glBindTexture(GL_TEXTURE_2D, m_minimapFontTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasW, atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void AsciiEffect::generateFontAtlas() {
    // Считаем "плотность чернил" (кол-во включенных пикселей) каждого
    // глифа и сортируем от самого пустого к самому плотному — это и
    // формирует правильную рампу яркости, как делает настоящий
    // ASCII-art конвертер.
    std::vector<int> order(s_glyphCount);
    for (int i = 0; i < s_glyphCount; i++) order[i] = i;

    auto density = [](const GlyphDef& g) {
        int count = 0;
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 8; b++)
                if ((g.rows[r] >> b) & 1) count++;
        return count;
    };

    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return density(s_glyphs[a]) < density(s_glyphs[b]);
    });

    m_rampLength = s_glyphCount;

    const int glyphSize = m_cellSize;
    const int atlasW = glyphSize * m_rampLength;
    const int atlasH = glyphSize;
    std::vector<unsigned char> pixels(atlasW * atlasH, 0);

    for (int level = 0; level < m_rampLength; level++) {
        const GlyphDef& g = s_glyphs[order[level]];
        for (int y = 0; y < glyphSize; y++) {
            int srcRow = (y * 8) / glyphSize;
            unsigned char rowBits = g.rows[srcRow];
            for (int x = 0; x < glyphSize; x++) {
                int srcCol = (x * 8) / glyphSize;
                bool on = (rowBits >> (7 - srcCol)) & 1;
                pixels[y * atlasW + (level * glyphSize + x)] = on ? 255 : 0;
            }
        }
    }

    glGenTextures(1, &m_fontTex);
    glBindTexture(GL_TEXTURE_2D, m_fontTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasW, atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void AsciiEffect::createQuad() {
    float verts[] = {
        // pos        // uv
        -1.f, -1.f,   0.f, 0.f,
         1.f, -1.f,   1.f, 0.f,
         1.f,  1.f,   1.f, 1.f,

        -1.f, -1.f,   0.f, 0.f,
         1.f,  1.f,   1.f, 1.f,
        -1.f,  1.f,   0.f, 1.f,
    };
    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
}

// ---------------- Публичное API ----------------

bool AsciiEffect::init(int sceneWidth, int sceneHeight, int cellSize) {
    m_cellSize = cellSize;
    m_normalCellSize = cellSize;
    createFBO(sceneWidth, sceneHeight);
    generateFontAtlas();
    generateMinimapFontAtlas();
    createQuad();

    const std::string vertSrc = ShaderLoader::LoadSource("assets/shaders/ascii_post.vert");
    const std::string fragSrc = ShaderLoader::LoadSource("assets/shaders/ascii_post.frag");
    GLuint vs = ShaderProgram::CompileShader(GL_VERTEX_SHADER, vertSrc.c_str(), "AsciiEffect");
    GLuint fs = ShaderProgram::CompileShader(GL_FRAGMENT_SHADER, fragSrc.c_str(), "AsciiEffect");
    m_program = ShaderProgram::LinkProgram(vs, fs, "AsciiEffect");
    cacheUniformLocations();
    return m_program != 0;
}

void AsciiEffect::cacheUniformLocations() {
    if (!m_program) return;
    m_uniSceneTex           = glGetUniformLocation(m_program, "sceneTex");
    m_uniFontTex             = glGetUniformLocation(m_program, "fontTex");
    m_uniScreenResolution    = glGetUniformLocation(m_program, "screenResolution");
    m_uniCellSize            = glGetUniformLocation(m_program, "cellSize");
    m_uniRampLength          = glGetUniformLocation(m_program, "rampLength");
    m_uniMinimapFontTex      = glGetUniformLocation(m_program, "minimapFontTex");
    m_uniMinimapGlyphCount   = glGetUniformLocation(m_program, "minimapGlyphCount");
    m_uniMinimapTex          = glGetUniformLocation(m_program, "minimapTex");
    m_uniMinimapGridSize     = glGetUniformLocation(m_program, "minimapGridSize");
    m_uniMinimapYawDeg       = glGetUniformLocation(m_program, "minimapYawDeg");
    m_uniStaminaFrac         = glGetUniformLocation(m_program, "staminaFrac");
    m_uniStaminaAlpha        = glGetUniformLocation(m_program, "staminaAlpha");
    m_uniHealthFrac          = glGetUniformLocation(m_program, "healthFrac");
    m_uniUiTex               = glGetUniformLocation(m_program, "uiTex");
    m_uniUiCols              = glGetUniformLocation(m_program, "uiCols");
    m_uniUiRows              = glGetUniformLocation(m_program, "uiRows");
    m_uniUiEnabled           = glGetUniformLocation(m_program, "uiEnabled");
    m_uniFadeAlpha           = glGetUniformLocation(m_program, "fadeAlpha");
    m_uniColorEnabled        = glGetUniformLocation(m_program, "colorEnabled");
    m_uniLensEffectEnabled   = glGetUniformLocation(m_program, "lensEffectEnabled");
    m_uniTime                = glGetUniformLocation(m_program, "uTime");
}

void AsciiEffect::resize(int sceneWidth, int sceneHeight) {
    destroyFBO();
    createFBO(sceneWidth, sceneHeight);
}

void AsciiEffect::shutdown() {
    destroyFBO();
    if (m_fontTex) glDeleteTextures(1, &m_fontTex);
    if (m_minimapFontTex) glDeleteTextures(1, &m_minimapFontTex);
    if (m_uiOverlayTex) glDeleteTextures(1, &m_uiOverlayTex);
    // m_minimapDataTex НЕ удаляем — им владеет DungeonScene.
    if (m_program) glDeleteProgram(m_program);
    if (m_quadVBO) glDeleteBuffers(1, &m_quadVBO);
    if (m_quadVAO) glDeleteVertexArrays(1, &m_quadVAO);
}

void AsciiEffect::setMinimap(GLuint dataTex, int gridSize, float playerYawDegrees) {
    m_minimapDataTex  = dataTex;
    m_minimapGridSize = gridSize;
    m_minimapYawDeg   = playerYawDegrees;
}

void AsciiEffect::setStamina(float fraction01, bool enabled) {
    m_staminaFrac = fraction01;
    // Только запоминаем ЦЕЛЬ — фактическая видимость (m_staminaAlpha)
    // плавно догоняет её в end() по реальному прошедшему времени,
    // а не переключается мгновенно (см. комментарий в end()).
    m_staminaEnabledTarget = enabled;
}

void AsciiEffect::setHealth(float fraction01) {
    m_healthFrac = fraction01;
}

void AsciiEffect::setCinematicMode(bool enabled, int cinematicCellSize) {
    // Значение по умолчанию (см. AsciiEffect.h) исторически совпадает с
    // тем, что раньше было захардкожено здесь; теперь его удобно менять
    // снаружи одним местом — см. DevTools::kCinematicCellSize.
    const int newCellSize = enabled ? cinematicCellSize : m_normalCellSize;

    if (enabled == m_cinematicMode && newCellSize == m_cellSize) return; // уже в нужном режиме
    m_cinematicMode = enabled;

    if (newCellSize == m_cellSize) return;

    m_cellSize = newCellSize;

    // Оба атласа шрифтов запекают глифы под конкретный пиксельный размер
    // ячейки (см. generateFontAtlas()/generateMinimapFontAtlas()), так
    // что при смене m_cellSize их нужно пересоздать.
    if (m_fontTex) { glDeleteTextures(1, &m_fontTex); m_fontTex = 0; }
    if (m_minimapFontTex) { glDeleteTextures(1, &m_minimapFontTex); m_minimapFontTex = 0; }

    generateFontAtlas();
    generateMinimapFontAtlas();
}

void AsciiEffect::setUserCellSize(int cellSize) {
    cellSize = std::clamp(cellSize, kMinCellSize, kMaxCellSize);
    m_normalCellSize = cellSize;

    // Кинематографичный режим сейчас активен — не трогаем m_cellSize
    // прямо сейчас, просто запомнили новое "обычное" значение (см.
    // m_normalCellSize выше в setCinematicMode()) — применится само,
    // когда пользователь выйдет из noclip/кинематографичного режима.
    if (m_cinematicMode) return;

    if (cellSize == m_cellSize) return;

    m_cellSize = cellSize;

    if (m_fontTex) { glDeleteTextures(1, &m_fontTex); m_fontTex = 0; }
    if (m_minimapFontTex) { glDeleteTextures(1, &m_minimapFontTex); m_minimapFontTex = 0; }

    generateFontAtlas();
    generateMinimapFontAtlas();
}

void AsciiEffect::setUIOverlay(bool enabled, const std::vector<unsigned char>& grid, int cols, int rows) {
    m_uiOverlayEnabled = enabled;

    if (!enabled) {
        // Данные не трогаем — при следующем enabled=true с тем же grid
        // не нужно было бы ничего перезаливать, но проще и надёжнее
        // просто перезаливать всегда ниже, когда включено.
        return;
    }

    m_uiOverlayCols = cols;
    m_uiOverlayRows = rows;

    if (cols <= 0 || rows <= 0 || (int)grid.size() < cols * rows) {
        m_uiOverlayEnabled = false;
        return;
    }

    if (!m_uiOverlayTex) {
        glGenTextures(1, &m_uiOverlayTex);
        glBindTexture(GL_TEXTURE_2D, m_uiOverlayTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, m_uiOverlayTex);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Заголовок CELL на экране меню анимируется (буквы сыплются/
    // наклоняются/падают), поэтому grid действительно меняется КАЖДЫЙ
    // кадр, пока меню открыто — это ожидаемо, и мы по-прежнему заливаем
    // новые данные каждый вызов. Меняется здесь только то, ЧЕМ мы это
    // делаем: если размер текстуры (cols x rows) не изменился с прошлого
    // вызова, переиспользуем уже выделенную GPU-память (glTexSubImage2D)
    // вместо того, чтобы каждый кадр выделять её заново (glTexImage2D).
    // Если размер поменялся (например, ресайз окна) — выделяем заново,
    // как раньше.
    if (cols == m_uiOverlayTexW && rows == m_uiOverlayTexH) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RED, GL_UNSIGNED_BYTE, grid.data());
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, cols, rows, 0, GL_RED, GL_UNSIGNED_BYTE, grid.data());
        m_uiOverlayTexW = cols;
        m_uiOverlayTexH = rows;
    }
}

void AsciiEffect::setFadeAlpha(float alpha01) {
    m_fadeAlpha = std::clamp(alpha01, 0.0f, 1.0f);
}

void AsciiEffect::begin() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_fboW, m_fboH);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void AsciiEffect::end(int windowWidth, int windowHeight) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowWidth, windowHeight);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneTex);
    // Build the mip chain for the just-finished 3D pass ONCE here, so the
    // post shader can read an already-downsampled sample via textureLod()
    // instead of hand-averaging a neighborhood per pixel (see
    // ascii_post.frag) — hardware mip generation is a well-optimized,
    // GPU-side box downsample, done a single time per frame.
    glGenerateMipmap(GL_TEXTURE_2D);
    glUniform1i(m_uniSceneTex, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_fontTex);
    glUniform1i(m_uniFontTex, 1);

    glUniform2f(m_uniScreenResolution, (float)windowWidth, (float)windowHeight);
    glUniform1f(m_uniCellSize, (float)m_cellSize);
    glUniform1f(m_uniRampLength, (float)m_rampLength);

    // ---- Общий UI-шрифт (мини-карта + полоса стамины) ----
    // Атлас/кол-во глифов нужны обоим HUD-элементам, поэтому биндим их
    // безусловно, а не только когда активна мини-карта — иначе полоса
    // стамины осталась бы без шрифта, если setMinimap() в этом кадре
    // не вызывался.
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_minimapFontTex);
    glUniform1i(m_uniMinimapFontTex, 3);
    glUniform1f(m_uniMinimapGlyphCount, (float)m_minimapGlyphCount);

    // ---- Мини-карта ----
    // Если данные не переданы (setMinimap не вызывался в этом кадре),
    // minimapGridSize останется 0 и шейдер просто пропустит блок мини-карты.
    if (m_minimapDataTex != 0 && m_minimapGridSize > 0) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_minimapDataTex);
        glUniform1i(m_uniMinimapTex, 2);

        glUniform1f(m_uniMinimapGridSize, (float)m_minimapGridSize);
        glUniform1f(m_uniMinimapYawDeg, m_minimapYawDeg);
    } else {
        glUniform1f(m_uniMinimapGridSize, 0.0f);
    }

    // ---- Полоса энергии/стамины ----
    // Плавный fade: m_staminaAlpha постепенно догоняет цель
    // (m_staminaEnabledTarget) по реальному прошедшему времени, а не
    // переключается мгновенно — так вход/выход из noclip не "дёргает"
    // полосу, а плавно скрывает/показывает её (см. main.cpp).
    {
        auto now = std::chrono::steady_clock::now();
        float dt = 0.0f;
        if (m_staminaFadeTimeValid) {
            dt = std::chrono::duration<float>(now - m_staminaFadeLastTime).count();
            // Защита от огромного dt после паузы/лагов/первого кадра.
            dt = std::min(dt, 0.1f);
        }
        m_staminaFadeLastTime = now;
        m_staminaFadeTimeValid = true;

        const float kFadeSpeed = 4.0f; // больше = быстрее fade (~0.25с "постоянная времени")
        const float target = m_staminaEnabledTarget ? 1.0f : 0.0f;
        const float t = 1.0f - std::exp(-kFadeSpeed * dt);
        m_staminaAlpha += (target - m_staminaAlpha) * t;
        m_staminaAlpha = std::clamp(m_staminaAlpha, 0.0f, 1.0f);
    }

    glUniform1f(m_uniStaminaFrac, m_staminaFrac);
    glUniform1f(m_uniStaminaAlpha, m_staminaAlpha);
    glUniform1f(m_uniHealthFrac, m_healthFrac);

    // ---- UI-текст (заголовок/кнопки меню, см. setUIOverlay()) ----
    if (m_uiOverlayEnabled && m_uiOverlayTex != 0) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, m_uiOverlayTex);
        glUniform1i(m_uniUiTex, 4);
        glUniform1f(m_uniUiCols, (float)m_uiOverlayCols);
        glUniform1f(m_uniUiRows, (float)m_uiOverlayRows);
        glUniform1f(m_uniUiEnabled, 1.0f);
    } else {
        glUniform1f(m_uniUiEnabled, 0.0f);
    }

    glUniform1f(m_uniFadeAlpha, m_fadeAlpha);
    glUniform1f(m_uniColorEnabled, m_colorEnabled ? 1.0f : 0.0f);
    glUniform1f(m_uniLensEffectEnabled, m_lensEffectEnabled ? 1.0f : 0.0f);
    glUniform1f(m_uniTime, m_time);

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
