#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

enum class OrderState { Created, Assigned, PickedUp, Delivered, Rejected, Pending };

enum class RejectReason { QueueFull, Unreachable, NoCourier };

enum class EventType {
    OrderCreated,
    OrderAssigned,
    PickupStarted,
    OrderPickedUp,
    OrderDelivered,
    OrderRejected,
    CourierBreakdown,
    OrderReassigned,
    SimulationStopping
};

const char* toString(OrderState state);
const char* toString(RejectReason reason);
const char* toString(EventType event);

bool isFinal(OrderState state);

bool canTransition(OrderState from, OrderState to);

struct InvalidTransition : std::logic_error {
    using std::logic_error::logic_error;
};

struct Order {
    std::string id;
    std::string restaurantId;
    std::string deliveryNode;
    std::int64_t createdAtMs = 0;
    std::int64_t readyAtMs = 0;

    OrderState state = OrderState::Created;
    std::string courierId;
    std::optional<RejectReason> rejectReason;
    std::int64_t deliveredAtMs = 0;

    Order() = default;
    Order(const Order&) = delete;
    Order& operator=(const Order&) = delete;
    Order(Order&&) = default;
    Order& operator=(Order&&) = default;

    void transitionTo(OrderState next);

    void assignTo(const std::string& courier);
    void reject(RejectReason reason);
    void markDelivered(std::int64_t atMs);

    void release();
};
