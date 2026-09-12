#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include "Zoning.h"

// ============================================================================
// DebugMapOverlay — панель отладки (клавиша M, только для бета-тестирования):
// показывает всю карту лабиринта целиком, без fog of war, в углу экрана, с
// маркером позиции/направления взгляда игрока. Рисуется своим отдельным
// шейдером (assets/shaders/debug_map.{vert,frag}), после AsciiEffect::end()
// (не проходит через основной ASCII-постпроцесс).
//
// Вынесено из DungeonScene при разбиении монолита на модули. Не владеет
// данными карты (см. render() ниже — принимает mapTexture/mapW/mapH/camPos/
// yaw параметрами), т.к. сама карта пока остаётся в DungeonScene (будущий
// MapGenerator). Видимость (m_debugMapVisible, переключается клавишей M)
// также остаётся полем DungeonScene — это состояние ввода, а не рендера.
//
// Также показывает срезанные углы (Шаг 1), колонны (Шаг 2) и границы/
// профиль зон (зонирование) — см. render() ниже. Без этого на debug-карте
// физически не отличить "что-то поменялось" от "всё как раньше": mapTex
// сам по себе кодирует срез угла битом в G-канале (см. debug_map.frag), но
// колонны и зоны в mapTex не попадают вообще (колонна — floor-клетка,
// зона — не часть грида), поэтому им нужны отдельные проходы рисования.
// ============================================================================
class DebugMapOverlay {
public:
    void create();
    void destroy();

    // No-op, если visible == false. mapTexture — полная карта без fog of
    // war (см. MinimapFog::mapTexture()); camPos/yaw — текущая камера
    // игрока (для маркера позиции/направления на панели).
    //
    // columnCentersXZ — мировые XZ-центры колонн (Шаг 2, см. Columns.h) —
    // рисуются отдельными маркерами, т.к. в mapTex колонна — обычная
    // floor-клетка и никак не выделена.
    //
    // zoneGrid — сектора зонирования (см. Zoning.h) — рисуются тонкой
    // сеткой границ + маленьким цветовым "свотчем" на сектор
    // (chamferProbability: синий=низкая, красный=высокая), чтобы можно
    // было на глаз видеть, что разные участки карты реально настроены
    // по-разному, а не гадать по одному только виду стен.
    // enemyPositions — УЛУЧШЕНИЕ ("на карте M показать врагов и где они
    // сейчас, чтобы при включении devtools понимал где они") — мировые
    // XZ-позиции всех активных врагов; рисуются отдельными маркерами,
    // тем же приёмом, что и колонны выше, но другим цветом (красный —
    // "опасность", не путать с зелёным колонн/кнопки).
    void render(int viewportWidth, int viewportHeight, bool visible,
                GLuint mapTexture, int mapW, int mapH,
                const glm::vec3& camPos, float yaw,
                const std::vector<glm::vec2>& columnCentersXZ,
                const Zoning::ZoneGrid& zoneGrid,
                const std::vector<glm::vec3>& enemyPositions);

private:
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    // ---- Cached uniform locations for m_program (see render()) ----
    // Same rationale as DungeonScene/AsciiEffect/Compass: these were
    // being looked up every render() call instead of once after link.
    GLint m_uniMode = -1;
    GLint m_uniColor = -1;
    GLint m_uniAlpha = -1;
    GLint m_uniTex = -1;
    void cacheUniformLocations();
};
