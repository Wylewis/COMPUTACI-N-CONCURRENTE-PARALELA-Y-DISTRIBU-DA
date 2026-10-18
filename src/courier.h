#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "estimate.h"
#include "graph.h"
#include "order.h"
#include "route_motion.h"

enum class CourierState { Idle, ToRestaurant, Queued, PickingUp, Delivering, BrokenDown };
const char* toString(CourierState state);

struct CourierView {
    std::string id;
    CourierState state = CourierState::Idle;
    std::string node;
    std::shared_ptr<const RouteMotion> trip;
    int bagCount = 0;
    int bagCapacity = 1;
    std::vector<std::string> deliveryNodes;
};

// Monitor de una moto: estado, nodo, viaje en curso y mochila con reserva.
class Courier {
public:
    Courier(std::string id, std::string startNode, int bagCapacity);
    Courier(const Courier&) = delete;
    Courier& operator=(const Courier&) = delete;

    const std::string& id() const { return id_; }
    int bagCapacity() const { return bagCapacity_; }

    bool reserve(Order& order);
    bool hasOrder(const std::string& orderId) const;
    std::optional<Order> removeOrder(const std::string& orderId);
    std::vector<Order> takeBag();
    int bagCount() const;
    bool markPickedUp(const std::string& orderId);

    void startTrip(std::shared_ptr<const RouteMotion> trip, CourierState newState);
    void endTrip(const std::string& atNode, CourierState newState);
    void setState(CourierState state);
    CourierState state() const;
    std::string node() const;

    CourierSnapshot estimateSnapshot(std::int64_t nowMs) const;
    CourierView view() const;
    std::optional<std::string> nearestDelivery(const Graph& graph) const;

private:
    int indexOf(const std::string& orderId) const;  // -1 si no esta en la mochila

    const std::string id_;
    const int bagCapacity_;
    mutable std::mutex mutex_;
    CourierState state_ = CourierState::Idle;
    std::string node_;
    std::shared_ptr<const RouteMotion> trip_;
    std::vector<Order> bag_;
};
