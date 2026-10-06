#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

// Estados de un pedido (seccion 4 del enunciado). El camino normal es
// Created -> Assigned -> PickedUp -> Delivered, nunca hacia atras. Rejected y
// Pending son los otros dos finales posibles.
enum class OrderState { Created, Assigned, PickedUp, Delivered, Rejected, Pending };

// Motivos de rechazo, con los nombres exactos que exige el log (seccion 6.2).
enum class RejectReason { QueueFull, Unreachable, NoCourier };

// Nombres de evento del log (seccion 6.2).
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

// Delivered, Rejected y Pending: el pedido ya no cambia.
bool isFinal(OrderState state);

// Transiciones permitidas:
//   Created  -> Assigned | Rejected | Pending
//   Assigned -> PickedUp | Pending
//   PickedUp -> Delivered | Pending
// Desde un estado final no se sale. Rejected solo es posible antes de asignar.
bool canTransition(OrderState from, OrderState to);

// Intento de mover un pedido hacia atras o fuera de un estado final.
struct InvalidTransition : std::logic_error {
    using std::logic_error::logic_error;
};

// Un pedido. Es solo movible: existe en un unico lugar del sistema a la vez
// (el libro de pedidos, una mochila, un traspaso), nunca en dos.
struct Order {
    std::string id;
    std::string restaurantId;
    std::string deliveryNode;      // punto de entrega: un nodo de la red de calles
    std::int64_t createdAtMs = 0;  // tiempo simulado de creacion
    std::int64_t readyAtMs = 0;    // tiempo simulado en que termina la preparacion

    OrderState state = OrderState::Created;
    std::string courierId;                      // repartidor actual, si esta asignado
    std::optional<RejectReason> rejectReason;   // solo si state == Rejected
    std::int64_t deliveredAtMs = 0;             // solo si state == Delivered

    Order() = default;
    Order(const Order&) = delete;
    Order& operator=(const Order&) = delete;
    Order(Order&&) = default;
    Order& operator=(Order&&) = default;

    // Cambia de estado respetando canTransition; lanza InvalidTransition si no.
    void transitionTo(OrderState next);

    // Atajos que ademas registran el dato que acompana al estado.
    void assignTo(const std::string& courier);
    void reject(RejectReason reason);
    void markDelivered(std::int64_t atMs);
};
