#include "order_generator.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

OrderGenerator::OrderGenerator(const Config& cfg, const Graph& graph)
    : cfg_(cfg), graph_(graph), rng_(cfg.orders.seed) {
    if (cfg_.restaurants.empty()) {
        throw std::invalid_argument("no se pueden generar pedidos sin restaurantes");
    }
    if (cfg_.nodes.size() < 2) {
        throw std::invalid_argument("se necesitan al menos dos nodos para tener puntos de entrega");
    }
    nextAtMs_ = drawIntervalMs();
}

std::int64_t OrderGenerator::drawIntervalMs() {
    const double mean = static_cast<double>(cfg_.orders.meanIntervalMs);
    if (mean <= 0.0) return 0;
    std::exponential_distribution<double> interval(1.0 / mean);
    return std::max<std::int64_t>(1, std::llround(interval(rng_)));
}

int OrderGenerator::drawBurstSize() {
    const int burstMax = cfg_.orders.burstMax;
    if (burstMax <= 1) return 1;
    std::bernoulli_distribution isBurst(kBurstProbability);
    if (!isBurst(rng_)) return 1;
    std::uniform_int_distribution<int> size(2, burstMax);
    return size(rng_);
}

OrderBatch OrderGenerator::next() {
    OrderBatch batch;
    batch.atMs = nextAtMs_;

    const int size = drawBurstSize();
    std::uniform_int_distribution<std::size_t> pickRestaurant(0, cfg_.restaurants.size() - 1);
    std::uniform_int_distribution<std::size_t> pickNode(0, cfg_.nodes.size() - 2);

    for (int i = 0; i < size; ++i) {
        const RestaurantConfig& r = cfg_.restaurants[pickRestaurant(rng_)];

        std::size_t idx = pickNode(rng_);
        const std::string& restaurantNode = r.node;
        if (cfg_.nodes[idx].id == restaurantNode) idx = cfg_.nodes.size() - 1;
        const std::string& deliveryNode = cfg_.nodes[idx].id;

        std::uniform_int_distribution<std::int64_t> prep(std::min(r.prepMinMs, r.prepMaxMs),
                                                         std::max(r.prepMinMs, r.prepMaxMs));
        Order o;
        o.id = "o" + std::to_string(counter_++);
        o.restaurantId = r.id;
        o.deliveryNode = deliveryNode;
        o.createdAtMs = batch.atMs;
        o.readyAtMs = batch.atMs + prep(rng_);
        if (!graph_.reachable(restaurantNode, deliveryNode)) {
            o.reject(RejectReason::Unreachable);
        }
        batch.orders.push_back(std::move(o));
    }

    nextAtMs_ += drawIntervalMs();
    return batch;
}
