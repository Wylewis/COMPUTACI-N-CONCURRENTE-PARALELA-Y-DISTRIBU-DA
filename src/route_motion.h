#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "graph.h"

struct GeoPoint {
    double lat = 0.0;
    double lon = 0.0;
};

std::int64_t travelTimeMs(double meters, double speedKmh);

GeoPoint interpolate(const GeoPoint& a, const GeoPoint& b, double t);

struct RouteLeg {
    std::string from;
    std::string to;
    std::string streetId;
    GeoPoint a;
    GeoPoint b;
    double lengthM = 0.0;
};

class RouteMotion {
public:

    RouteMotion(const Route& route, const Graph& graph, double speedKmh, std::int64_t startMs);

    const std::vector<RouteLeg>& legs() const { return legs_; }
    double lengthM() const { return lengthM_; }
    double speedKmh() const { return speedKmh_; }
    std::int64_t startMs() const { return startMs_; }
    std::int64_t durationMs() const { return durationMs_; }
    std::int64_t arrivalMs() const { return startMs_ + durationMs_; }

    double metersAt(std::int64_t simMs) const;

    GeoPoint positionAtMeters(double meters) const;

    GeoPoint positionAt(std::int64_t simMs) const { return positionAtMeters(metersAt(simMs)); }

    std::size_t legIndexAt(std::int64_t simMs) const;

    std::vector<std::int64_t> legDurationsMs() const;

    bool arrived(std::int64_t simMs) const { return simMs >= arrivalMs(); }

private:
    std::vector<RouteLeg> legs_;
    std::vector<double> cumulativeM_;
    double lengthM_ = 0.0;
    double speedKmh_ = 0.0;
    std::int64_t startMs_ = 0;
    std::int64_t durationMs_ = 0;
    GeoPoint origin_;
};
