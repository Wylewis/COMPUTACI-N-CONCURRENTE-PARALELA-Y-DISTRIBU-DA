#include "demand_trend.h"

#include <algorithm>
#include <cmath>

#include "parallel_for.h"

namespace {

float atBorder(const float* counts, std::size_t n, const float* weights, std::size_t i) {
    float sum = 0.0f;
    for (int k = -kTrendRadius; k <= kTrendRadius; ++k) {
        const long long j = static_cast<long long>(i) + k;
        if (j < 0 || j >= static_cast<long long>(n)) continue;
        sum += weights[k + kTrendRadius] * counts[j];
    }
    return sum;
}

void smoothRange(const float* counts, std::size_t n, const float* weights, float* out, std::size_t begin, std::size_t end) {
    const std::size_t radius = static_cast<std::size_t>(kTrendRadius);
    const std::size_t innerBegin = std::max(begin, std::min(end, radius));
    const std::size_t innerEnd = std::max(innerBegin, std::min(end, n > radius ? n - radius : 0));
    for (std::size_t i = begin; i < innerBegin; ++i) out[i] = atBorder(counts, n, weights, i);
    for (std::size_t i = innerBegin; i < innerEnd; ++i) {
        const float* window = counts + i - radius;
        float sum = 0.0f;
        for (int k = 0; k < kTrendDim; ++k) sum += weights[k] * window[k];
        out[i] = sum;
    }
    for (std::size_t i = innerEnd; i < end; ++i) out[i] = atBorder(counts, n, weights, i);
}

}

std::vector<float> trendWeights(float sigma) {
    std::vector<float> w(static_cast<std::size_t>(kTrendDim));
    float total = 0.0f;
    for (int k = -kTrendRadius; k <= kTrendRadius; ++k) {
        const float v = std::exp(-static_cast<float>(k * k) / (2.0f * sigma * sigma));
        w[static_cast<std::size_t>(k + kTrendRadius)] = v;
        total += v;
    }
    for (float& v : w) v /= total;
    return w;
}

std::vector<float> ordersPerSlot(const std::vector<std::int64_t>& createdMs, std::int64_t slotMs) {
    const std::int64_t width = slotMs > 0 ? slotMs : 1;
    std::int64_t last = -1;
    for (const std::int64_t t : createdMs) last = std::max(last, t);
    if (last < 0) return {};
    std::vector<float> counts(static_cast<std::size_t>(last / width) + 1, 0.0f);
    for (const std::int64_t t : createdMs)
        if (t >= 0) counts[static_cast<std::size_t>(t / width)] += 1.0f;
    return counts;
}

std::vector<float> trendCpu(const float* counts, std::size_t n, const float* weights) {
    std::vector<float> out(n, 0.0f);
    smoothRange(counts, n, weights, out.data(), 0, n);
    return out;
}

std::vector<float> trendCpuParallel(const float* counts, std::size_t n, const float* weights, ThreadPool& pool) {
    std::vector<float> out(n, 0.0f);
    float* data = out.data();
    parallelFor(pool, n, [counts, n, weights, data](std::size_t begin, std::size_t end, std::size_t) {
        smoothRange(counts, n, weights, data, begin, end);
    });
    return out;
}

bool trendsMatch(const std::vector<float>& a, const std::vector<float>& b, float tolerance) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const float scale = std::max(1.0f, std::fabs(a[i]));
        if (!(std::fabs(a[i] - b[i]) <= tolerance * scale)) return false;
    }
    return true;
}

std::size_t trendPeak(const std::vector<float>& trend) {
    return static_cast<std::size_t>(std::max_element(trend.begin(), trend.end()) - trend.begin());
}
