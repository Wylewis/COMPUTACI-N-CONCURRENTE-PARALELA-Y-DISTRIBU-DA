#include "distance_table.h"

#include <queue>
#include <stdexcept>
#include <utility>

DistanceTable::Row DistanceTable::computeRow(const Graph& graph, const std::string& origin) {
    const std::size_t n = graph.nodeCount();
    Row dist(n, kUnreachable);
    const std::size_t source = graph.nodeIndex(origin);

    using Item = std::pair<double, std::size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    dist[source] = 0.0;
    queue.push({0.0, source});

    while (!queue.empty()) {
        const auto [d, u] = queue.top();
        queue.pop();
        if (d > dist[u]) continue;
        for (const Graph::Edge& e : graph.edgesAt(u)) {
            const double nd = d + e.lengthM;
            if (nd < dist[e.to]) {
                dist[e.to] = nd;
                queue.push({nd, e.to});
            }
        }
    }
    return dist;
}

DistanceTable::DistanceTable(const Graph& graph, const std::vector<std::string>& origins)
    : graph_(graph), nodeCount_(graph.nodeCount()) {
    rows_.reserve(origins.size());
    for (const std::string& origin : origins) {
        rowIndex_[origin] = rows_.size();
        rows_.push_back(computeRow(graph, origin));
    }
}

DistanceTable::DistanceTable(const Graph& graph, const std::vector<std::string>& origins,
                             std::vector<Row> rows)
    : graph_(graph), nodeCount_(graph.nodeCount()), rows_(std::move(rows)) {
    if (rows_.size() != origins.size()) {
        throw std::invalid_argument("la tabla de distancias recibio " + std::to_string(rows_.size()) +
                                    " filas para " + std::to_string(origins.size()) + " origenes");
    }
    for (std::size_t i = 0; i < origins.size(); ++i) {
        if (rows_[i].size() != nodeCount_) {
            throw std::invalid_argument("la fila de '" + origins[i] + "' no tiene una entrada por nodo");
        }
        rowIndex_[origins[i]] = i;
    }
}

std::optional<double> DistanceTable::distanceM(const std::string& origin, const std::string& target) const {
    auto it = rowIndex_.find(origin);
    if (it == rowIndex_.end()) return std::nullopt;
    const double d = rows_[it->second][graph_.nodeIndex(target)];
    if (d == kUnreachable) return std::nullopt;
    return d;
}

const DistanceTable::Row& DistanceTable::row(const std::string& origin) const {
    auto it = rowIndex_.find(origin);
    if (it == rowIndex_.end()) {
        throw std::out_of_range("origen sin fila en la tabla de distancias: " + origin);
    }
    return rows_[it->second];
}

std::size_t DistanceTable::reachableCount(const std::string& origin) const {
    std::size_t count = 0;
    for (double d : row(origin)) {
        if (d != kUnreachable) ++count;
    }
    return count;
}
