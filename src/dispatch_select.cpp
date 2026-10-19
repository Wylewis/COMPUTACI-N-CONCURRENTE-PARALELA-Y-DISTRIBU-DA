#include "dispatch_select.h"

#include <algorithm>
#include <future>

double straightLineM(const Graph& graph, const std::string& a, const std::string& b) {
    const Graph::Node& na = graph.nodeAt(graph.nodeIndex(a));
    const Graph::Node& nb = graph.nodeAt(graph.nodeIndex(b));
    return distanceM(na.lat, na.lon, nb.lat, nb.lon);
}

std::vector<CourierSnapshot> ringCandidates(const std::vector<CourierSnapshot>& fleet, const Graph& graph,
                                            const std::string& restaurantNode, double innerM, double outerM) {
    std::vector<CourierSnapshot> out;
    for (const CourierSnapshot& c : fleet) {
        if (c.broken || c.bagCount >= c.bagCapacity) continue;
        const double d = straightLineM(graph, c.node, restaurantNode);
        if (d > innerM && d <= outerM) out.push_back(c);
    }
    return out;
}

std::vector<Estimate> estimateAll(const std::vector<CourierSnapshot>& candidates, const Order& order,
                                  const RestaurantConfig& restaurant, const Graph& graph, double speedKmh,
                                  std::int64_t nowMs) {
    std::vector<Estimate> out;
    for (const CourierSnapshot& c : candidates) {
        try {
            out.push_back(estimateDelivery(c, order, restaurant, graph, speedKmh, nowMs));
        } catch (const EstimateError&) {
        }
    }
    return out;
}

AsyncEstimates estimateAllAsync(const std::vector<CourierSnapshot>& candidates, const OneEstimate& one,
                                std::chrono::milliseconds quoteTimeout) {
    std::vector<std::future<Estimate>> futures;
    futures.reserve(candidates.size());
    for (const CourierSnapshot& c : candidates)
        futures.push_back(std::async(std::launch::async, [&one, c] { return one(c); }));
    const auto deadline = std::chrono::steady_clock::now() + quoteTimeout;
    AsyncEstimates out;
    for (std::future<Estimate>& f : futures) {
        if (f.wait_until(deadline) != std::future_status::ready) {
            ++out.late;
            continue;
        }
        try {
            out.estimates.push_back(f.get());
        } catch (const EstimateError&) {
            ++out.failed;
        }
    }
    return out;  // los futures tardios se unen al destruirse: ver D46.3
}

std::optional<Estimate> bestEstimate(const std::vector<Estimate>& estimates) {
    if (estimates.empty()) return std::nullopt;
    return *std::min_element(estimates.begin(), estimates.end(), [](const Estimate& a, const Estimate& b) {
        return a.deliveryAtMs != b.deliveryAtMs ? a.deliveryAtMs < b.deliveryAtMs : a.courierId < b.courierId;
    });
}

std::optional<Selection> selectCourier(const std::vector<CourierSnapshot>& fleet, const Order& order,
                                       const RestaurantConfig& restaurant, const Graph& graph, double speedKmh,
                                       std::int64_t nowMs, double firstRadiusM, const Mapper& mapper) {
    double farthestM = 0.0;
    for (const CourierSnapshot& c : fleet) farthestM = std::max(farthestM, straightLineM(graph, c.node, restaurant.node));

    Selection sel;
    double innerM = -1.0;  // el primer anillo incluye distancia cero
    for (double outerM = firstRadiusM;; innerM = outerM, outerM *= 2.0) {
        ++sel.rings;
        const std::vector<CourierSnapshot> ring = ringCandidates(fleet, graph, restaurant.node, innerM, outerM);
        const std::vector<Estimate> estimates =
            mapper ? mapper(ring) : estimateAll(ring, order, restaurant, graph, speedKmh, nowMs);
        sel.estimated += static_cast<int>(estimates.size());
        if (const auto best = bestEstimate(estimates)) {
            sel.estimate = *best;
            sel.radiusM = outerM;
            return sel;
        }
        if (outerM >= farthestM) return std::nullopt;
    }
}
