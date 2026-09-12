#include "DiagonalCorridors.h"
#include <vector>

namespace DiagonalCorridors {

std::vector<unsigned char> DetectChains(
    int mapW, int mapH,
    const std::vector<int>& map,
    int minChainLength)
{
    std::vector<unsigned char> result((size_t)mapW * (size_t)mapH, 0);
    if (mapW <= 0 || mapH <= 0) return result;

    // Тип eligible-угла на каждую клетку — один проход, переиспользуется
    // и для построения графа цепочек, и позже для проверки типа соседа.
    std::vector<WallShapes::CornerCut> cutType((size_t)mapW * (size_t)mapH);
    for (int z = 0; z < mapH; ++z) {
        for (int x = 0; x < mapW; ++x) {
            cutType[(size_t)z * mapW + x] = WallShapes::GetEligibleCornerCut(mapW, mapH, map, x, z);
        }
    }

    // BFS по диагональным соседям (±2,±2) ОДНОГО типа — расстояние 2
    // соответствует геометрии MapGenerator ("1 шаг = 2 клетки", см.
    // DiagonalCorridors.h). Проверяем все 4 диагональных смещения, а не
    // только "ожидаемую" ось для конкретного типа (см. комментарий в
    // заголовке — так надёжнее, не полагается на выведенную вручную
    // геометрию, только на факт "сосед того же типа на диагональном
    // расстоянии 2").
    static constexpr int kOffsets[4][2] = { {2,2}, {-2,-2}, {2,-2}, {-2,2} };

    std::vector<unsigned char> visited((size_t)mapW * (size_t)mapH, 0);
    std::vector<int> stack;
    std::vector<int> component;

    for (int z = 0; z < mapH; ++z) {
        for (int x = 0; x < mapW; ++x) {
            const size_t idx = (size_t)z * mapW + x;
            if (cutType[idx] == WallShapes::CornerCut::None || visited[idx]) continue;

            stack.clear();
            component.clear();
            stack.push_back((int)idx);
            visited[idx] = 1;
            component.push_back((int)idx);

            while (!stack.empty()) {
                const int cur = stack.back();
                stack.pop_back();
                const int cx = cur % mapW;
                const int cz = cur / mapW;
                const WallShapes::CornerCut curType = cutType[(size_t)cur];

                for (const auto& off : kOffsets) {
                    const int nx = cx + off[0];
                    const int nz = cz + off[1];
                    if (nx < 0 || nx >= mapW || nz < 0 || nz >= mapH) continue;
                    const size_t nidx = (size_t)nz * mapW + nx;
                    if (visited[nidx] || cutType[nidx] != curType) continue;

                    visited[nidx] = 1;
                    stack.push_back((int)nidx);
                    component.push_back((int)nidx);
                }
            }

            if ((int)component.size() >= minChainLength) {
                for (int cellIdx : component) result[(size_t)cellIdx] = 1;
            }
        }
    }

    return result;
}

} // namespace DiagonalCorridors
