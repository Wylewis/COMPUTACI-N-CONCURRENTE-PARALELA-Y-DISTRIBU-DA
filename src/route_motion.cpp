#include "route_motion.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

std::int64_t travelTimeMs(double meters, double speedKmh) {
    if (!(speedKmh > 0.0)) {
        throw std::invalid_argument("fleet.speedKmh debe ser mayor que 0");
    }
    const double metersPerSecond = speedKmh * 1000.0 / 3600.0;
    return static_cast<std::int64_t>(std::llround(std::max(0.0, meters) / metersPerSecond * 1000.0));
}

GeoPoint interpolate(const GeoPoint& a, const GeoPoint& b, double t) {
    const double k = std::max(0.0, std::min(1.0, t));
    return GeoPoint{a.lat + (b.lat - a.lat) * k, a.lon + (b.lon - a.lon) * k};
}

RouteMotion::RouteMotion(const Route& route, const Graph& graph, double speedKmh, std::int64_t startMs)
    : speedKmh_(speedKmh), startMs_(startMs) {
    if (route.nodes.empty()) {
        throw std::invalid_argument("la ruta no tiene nodos");
    }
    if (!(speedKmh > 0.0)) {
        throw std::invalid_argument("fleet.speedKmh debe ser mayor que 0");
    }
    const Graph::Node& first = graph.node(route.nodes.front());
    origin_ = GeoPoint{first.lat, first.lon};

    double total = 0.0;
    for (std::size_t i = 0; i + 1 < route.nodes.size(); ++i) {
        const Graph::Node& na = graph.node(route.nodes[i]);
        const Graph::Node& nb = graph.node(route.nodes[i + 1]);
        RouteLeg leg;
        leg.from = na.id;
        leg.to = nb.id;
        leg.streetId = i < route.streets.size() ? route.streets[i] : "";
        leg.a = GeoPoint{na.lat, na.lon};
        leg.b = GeoPoint{nb.lat, nb.lon};
        leg.lengthM = distanceM(na.lat, na.lon, nb.lat, nb.lon);
        total += leg.lengthM;
        legs_.push_back(leg);
        cumulativeM_.push_back(total);
    }
    lengthM_ = total;
    durationMs_ = travelTimeMs(lengthM_, speedKmh_);
}

double RouteMotion::metersAt(std::int64_t simMs) const {
    if (simMs <= startMs_) return 0.0;
    if (simMs >= arrivalMs()) return lengthM_;
    const double elapsedS = static_cast<double>(simMs - startMs_) / 1000.0;
    const double metersPerSecond = speedKmh_ * 1000.0 / 3600.0;
    return std::min(lengthM_, elapsedS * metersPerSecond);
}

GeoPoint RouteMotion::positionAtMeters(double meters) const {
    if (legs_.empty()) return origin_;
    const double m = std::max(0.0, std::min(lengthM_, meters));
    double start = 0.0;
    for (std::size_t i = 0; i < legs_.size(); ++i) {
        const double end = cumulativeM_[i];
        if (m <= end || i + 1 == legs_.size()) {
            const double len = legs_[i].lengthM;
            const double t = len > 0.0 ? (m - start) / len : 1.0;
            return interpolate(legs_[i].a, legs_[i].b, t);
        }
        start = end;
    }
    return legs_.back().b;
}

std::size_t RouteMotion::legIndexAt(std::int64_t simMs) const {
    if (legs_.empty()) return 0;
    const double m = metersAt(simMs);
    for (std::size_t i = 0; i < legs_.size(); ++i) {
        if (m < cumulativeM_[i]) return i;
    }
    return legs_.size() - 1;
}

std::vector<std::int64_t> RouteMotion::legDurationsMs() const {
    std::vector<std::int64_t> out;
    out.reserve(legs_.size());
    for (const RouteLeg& leg : legs_) out.push_back(travelTimeMs(leg.lengthM, speedKmh_));
    return out;
}
