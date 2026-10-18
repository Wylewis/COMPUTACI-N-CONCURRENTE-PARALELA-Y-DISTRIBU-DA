#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "order.h"
#include "order_generator.h"

// Donde quedo un pedido aceptado: su restaurante y su posicion en la cola de
// ese restaurante en el momento de entrar (0 = el proximo en despacharse).
struct Placement {
    std::string orderId;
    std::string restaurantId;
    std::size_t position = 0;
};

// Resultado de recibir una rafaga.
struct SubmitOutcome {
    std::vector<Placement> accepted;
    std::vector<Order> rejected;  // con su motivo: unreachable (venian asi) o queueFull
};

// El libro de pedidos: el unico lugar donde esperan los pedidos aun no
// asignados. Es un monitor con un solo mutex, porque el limite maxPending es
// global: comprobar si hay lugar e insertar deben ser un unico paso. Dentro
// hay una cola por restaurante, cada una con su variable de condicion, asi el
// despachador de un restaurante duerme hasta que llegue un pedido suyo y no
// se despierta por los de otros.
//
// Cada cola se atiende por antiguedad: los pedidos entran en orden de
// creacion y un pedido devuelto (reserva liberada por averia) vuelve al
// frente, porque es el mas antiguo.
class OrderBook {
public:
    explicit OrderBook(const Config& cfg);

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    // Recibe una rafaga. Los pedidos que ya vienen rechazados (unreachable)
    // pasan al vector de rechazados sin contar. Los demas entran mientras el
    // total en espera sea menor que maxPending; el resto se rechaza con
    // queueFull. Las posiciones en cola se calculan con un scan exclusivo
    // sobre la rafaga. Todo bajo el cerrojo, en un solo paso.
    SubmitOutcome submit(OrderBatch&& batch);

    // Saca el proximo pedido de la cola de `restaurantId`; duerme hasta que
    // haya uno o el libro se cierre. std::nullopt si cerrado y vacio.
    // Lanza std::out_of_range si el restaurante no existe.
    std::optional<Order> take(const std::string& restaurantId);

    // Devuelve al frente de su cola un pedido asignado cuya reserva se libero.
    void putBack(Order order);

    // Cierra el libro: los take pendientes despiertan y devuelven lo que haya.
    void close();
    bool closed() const;

    // Saca todo lo que queda en espera (para reportarlo como pending al apagar).
    std::vector<Order> drain();

    std::size_t pending() const;
    std::size_t pendingAt(const std::string& restaurantId) const;
    std::size_t maxPending() const { return maxPending_; }

private:
    struct RestaurantQueue {
        std::deque<Order> orders;
        std::condition_variable available;
    };

    RestaurantQueue& queueOf(const std::string& restaurantId);

    const std::size_t maxPending_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<RestaurantQueue>> queues_;
    std::vector<std::string> restaurantOrder_;  // ids en el orden del config
    std::size_t pending_ = 0;
    bool closed_ = false;
};
