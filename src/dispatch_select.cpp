#include "dispatch_select.h"

#include <algorithm>

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

std::optional<Estimate> bestEstimate(const std::vector<Estimate>& estimates) {
    if (estimates.empty()) return std::nullopt;
    return *std::min_element(estimates.begin(), estimates.end(), [](const Estimate& a, const Estimate& b) {
        return a.deliveryAtMs != b.deliveryAtMs ? a.deliveryAtMs < b.deliveryAtMs : a.courierId < b.courierId;
    });
}

std::optional<Selection> selectCourier(const std::vector<CourierSnapshot>& fleet, const Order& order,
                                       const RestaurantConfig& restaurant, const Graph& graph, double speedKmh,
                                       std::int64_t nowMs, double firstRadiusM) {
    double farthestM = 0.0;
    for (const CourierSnapshot& c : fleet) farthestM = std::max(farthestM, straightLineM(graph, c.node, restaurant.node));

    Selection sel;
    double innerM = -1.0;  // el primer anillo incluye distancia cero
    for (double outerM = firstRadiusM;; innerM = outerM, outerM *= 2.0) {
        ++sel.rings;
        const std::vector<Estimate> estimates =
            estimateAll(ringCandidates(fleet, graph, restaurant.node, innerM, outerM), order, restaurant, graph,
                        speedKmh, nowMs);
        sel.estimated += static_cast<int>(estimates.size());
        if (const auto best = bestEstimate(estimates)) {
            sel.estimate = *best;
            sel.radiusM = outerM;
            return sel;
        }
        if (outerM >= farthestM) return std::nullopt;
    }
}
