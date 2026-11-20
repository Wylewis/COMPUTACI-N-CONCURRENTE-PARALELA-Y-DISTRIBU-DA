#include "delivery_histogram.h"

#include <algorithm>

#include "parallel_for.h"

std::vector<unsigned> deliveryHistogramCpu(const int* ms, std::size_t n, int bins, int binMs) {
    const int width = histogramBins(bins);
    std::vector<unsigned> counts(static_cast<std::size_t>(width), 0);
    for (std::size_t i = 0; i < n; ++i) ++counts[static_cast<std::size_t>(deliveryBinOf(ms[i], binMs, width))];
    return counts;
}

std::vector<unsigned> deliveryHistogramCpuParallel(const int* ms, std::size_t n, int bins, ThreadPool& pool, int binMs) {
    const int width = histogramBins(bins);
    const std::size_t slots = static_cast<std::size_t>(width);
    const std::size_t chunks = std::max<std::size_t>(1, pool.size());
    std::vector<unsigned> perChunk(chunks * slots, 0);
    unsigned* all = perChunk.data();
    parallelFor(pool, n, [ms, all, slots, width, binMs](std::size_t begin, std::size_t end, std::size_t chunk) {
        std::vector<unsigned> mine(slots, 0);
        for (std::size_t i = begin; i < end; ++i) ++mine[static_cast<std::size_t>(deliveryBinOf(ms[i], binMs, width))];
        std::copy(mine.begin(), mine.end(), all + chunk * slots);
    });
    std::vector<unsigned> counts(slots, 0);
    for (std::size_t c = 0; c < chunks; ++c)
        for (std::size_t b = 0; b < slots; ++b) counts[b] += perChunk[c * slots + b];
    return counts;
}

int histogramPercentileBin(const std::vector<unsigned>& counts, double fraction) {
    unsigned long long total = 0;
    for (const unsigned c : counts) total += c;
    if (total == 0) return -1;
    const double target = fraction * static_cast<double>(total);
    unsigned long long seen = 0;
    for (std::size_t b = 0; b < counts.size(); ++b) {
        seen += counts[b];
        if (static_cast<double>(seen) >= target && seen > 0) return static_cast<int>(b);
    }
    return static_cast<int>(counts.size()) - 1;
}
