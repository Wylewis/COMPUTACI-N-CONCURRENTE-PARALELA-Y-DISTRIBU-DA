#include "demand_heatmap.h"

#include <algorithm>
#include <cmath>

#include "parallel_for.h"

std::vector<float> gaussianWeights(float sigma) {
    std::vector<float> w(static_cast<std::size_t>(kHeatDim * kHeatDim));
    float total = 0.0f;
    for (int dy = -kHeatRadius; dy <= kHeatRadius; ++dy) {
        for (int dx = -kHeatRadius; dx <= kHeatRadius; ++dx) {
            const float v = std::exp(-static_cast<float>(dx * dx + dy * dy) / (2.0f * sigma * sigma));
            w[static_cast<std::size_t>((dy + kHeatRadius) * kHeatDim + dx + kHeatRadius)] = v;
            total += v;
        }
    }
    for (float& v : w) v /= total;
    return w;
}

std::vector<float> binDemand(const float* x, const float* y, std::size_t points, int grid) {
    std::vector<float> counts(static_cast<std::size_t>(grid) * static_cast<std::size_t>(grid), 0.0f);
    if (grid <= 0) return counts;
    for (std::size_t i = 0; i < points; ++i) {
        if (!(x[i] >= 0.0f) || !(y[i] >= 0.0f) || x[i] >= 1.0f || y[i] >= 1.0f) continue;
        const int col = std::min(grid - 1, static_cast<int>(x[i] * static_cast<float>(grid)));
        const int row = std::min(grid - 1, static_cast<int>(y[i] * static_cast<float>(grid)));
        counts[static_cast<std::size_t>(row) * static_cast<std::size_t>(grid) + static_cast<std::size_t>(col)] += 1.0f;
    }
    return counts;
}

namespace {

void smoothRows(const float* counts, int grid, const float* weights, float* out, int firstRow, int lastRow) {
    for (int row = firstRow; row < lastRow; ++row) {
        for (int col = 0; col < grid; ++col) {
            float sum = 0.0f;
            for (int dy = -kHeatRadius; dy <= kHeatRadius; ++dy) {
                const int r = row + dy;
                if (r < 0 || r >= grid) continue;
                for (int dx = -kHeatRadius; dx <= kHeatRadius; ++dx) {
                    const int c = col + dx;
                    if (c < 0 || c >= grid) continue;
                    sum += weights[(dy + kHeatRadius) * kHeatDim + dx + kHeatRadius] * counts[r * grid + c];
                }
            }
            out[row * grid + col] = sum;
        }
    }
}

}

std::vector<float> smoothCpu(const float* counts, int grid, const float* weights) {
    std::vector<float> out(static_cast<std::size_t>(grid) * static_cast<std::size_t>(grid), 0.0f);
    smoothRows(counts, grid, weights, out.data(), 0, grid);
    return out;
}

std::vector<float> smoothCpuParallel(const float* counts, int grid, const float* weights, ThreadPool& pool) {
    std::vector<float> out(static_cast<std::size_t>(grid) * static_cast<std::size_t>(grid), 0.0f);
    float* data = out.data();
    parallelFor(pool, static_cast<std::size_t>(grid), [counts, grid, weights, data](std::size_t begin, std::size_t end, std::size_t) {
        smoothRows(counts, grid, weights, data, static_cast<int>(begin), static_cast<int>(end));
    });
    return out;
}

bool heatmapsMatch(const std::vector<float>& a, const std::vector<float>& b, float tolerance) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const float scale = std::max(1.0f, std::fabs(a[i]));
        if (!(std::fabs(a[i] - b[i]) <= tolerance * scale)) return false;
    }
    return true;
}

std::size_t hottestCell(const std::vector<float>& heat) {
    return static_cast<std::size_t>(std::max_element(heat.begin(), heat.end()) - heat.begin());
}
