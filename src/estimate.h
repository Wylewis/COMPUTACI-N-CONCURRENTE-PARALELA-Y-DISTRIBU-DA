#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#include "config.h"
#include "graph.h"
#include "order.h"

constexpr std::int64_t kPickupHandoverMs = 60'000;
constexpr std::int64_t kDeliveryHandoverMs = 60'000;

struct CourierSnapshot {
    std::string id;
    std::string node;
    std::int64_t availableAtMs = 0;
    int bagCount = 0;
    int bagCapacity = 1;
    bool broken = false;
};

struct Estimate {
    std::string courierId;
    std::int64_t pickupAtMs = 0;
    std::int64_t deliveryAtMs = 0;
    double toRestaurantM = 0.0;
    double toCustomerM = 0.0;
};

struct EstimateError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Estimate estimateDelivery(const CourierSnapshot& courier, const Order& order,
                          const RestaurantConfig& restaurant, const Graph& graph,
                          double speedKmh, std::int64_t nowMs);
