#include "travel_table.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <utility>

#include "parallel_for.h"

namespace {

void dijkstraRow(const CsrGraph& g, int source, int* row) {
    const std::size_t n = static_cast<std::size_t>(g.n);
    std::fill(row, row + n, kUnreachableMs);
    using Item = std::pair<int, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    row[source] = 0;
    queue.push({0, source});
    while (!queue.empty()) {
        const auto [d, u] = queue.top();
        queue.pop();
        if (d > row[u]) continue;
        for (int e = g.rowStart[static_cast<std::size_t>(u)]; e < g.rowStart[static_cast<std::size_t>(u) + 1]; ++e) {
            const int v = g.to[static_cast<std::size_t>(e)];
            const int nd = d + g.weightMs[static_cast<std::size_t>(e)];
            if (nd < row[v]) {
                row[v] = nd;
                queue.push({nd, v});
            }
        }
    }
}

}

std::vector<int> travelTableCpu(const CsrGraph& g) {
    const std::size_t n = static_cast<std::size_t>(g.n);
    std::vector<int> table(n * n);
    for (int s = 0; s < g.n; ++s) dijkstraRow(g, s, table.data() + static_cast<std::size_t>(s) * n);
    return table;
}

std::vector<int> travelTableCpuParallel(const CsrGraph& g, ThreadPool& pool) {
    const std::size_t n = static_cast<std::size_t>(g.n);
    std::vector<int> table(n * n);
    int* data = table.data();
    parallelFor(pool, n, [&g, data, n](std::size_t begin, std::size_t end, std::size_t) {
        for (std::size_t s = begin; s < end; ++s) dijkstraRow(g, static_cast<int>(s), data + s * n);
    });
    return table;
}

std::size_t TravelTable::unreachablePairs() const {
    return static_cast<std::size_t>(std::count(ms_.begin(), ms_.end(), kUnreachableMs));
}
