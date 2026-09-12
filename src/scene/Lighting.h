#pragma once
#include <glm/glm.hpp>
#include <vector>

// ============================================================================
// Lighting — кэш "активных" факелов (ближайшие к камере, не дальше дальности
// прорисовки), которые реально загружаются в uniform-массивы шейдера сцены
// (torchPos/torchColor/torchIntensity[32], см. assets/shaders/scene.frag).
//
// Вынесено из DungeonScene::render() при разбиении монолита на модули —
// раньше это был инлайн-блок прямо в теле render(). Пересчёт активного
// набора — не каждый кадр, а раз в kRecomputeInterval секунд ИЛИ если
// игрок сдвинулся дальше kRecomputeMoveDist с прошлого пересчёта: набор из
// 32 ближайших факелов физически не может измениться за доли секунды или
// за пару сантиметров движения, так что между пересчётами кэш просто
// переиспользуется. Освещение при этом выглядит абсолютно так же — просто
// считается реже.
// ============================================================================
class Lighting {
public:
    static constexpr double kRecomputeInterval = 1.0 / 20.0; // seconds
    static constexpr float  kRecomputeMoveDist = 0.75f;      // world units

    // now — обычно glfwGetTime(). Пересчитывает активный набор только если
    // кэш ещё не строился, прошло достаточно времени, или камера ушла
    // достаточно далеко — иначе no-op (используется прошлый результат).
    void update(const glm::vec3& camPos, float renderDistance, int maxActiveTorches,
                const std::vector<glm::vec3>& torchFlamePos,
                const std::vector<glm::vec3>& torchColor,
                const std::vector<float>& torchIntensity,
                double now);

    const std::vector<glm::vec3>& activePositions() const { return m_cachedActiveTorchPos; }
    const std::vector<glm::vec3>& activeColors() const { return m_cachedActiveTorchColor; }
    const std::vector<float>& activeIntensities() const { return m_cachedActiveTorchIntensity; }
    int activeCount() const { return m_cachedActiveTorchCount; }

    // Permanent index (into the full torchFlamePos/torchColor/... arrays
    // passed to update(), i.e. DungeonScene::m_torchFlamePos) for each
    // active slot, same order/length as activePositions() etc.
    //
    // NOT currently used by DungeonScene — the engine reverted to the
    // original per-pixel shadowedByWall() raymarch (see scene.frag),
    // which needs no permanent index at all. Left in place in case a
    // baked-visibility approach (see src/scene/LightBaking.h, currently
    // unused/parked for the same reason) is revisited later; harmless
    // either way, it's just a small bit of extra bookkeeping in update().
    const std::vector<int>& activeOriginalIndices() const { return m_cachedActiveTorchOriginalIndex; }

private:
    bool m_cacheValid = false;
    double m_lastRecomputeTime = -1.0;
    glm::vec3 m_lastRecomputeCamPos{ 0.0f };

    int m_cachedActiveTorchCount = 0;
    std::vector<glm::vec3> m_cachedActiveTorchPos;
    std::vector<glm::vec3> m_cachedActiveTorchColor;
    std::vector<float> m_cachedActiveTorchIntensity;
    std::vector<int> m_cachedActiveTorchOriginalIndex;
};
