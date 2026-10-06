#include "order.h"

const char* toString(OrderState state) {
    switch (state) {
        case OrderState::Created:   return "created";
        case OrderState::Assigned:  return "assigned";
        case OrderState::PickedUp:  return "pickedUp";
        case OrderState::Delivered: return "delivered";
        case OrderState::Rejected:  return "rejected";
        case OrderState::Pending:   return "pending";
    }
    return "?";
}

const char* toString(RejectReason reason) {
    switch (reason) {
        case RejectReason::QueueFull:   return "queueFull";
        case RejectReason::Unreachable: return "unreachable";
        case RejectReason::NoCourier:   return "noCourier";
    }
    return "?";
}

const char* toString(EventType event) {
    switch (event) {
        case EventType::OrderCreated:       return "orderCreated";
        case EventType::OrderAssigned:      return "orderAssigned";
        case EventType::PickupStarted:      return "pickupStarted";
        case EventType::OrderPickedUp:      return "orderPickedUp";
        case EventType::OrderDelivered:     return "orderDelivered";
        case EventType::OrderRejected:      return "orderRejected";
        case EventType::CourierBreakdown:   return "courierBreakdown";
        case EventType::OrderReassigned:    return "orderReassigned";
        case EventType::SimulationStopping: return "simulationStopping";
    }
    return "?";
}

bool isFinal(OrderState state) {
    return state == OrderState::Delivered || state == OrderState::Rejected ||
           state == OrderState::Pending;
}

bool canTransition(OrderState from, OrderState to) {
    if (isFinal(from)) return false;
    if (to == OrderState::Pending) return true;  // la simulacion termino antes
    switch (from) {
        case OrderState::Created:
            return to == OrderState::Assigned || to == OrderState::Rejected;
        case OrderState::Assigned:
            return to == OrderState::PickedUp;
        case OrderState::PickedUp:
            return to == OrderState::Delivered;
        default:
            return false;
    }
}

void Order::transitionTo(OrderState next) {
    if (!canTransition(state, next)) {
        throw InvalidTransition("pedido " + id + ": transicion invalida de " +
                                toString(state) + " a " + toString(next));
    }
    state = next;
}

void Order::assignTo(const std::string& courier) {
    transitionTo(OrderState::Assigned);
    courierId = courier;
}

void Order::reject(RejectReason reason) {
    transitionTo(OrderState::Rejected);
    rejectReason = reason;
}

void Order::markDelivered(std::int64_t atMs) {
    transitionTo(OrderState::Delivered);
    deliveredAtMs = atMs;
}
