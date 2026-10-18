#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "config.h"
#include "event_logger.h"
#include "fleet.h"
#include "graph.h"
#include "order_book.h"
#include "sim_clock.h"
#include "stop_signal.h"

// El despachador de un restaurante: toma pedidos de su cola del libro, copia
// la flota, elige con selectCourier y asigna. Si nadie puede, duerme hasta que
// la flota cambie; si la reserva falla porque otro despachador gano esa moto,
// reintenta con copias frescas. Si vence acceptTimeoutMs desde la creacion
// sin moto, rechaza con noCourier. Al parar devuelve el pedido al libro.
class DispatcherThread {
public:
    DispatcherThread(const RestaurantConfig& restaurant, OrderBook& book, Fleet& fleet, const Graph& graph,
                     double speedKmh, std::int64_t acceptTimeoutMs, EventLogger& logger, const SimClock& clock,
                     StopSignal& stop);
    ~DispatcherThread();
    DispatcherThread(const DispatcherThread&) = delete;
    DispatcherThread& operator=(const DispatcherThread&) = delete;

    void start();
    void join();  // despues de stop.request() y book.close()

    std::int64_t assigned() const { return assigned_.load(std::memory_order_acquire); }
    std::int64_t retries() const { return retries_.load(std::memory_order_acquire); }
    std::int64_t waits() const { return waits_.load(std::memory_order_acquire); }
    std::int64_t rejected() const { return rejected_.load(std::memory_order_acquire); }

private:
    void run();
    enum class Outcome { Assigned, Rejected, Stopped };
    Outcome dispatch(Order& order);  // Stopped deja el pedido intacto en `order`

    const RestaurantConfig restaurant_;
    OrderBook& book_;
    Fleet& fleet_;
    const Graph& graph_;
    const double speedKmh_;
    const std::int64_t acceptTimeoutMs_;
    EventLogger& logger_;
    const SimClock& clock_;
    StopSignal& stop_;
    std::thread thread_;
    std::atomic<std::int64_t> assigned_{0}, retries_{0}, waits_{0}, rejected_{0};
};
