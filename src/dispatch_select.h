#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "config.h"
#include "estimate.h"
#include "graph.h"
#include "order.h"
#include "thread_pool.h"

constexpr double kFirstRadiusM = 300.0;

double straightLineM(const Graph& graph, const std::string& a, const std::string& b);

std::vector<CourierSnapshot> ringCandidates(const std::vector<CourierSnapshot>& fleet, const Graph& graph,
                                            const std::string& restaurantNode, double innerM, double outerM);

std::vector<Estimate> estimateAll(const std::vector<CourierSnapshot>& candidates, const Order& order,
                                  const RestaurantConfig& restaurant, const Graph& graph, double speedKmh,
                                  std::int64_t nowMs);

using OneEstimate = std::function<Estimate(const CourierSnapshot&)>;
struct AsyncEstimates {
    std::vector<Estimate> estimates;
    int late = 0;
    int failed = 0;
};
AsyncEstimates estimateAllAsync(const std::vector<CourierSnapshot>& candidates, const OneEstimate& one,
                                std::chrono::milliseconds quoteTimeout);

AsyncEstimates estimateAllPooled(const std::vector<CourierSnapshot>& candidates, const OneEstimate& one,
                                 ThreadPool& pool, std::chrono::milliseconds quoteTimeout);

std::optional<Estimate> bestEstimate(const std::vector<Estimate>& estimates);

struct Selection {
    Estimate estimate;
    double radiusM = 0.0;
    int rings = 0;
    int estimated = 0;
};

using Mapper = std::function<std::vector<Estimate>(const std::vector<CourierSnapshot>&)>;
std::optional<Selection> selectCourier(const std::vector<CourierSnapshot>& fleet, const Order& order,
                                       const RestaurantConfig& restaurant, const Graph& graph, double speedKmh,
                                       std::int64_t nowMs, double firstRadiusM = kFirstRadiusM,
                                       const Mapper& mapper = {});
