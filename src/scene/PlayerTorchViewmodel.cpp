#include "PlayerTorchViewmodel.h"
#include <cstddef> // offsetof

void PlayerTorchViewmodel::init()
{
    // Геометрия строится ОДИН раз здесь, напрямую в системе координат
    // камеры (OpenGL view-space convention: +X вправо, +Y вверх, -Z
    // вперёд, камера в начале координат) — см. большой комментарий в
    // PlayerTorchViewmodel.h. Мировая позиция восстанавливается в
    // scene.vert через inverse(view) каждый кадр, поэтому здесь эти
    // координаты — просто константы, ничего не пересчитывается.

    // Держится в ЛЕВОЙ руке (-X), заметно ниже уровня глаз (-Y) и
    // немного впереди камеры (-Z). "Рукоять удлинить ВНИЗ" — удлиняем
    // именно за счёт точки хвата (grip), а не за счёт того, где сидит
    // пламя (tip остаётся там же, где читается хорошо в кадре).
    const glm::vec3 grip(-0.30f, -0.60f, -0.38f);
    // ОТКАТ по срочной просьбе — вернули состояние ДО правки "наклон
    // верхушки на игрока" (tip.z был -0.28, tip.x был -0.24 — эти два
    // числа и меняли специально ради того наклона). tip.z=-0.46 (дальше
    // от камеры, чем grip.z=-0.38) и tip.x=-0.30 (совпадает с grip.x) —
    // это и есть состояние ДО той правки.
    const glm::vec3 tip(-0.30f, -0.10f, -0.46f);

    const glm::vec3 handleColor(0.34f, 0.25f, 0.16f); // тот же цвет, что и у настенных факелов
    const glm::vec3 flameColor(1.0f, 0.60f, 0.15f);   // vColor.r=1.0 — множитель яркости в шейдере (см. scene.frag)
    const float handleMatId = 1.0f; // обычный "лит" материал — получает свет от факелов И playerLight

    std::vector<Vertex> verts;
    std::vector<GLuint> indices;

    AddCylinder(
        verts, indices,
        grip, tip,
        0.045f, 0.025f,
        4, // было 10 — "снизь вертексы, сделай рукоять ромбовидной"; 4 грани = призма с ромбовидным сечением вместо круглой
        handleColor,
        handleMatId
    );

    // Зарезервированный индекс пламени (1536) — заведомо выше реального
    // числа факелов на карте и заведомо ниже границы 2048, за которой
    // vMatId = 2.0 + torchIndex/4096 вышел бы за 2.5 и пламя ошибочно
    // попало бы в ветку частиц (vMatId > 2.5) вместо ветки
    // самосветящегося пламени (scene.frag, `if (matId > 1.5)`).
    const int kViewmodelFlameIndex = 1536;
    const float FLAME_ID_SCALE = 4096.0f;
    const float flameMatId = 2.0f + (float)kViewmodelFlameIndex / FLAME_ID_SCALE;

    const glm::vec3 handleDir = glm::normalize(tip - grip);
    m_localFlamePos = tip + handleDir * 0.05f;

    AddSphere(
        verts, indices,
        m_localFlamePos,
        0.070f,
        8, 5,
        flameColor,
        flameMatId
    );

    // ---- Частицы-пепел (см. PlayerTorchViewmodel.h) ----
    // Тот же приём и те же 3 "слота" на факел, что и у настенных
    // (SceneGeometry.cpp::AddTorchMesh) — координаты слотов (направление
    // разлёта + цвет угольков) скопированы оттуда же для визуальной
    // одинаковости. torchId переиспользует тот же зарезервированный
    // индекс 1536, что и у пламени (см. flameMatId выше) — здесь он
    // используется в СВОЕЙ, отдельной кодировке (см. scene.vert:
    // particleKey = torchId*10+slot, matId = 3.0+particleKey/16384), но
    // то же самое число 1536*10+9=15369 всё ещё безопасно меньше
    // PARTICLE_ID_SCALE=16384 и заведомо больше реального числа факелов
    // на карте (после фикса PlaceTorches — сотни, не тысячи).
    m_particleFirstVertex = (GLint)verts.size();
    {
        const int torchIndex = kViewmodelFlameIndex;
        const float PARTICLE_ID_SCALE = 16384.0f;
        const glm::vec3 particleOrigin = m_localFlamePos;

        auto particleMatId = [&](int slot) {
            return 3.0f + (float)(torchIndex * 10 + slot) / PARTICLE_ID_SCALE;
        };

        verts.push_back({ particleOrigin, glm::vec3(-0.030f, 0.40f, 0.010f), glm::vec3(1.0f, 0.48f, 0.08f), particleMatId(0) });
        verts.push_back({ particleOrigin, glm::vec3(0.020f, 0.48f, -0.025f), glm::vec3(1.0f, 0.58f, 0.10f), particleMatId(1) });
        verts.push_back({ particleOrigin, glm::vec3(-0.045f, 0.43f, -0.020f), glm::vec3(1.0f, 0.52f, 0.07f), particleMatId(2) });
    }
    m_particleCount = (GLsizei)verts.size() - m_particleFirstVertex;

    // ---- Смещение +10.0 на matId (и рукояти, и пламени, и частиц) ----
    // Сигнал для scene.vert (uIsViewmodelDraw): "это viewmodel, интерп-
    // ретируй pos/normal как локальные координаты камеры, а не мировые".
    // Шейдер сам вычитает 10.0 обратно перед тем, как использовать matId
    // по его обычному назначению (ветка пламени/материала во фрагментном
    // шейдере эту разницу не видит вообще).
    for (Vertex& v : verts)
        v.matId += 10.0f;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(Vertex)), verts.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, matId));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices.size() * sizeof(GLuint)), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);

    m_indexCount = (GLsizei)indices.size();
}

void PlayerTorchViewmodel::destroy()
{
    if (m_ebo) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    m_indexCount = 0;
    m_particleFirstVertex = 0;
    m_particleCount = 0;
}

glm::vec3 PlayerTorchViewmodel::worldFlamePos(const glm::mat4& invView) const
{
    return glm::vec3(invView * glm::vec4(m_localFlamePos, 1.0f));
}

void PlayerTorchViewmodel::draw() const
{
    if (m_indexCount <= 0)
        return;

    // "Факел должен быть всегда поверх стен": как и оружие-viewmodel в
    // шутерах — отключаем тест глубины ТОЛЬКО на время этой отрисовки.
    glDisable(GL_DEPTH_TEST);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);

    // ---- Частицы-пепел ----
    // Тот же blend/point-size режим, что и у настенных факелов (см.
    // DungeonScene::render() — GL_SRC_ALPHA/GL_ONE, аддитивное свечение
    // угольков, а не обычная альфа-прозрачность), но локально для этого
    // вызова, а не глобально на всю сцену.
    if (m_particleCount > 0)
    {
        glEnable(GL_PROGRAM_POINT_SIZE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);

        glDrawArrays(GL_POINTS, m_particleFirstVertex, m_particleCount);

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_PROGRAM_POINT_SIZE);
    }

    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
}
