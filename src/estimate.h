#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#include "config.h"
#include "graph.h"
#include "order.h"

// Tiempos simulados fijos del modelo: lo que tarda recoger un pedido en el
// restaurante y entregarlo en la puerta del cliente. Los usan tanto la
// estimacion como el repartidor, asi la estimacion coincide con la realidad.
constexpr std::int64_t kPickupHandoverMs = 60'000;
constexpr std::int64_t kDeliveryHandoverMs = 60'000;

// Lo que la estimacion necesita saber de un repartidor. Es una copia tomada
// bajo el cerrojo del repartidor: la tarea que estima trabaja sobre ella sin
// compartir nada con el hilo de la moto.
struct CourierSnapshot {
    std::string id;
    std::string node;               // nodo donde esta, o al que llegara al terminar lo que hace
    std::int64_t availableAtMs = 0; // instante simulado en que queda libre en ese nodo
    int bagCount = 0;               // pedidos que lleva (asignados o recogidos)
    int bagCapacity = 1;
};

// Resultado de una estimacion.
struct Estimate {
    std::string courierId;
    std::int64_t pickupAtMs = 0;    // instante en que recogeria el pedido
    std::int64_t deliveryAtMs = 0;  // instante en que lo entregaria: menor es mejor
    double toRestaurantM = 0.0;     // metros hasta el restaurante
    double toCustomerM = 0.0;       // metros del restaurante al cliente
};

// Una estimacion que no se puede calcular: mochila llena, o sin ruta hasta el
// restaurante o hasta el cliente. El despachador la recibe como error y sigue.
struct EstimateError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Estima cuando entregaria `courier` el pedido `order` si lo tomara ahora
// (`nowMs`), yendo primero al restaurante y luego al cliente, a `speedKmh`.
// Espera la preparacion si llega antes de `order.readyAtMs`. No contempla la
// fila del restaurante, que depende de los demas repartidores.
// Lanza EstimateError si no es posible. Funcion pura: no toca estado compartido.
Estimate estimateDelivery(const CourierSnapshot& courier, const Order& order,
                          const RestaurantConfig& restaurant, const Graph& graph,
                          double speedKmh, std::int64_t nowMs);
