#include "estimate.h"

#include <algorithm>

#include "route_motion.h"

Estimate estimateDelivery(const CourierSnapshot& courier, const Order& order,
                          const RestaurantConfig& restaurant, const Graph& graph,
                          double speedKmh, std::int64_t nowMs) {
    if (courier.bagCount >= courier.bagCapacity) {
        throw EstimateError("repartidor " + courier.id + ": mochila llena (" +
                            std::to_string(courier.bagCount) + "/" + std::to_string(courier.bagCapacity) + ")");
    }
    if (order.restaurantId != restaurant.id) {
        throw EstimateError("pedido " + order.id + ": el restaurante " + restaurant.id +
                            " no es el del pedido (" + order.restaurantId + ")");
    }

    const auto toRestaurant = graph.shortestPath(courier.node, restaurant.node);
    if (!toRestaurant) {
        throw EstimateError("repartidor " + courier.id + ": sin ruta de " + courier.node +
                            " al restaurante " + restaurant.id + " (" + restaurant.node + ")");
    }
    const auto toCustomer = graph.shortestPath(restaurant.node, order.deliveryNode);
    if (!toCustomer) {
        throw EstimateError("pedido " + order.id + ": sin ruta del restaurante " + restaurant.id +
                            " al punto de entrega " + order.deliveryNode);
    }

    const std::int64_t departMs = std::max(nowMs, courier.availableAtMs);
    const std::int64_t arriveRestaurantMs = departMs + travelTimeMs(toRestaurant->lengthM, speedKmh);
    const std::int64_t pickupAtMs = std::max(arriveRestaurantMs, order.readyAtMs);
    const std::int64_t leaveRestaurantMs = pickupAtMs + kPickupHandoverMs;
    const std::int64_t deliveryAtMs =
        leaveRestaurantMs + travelTimeMs(toCustomer->lengthM, speedKmh) + kDeliveryHandoverMs;

    Estimate e;
    e.courierId = courier.id;
    e.pickupAtMs = pickupAtMs;
    e.deliveryAtMs = deliveryAtMs;
    e.toRestaurantM = toRestaurant->lengthM;
    e.toCustomerM = toCustomer->lengthM;
    return e;
}
