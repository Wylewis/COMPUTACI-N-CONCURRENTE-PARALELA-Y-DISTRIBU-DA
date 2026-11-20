#include "response_field.h"

#include <algorithm>

std::vector<int> responseSeed(const CoverageMap& map, const float* x, const float* y, const int* waitMs, std::size_t points) {
    const std::size_t side = static_cast<std::size_t>(map.grid > 0 ? map.grid : 0);
    std::vector<int> seed(side * side, kCoverFar);
    if (map.grid <= 0) return seed;
    for (std::size_t i = 0; i < points; ++i) {
        if (!(x[i] >= 0.0f) || !(y[i] >= 0.0f) || x[i] >= 1.0f || y[i] >= 1.0f) continue;
        const int col = std::min(map.grid - 1, static_cast<int>(x[i] * static_cast<float>(map.grid)));
        const int row = std::min(map.grid - 1, static_cast<int>(y[i] * static_cast<float>(map.grid)));
        int& cell = seed[static_cast<std::size_t>(row) * side + static_cast<std::size_t>(col)];
        cell = std::min(cell, std::min(std::max(waitMs[i], 0), kCoverFar));
    }
    return seed;
}
