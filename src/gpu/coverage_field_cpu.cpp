#include "coverage_field.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <utility>

#include "parallel_for.h"

namespace {

std::size_t cellsOf(const CoverageMap& map) {
    return static_cast<std::size_t>(map.grid > 0 ? map.grid : 0) * static_cast<std::size_t>(map.grid > 0 ? map.grid : 0);
}

int relaxed(int cur, unsigned char open, int left, int right, int up, int down, int stepX, int stepY) {
    const int byRow = std::min(left, right) + stepX;
    const int byCol = std::min(up, down) + stepY;
    const int best = std::min(cur, std::min(byRow, byCol));
    return open ? best : cur;
}

bool sweepRows(const CoverageMap& map, const int* in, int* out, const int* farRow, int firstRow, int lastRow) {
    const int g = map.grid, sx = map.stepXMs, sy = map.stepYMs;
    int changed = 0;
    for (int row = firstRow; row < lastRow; ++row) {
        const int* cur = in + static_cast<std::size_t>(row) * static_cast<std::size_t>(g);
        const int* up = row > 0 ? cur - g : farRow;
        const int* down = row < g - 1 ? cur + g : farRow;
        const unsigned char* open = map.open.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(g);
        int* dst = out + static_cast<std::size_t>(row) * static_cast<std::size_t>(g);
        dst[0] = relaxed(cur[0], open[0], kCoverFar, g > 1 ? cur[1] : kCoverFar, up[0], down[0], sx, sy);
        changed |= dst[0] != cur[0];
        for (int col = 1; col < g - 1; ++col) {
            dst[col] = relaxed(cur[col], open[col], cur[col - 1], cur[col + 1], up[col], down[col], sx, sy);
            changed |= dst[col] != cur[col];
        }
        if (g > 1) {
            dst[g - 1] = relaxed(cur[g - 1], open[g - 1], cur[g - 2], kCoverFar, up[g - 1], down[g - 1], sx, sy);
            changed |= dst[g - 1] != cur[g - 1];
        }
    }
    return changed != 0;
}

}

std::vector<int> coverageSeed(const CoverageMap& map, const float* x, const float* y, std::size_t points) {
    std::vector<int> seed(cellsOf(map), kCoverFar);
    if (map.grid <= 0) return seed;
    for (std::size_t i = 0; i < points; ++i) {
        if (!(x[i] >= 0.0f) || !(y[i] >= 0.0f) || x[i] >= 1.0f || y[i] >= 1.0f) continue;
        const int col = std::min(map.grid - 1, static_cast<int>(x[i] * static_cast<float>(map.grid)));
        const int row = std::min(map.grid - 1, static_cast<int>(y[i] * static_cast<float>(map.grid)));
        seed[static_cast<std::size_t>(row) * static_cast<std::size_t>(map.grid) + static_cast<std::size_t>(col)] = 0;
    }
    return seed;
}

std::vector<int> coverageDijkstra(const CoverageMap& map, const int* seed) {
    const std::size_t cells = cellsOf(map);
    std::vector<int> dist(seed, seed + cells);
    using Entry = std::pair<int, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> frontier;
    for (std::size_t p = 0; p < cells; ++p)
        if (dist[p] < kCoverFar) frontier.push({dist[p], static_cast<int>(p)});
    const int g = map.grid;
    while (!frontier.empty()) {
        const auto [d, p] = frontier.top();
        frontier.pop();
        if (d != dist[static_cast<std::size_t>(p)]) continue;
        const int row = p / g, col = p % g;
        const int next[4] = {col > 0 ? p - 1 : -1, col < g - 1 ? p + 1 : -1, row > 0 ? p - g : -1, row < g - 1 ? p + g : -1};
        for (int k = 0; k < 4; ++k) {
            const int q = next[k];
            if (q < 0 || !map.open[static_cast<std::size_t>(q)]) continue;
            const int through = d + (k < 2 ? map.stepXMs : map.stepYMs);
            if (through < dist[static_cast<std::size_t>(q)]) {
                dist[static_cast<std::size_t>(q)] = through;
                frontier.push({through, q});
            }
        }
    }
    return dist;
}

std::vector<int> coverageCpu(const CoverageMap& map, const int* seed, int* sweeps) {
    const std::size_t cells = cellsOf(map);
    std::vector<int> current(seed, seed + cells), next(cells, kCoverFar);
    const std::vector<int> farRow(static_cast<std::size_t>(map.grid > 0 ? map.grid : 0), kCoverFar);
    int done = 0;
    bool changed = cells > 0;
    while (changed) {
        changed = sweepRows(map, current.data(), next.data(), farRow.data(), 0, map.grid);
        current.swap(next);
        ++done;
    }
    if (sweeps) *sweeps = done;
    return current;
}

std::vector<int> coverageCpuParallel(const CoverageMap& map, const int* seed, ThreadPool& pool, int* sweeps) {
    const std::size_t cells = cellsOf(map);
    std::vector<int> current(seed, seed + cells), next(cells, kCoverFar);
    const std::vector<int> farRow(static_cast<std::size_t>(map.grid > 0 ? map.grid : 0), kCoverFar);
    std::vector<unsigned char> changedBy(std::max(1u, pool.size()), 0);
    int done = 0;
    bool changed = cells > 0;
    while (changed) {
        std::fill(changedBy.begin(), changedBy.end(), static_cast<unsigned char>(0));
        const int* in = current.data();
        int* out = next.data();
        unsigned char* flags = changedBy.data();
        const int* far = farRow.data();
        parallelFor(pool, static_cast<std::size_t>(map.grid),
                    [&map, in, out, far, flags](std::size_t begin, std::size_t end, std::size_t task) {
                        flags[task] = sweepRows(map, in, out, far, static_cast<int>(begin), static_cast<int>(end)) ? 1 : 0;
                    });
        changed = std::any_of(changedBy.begin(), changedBy.end(), [](unsigned char c) { return c != 0; });
        current.swap(next);
        ++done;
    }
    if (sweeps) *sweeps = done;
    return current;
}

CoverageWorst worstCovered(const CoverageMap& map, const std::vector<int>& field) {
    CoverageWorst worst;
    worst.ms = -1;
    for (std::size_t p = 0; p < field.size() && p < map.open.size(); ++p) {
        if (!map.open[p]) continue;
        if (field[p] >= kCoverFar) {
            ++worst.unreached;
            continue;
        }
        ++worst.reached;
        if (field[p] > worst.ms) {
            worst.ms = field[p];
            worst.cell = p;
        }
    }
    if (worst.ms < 0) worst.ms = 0;
    return worst;
}
