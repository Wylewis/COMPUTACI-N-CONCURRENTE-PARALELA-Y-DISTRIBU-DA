#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "config.h"
#include "graph.h"
#include "order.h"

struct OrderBatch {
    std::int64_t atMs = 0;
    std::vector<Order> orders;
};

class OrderGenerator {
public:
    OrderGenerator(const Config& cfg, const Graph& graph);

    std::int64_t nextAtMs() const { return nextAtMs_; }

    OrderBatch next();

    std::int64_t created() const { return counter_; }

private:
    std::int64_t drawIntervalMs();
    int drawBurstSize();

    const Config& cfg_;
    const Graph& graph_;
    std::mt19937_64 rng_;
    std::int64_t nextAtMs_ = 0;
    std::int64_t counter_ = 0;
};

constexpr double kBurstProbability = 0.2;
