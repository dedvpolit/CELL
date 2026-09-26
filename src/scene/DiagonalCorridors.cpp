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

    // Eligible-corner type per cell: one pass, reused both to build the chain graph and to check
    // the neighbor type later.
    std::vector<WallShapes::CornerCut> cutType((size_t)mapW * (size_t)mapH);
    for (int z = 0; z < mapH; ++z) {
        for (int x = 0; x < mapW; ++x) {
            cutType[(size_t)z * mapW + x] = WallShapes::GetEligibleCornerCut(mapW, mapH, map, x, z);
        }
    }

    // BFS over the diagonal neighbors (+-2,+-2) of the same type: distance 2 matches MapGenerator's
    // geometry ("1 step = 2 cells"). All 4 diagonal offsets are checked, not just the "expected"
    // axis for a given type: more robust, it does not rely on manually derived geometry, only on
    // "same-type neighbor at diagonal distance 2".
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
