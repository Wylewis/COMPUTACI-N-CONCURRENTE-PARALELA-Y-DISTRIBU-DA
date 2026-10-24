#include "distance_table_parallel.h"

#include <algorithm>
#include <future>
#include <utility>

DistanceTable buildDistanceTableParallel(const Graph& graph, const std::vector<std::string>& origins,
                                         unsigned tasks) {
    const std::size_t n = origins.size();
    std::vector<DistanceTable::Row> rows(n);
    if (n == 0) return DistanceTable(graph, origins, std::move(rows));

    if (tasks == 0) tasks = defaultTaskCount(n);
    tasks = static_cast<unsigned>(std::min<std::size_t>(tasks, n));

    std::vector<std::future<void>> pending;
    pending.reserve(tasks);
    const std::size_t chunk = (n + tasks - 1) / tasks;
    for (unsigned k = 0; k < tasks; ++k) {
        const std::size_t begin = static_cast<std::size_t>(k) * chunk;
        const std::size_t end = std::min(n, begin + chunk);
        if (begin >= end) break;
        pending.push_back(std::async(std::launch::async, [&graph, &origins, &rows, begin, end] {
            for (std::size_t i = begin; i < end; ++i) {
                rows[i] = DistanceTable::computeRow(graph, origins[i]);
            }
        }));
    }

    for (auto& f : pending) f.get();

    return DistanceTable(graph, origins, std::move(rows));
}
