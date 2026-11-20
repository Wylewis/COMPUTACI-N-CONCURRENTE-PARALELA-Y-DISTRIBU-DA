#include "round_trip_reach.h"

#include <algorithm>

#include "parallel_for.h"

namespace {

constexpr std::size_t kTurnBlock = 64;

void countRows(const int* table, const int* turned, std::size_t n, int limitMs, std::size_t begin, std::size_t end, unsigned* reach) {
    for (std::size_t i = begin; i < end; ++i) {
        const int* out = table + i * n;
        const int* back = turned + i * n;
        unsigned count = 0;
        for (std::size_t j = 0; j < n; ++j) count += out[j] + back[j] <= limitMs ? 1u : 0u;
        reach[i] = count;
    }
}

}

std::vector<int> turnedTable(const std::vector<int>& table, int n) {
    const std::size_t side = static_cast<std::size_t>(n > 0 ? n : 0);
    std::vector<int> turned(side * side);
    for (std::size_t bi = 0; bi < side; bi += kTurnBlock)
        for (std::size_t bj = 0; bj < side; bj += kTurnBlock)
            for (std::size_t i = bi; i < std::min(side, bi + kTurnBlock); ++i)
                for (std::size_t j = bj; j < std::min(side, bj + kTurnBlock); ++j) turned[i * side + j] = table[j * side + i];
    return turned;
}

std::vector<unsigned> roundTripReachCpu(const int* table, const int* turned, int n, int limitMs) {
    const std::size_t side = static_cast<std::size_t>(n > 0 ? n : 0);
    std::vector<unsigned> reach(side, 0);
    countRows(table, turned, side, limitMs, 0, side, reach.data());
    return reach;
}

std::vector<unsigned> roundTripReachCpuColumn(const int* table, int n, int limitMs) {
    const std::size_t side = static_cast<std::size_t>(n > 0 ? n : 0);
    std::vector<unsigned> reach(side, 0);
    for (std::size_t i = 0; i < side; ++i) {
        unsigned count = 0;
        for (std::size_t j = 0; j < side; ++j) count += table[i * side + j] + table[j * side + i] <= limitMs ? 1u : 0u;
        reach[i] = count;
    }
    return reach;
}

std::vector<unsigned> roundTripReachCpuParallel(const int* table, const int* turned, int n, ThreadPool& pool, int limitMs) {
    const std::size_t side = static_cast<std::size_t>(n > 0 ? n : 0);
    std::vector<unsigned> reach(side, 0);
    unsigned* out = reach.data();
    parallelFor(pool, side, [table, turned, side, limitMs, out](std::size_t begin, std::size_t end, std::size_t) {
        countRows(table, turned, side, limitMs, begin, end, out);
    });
    return reach;
}

std::size_t widestReach(const std::vector<unsigned>& reach) {
    return static_cast<std::size_t>(std::max_element(reach.begin(), reach.end()) - reach.begin());
}
