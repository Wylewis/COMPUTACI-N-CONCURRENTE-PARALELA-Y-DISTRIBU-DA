#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "graph.h"

class DistanceTable {
public:
    static constexpr double kUnreachable = std::numeric_limits<double>::infinity();

    using Row = std::vector<double>;

    static Row computeRow(const Graph& graph, const std::string& origin);

    DistanceTable(const Graph& graph, const std::vector<std::string>& origins);

    DistanceTable(const Graph& graph, const std::vector<std::string>& origins, std::vector<Row> rows);

    std::size_t originCount() const { return rows_.size(); }
    std::size_t nodeCount() const { return nodeCount_; }
    bool hasOrigin(const std::string& origin) const { return rowIndex_.count(origin) != 0; }

    std::optional<double> distanceM(const std::string& origin, const std::string& target) const;

    const Row& row(const std::string& origin) const;

    std::size_t reachableCount(const std::string& origin) const;

private:
    const Graph& graph_;
    std::size_t nodeCount_ = 0;
    std::unordered_map<std::string, std::size_t> rowIndex_;
    std::vector<Row> rows_;
};
