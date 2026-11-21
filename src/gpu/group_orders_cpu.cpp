#include "group_orders.h"

#include <algorithm>

#include "parallel_for.h"

std::vector<int> exclusiveScanCpu(const std::vector<int>& values) {
    std::vector<int> out(values.size() + 1);
    int running = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        out[i] = running;
        running += values[i];
    }
    out[values.size()] = running;
    return out;
}

std::vector<int> exclusiveScanCpuParallel(const std::vector<int>& values, ThreadPool& pool) {
    const std::size_t n = values.size();
    std::vector<int> out(n + 1, 0);
    if (n == 0) return out;
    std::vector<int> chunkSum(std::max<std::size_t>(1, pool.size()), 0);
    const int* in = values.data();
    int* sums = chunkSum.data();
    parallelFor(pool, n, [in, sums](std::size_t begin, std::size_t end, std::size_t chunk) {
        int total = 0;
        for (std::size_t i = begin; i < end; ++i) total += in[i];
        sums[chunk] = total;
    });
    int running = 0;
    for (int& s : chunkSum) {
        const int total = s;
        s = running;
        running += total;
    }
    int* result = out.data();
    parallelFor(pool, n, [in, sums, result](std::size_t begin, std::size_t end, std::size_t chunk) {
        int acc = sums[chunk];
        for (std::size_t i = begin; i < end; ++i) {
            result[i] = acc;
            acc += in[i];
        }
    });
    out[n] = running;
    return out;
}

OrderGroups groupOrdersCpu(const int* group, std::size_t orders, int groups) {
    OrderGroups out;
    std::vector<int> counts(static_cast<std::size_t>(std::max(0, groups)), 0);
    for (std::size_t i = 0; i < orders; ++i) ++counts[static_cast<std::size_t>(group[i])];
    out.start = exclusiveScanCpu(counts);
    std::vector<int> cursor(out.start.begin(), out.start.end() - 1);
    out.order.resize(orders);
    for (std::size_t i = 0; i < orders; ++i) out.order[static_cast<std::size_t>(cursor[static_cast<std::size_t>(group[i])]++)] = static_cast<int>(i);
    return out;
}

OrderGroups groupOrdersCpuParallel(const int* group, std::size_t orders, int groups, ThreadPool& pool) {
    OrderGroups out;
    const std::size_t bins = static_cast<std::size_t>(std::max(0, groups));
    const std::size_t chunks = std::max<std::size_t>(1, pool.size());
    std::vector<int> local(chunks * bins, 0);
    int* histogram = local.data();
    parallelFor(pool, orders, [group, histogram, bins](std::size_t begin, std::size_t end, std::size_t chunk) {
        int* mine = histogram + chunk * bins;
        for (std::size_t i = begin; i < end; ++i) ++mine[static_cast<std::size_t>(group[i])];
    });
    std::vector<int> counts(bins, 0);
    int* total = counts.data();
    parallelFor(pool, bins, [histogram, total, bins, chunks](std::size_t begin, std::size_t end, std::size_t) {
        for (std::size_t g = begin; g < end; ++g) {
            int running = 0;
            for (std::size_t c = 0; c < chunks; ++c) {
                const int here = histogram[c * bins + g];
                histogram[c * bins + g] = running;
                running += here;
            }
            total[g] = running;
        }
    });
    out.start = exclusiveScanCpuParallel(counts, pool);
    out.order.resize(orders);
    const int* start = out.start.data();
    int* order = out.order.data();
    parallelFor(pool, orders, [group, histogram, bins, start, order](std::size_t begin, std::size_t end, std::size_t chunk) {
        int* mine = histogram + chunk * bins;
        for (std::size_t i = begin; i < end; ++i) {
            const std::size_t g = static_cast<std::size_t>(group[i]);
            order[static_cast<std::size_t>(start[g] + mine[g]++)] = static_cast<int>(i);
        }
    });
    return out;
}

void sortWithinGroups(OrderGroups& groups) {
    for (std::size_t g = 0; g + 1 < groups.start.size(); ++g)
        std::sort(groups.order.begin() + groups.start[g], groups.order.begin() + groups.start[g + 1]);
}
