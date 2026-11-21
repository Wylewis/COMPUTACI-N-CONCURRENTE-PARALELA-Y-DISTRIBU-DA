#include "backlog_curve.h"

#include <algorithm>

#include "parallel_for.h"

std::vector<int> backlogDeltas(const std::vector<std::int64_t>& createdMs, const std::vector<std::int64_t>& closedMs,
                               std::int64_t slotMs) {
    const std::int64_t width = slotMs > 0 ? slotMs : 1;
    std::int64_t last = -1;
    for (const std::int64_t t : createdMs) last = std::max(last, t);
    for (const std::int64_t t : closedMs) last = std::max(last, t);
    if (last < 0) return {};
    std::vector<int> delta(static_cast<std::size_t>(last / width) + 1, 0);
    for (const std::int64_t t : createdMs)
        if (t >= 0) ++delta[static_cast<std::size_t>(t / width)];
    for (const std::int64_t t : closedMs)
        if (t >= 0) --delta[static_cast<std::size_t>(t / width)];
    return delta;
}

std::vector<int> backlogCurveCpu(const int* delta, std::size_t slots) {
    std::vector<int> curve(slots);
    int waiting = 0;
    for (std::size_t i = 0; i < slots; ++i) {
        waiting += delta[i];
        curve[i] = waiting;
    }
    return curve;
}

std::vector<int> backlogCurveCpuParallel(const int* delta, std::size_t slots, ThreadPool& pool) {
    std::vector<int> curve(slots);
    if (slots == 0) return curve;
    std::vector<int> chunkSum(std::max<std::size_t>(1, pool.size()), 0);
    int* sums = chunkSum.data();
    parallelFor(pool, slots, [delta, sums](std::size_t begin, std::size_t end, std::size_t chunk) {
        int total = 0;
        for (std::size_t i = begin; i < end; ++i) total += delta[i];
        sums[chunk] = total;
    });
    int running = 0;
    for (int& s : chunkSum) {
        const int total = s;
        s = running;
        running += total;
    }
    int* out = curve.data();
    parallelFor(pool, slots, [delta, sums, out](std::size_t begin, std::size_t end, std::size_t chunk) {
        int waiting = sums[chunk];
        for (std::size_t i = begin; i < end; ++i) {
            waiting += delta[i];
            out[i] = waiting;
        }
    });
    return curve;
}

BacklogPeak backlogPeak(const std::vector<int>& curve) {
    BacklogPeak peak;
    for (std::size_t i = 0; i < curve.size(); ++i) {
        if (curve[i] > peak.waiting) {
            peak.waiting = curve[i];
            peak.slot = i;
        }
    }
    return peak;
}
