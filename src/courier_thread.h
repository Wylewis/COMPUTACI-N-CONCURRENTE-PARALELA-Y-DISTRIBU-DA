#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "breakdown.h"
#include "config.h"
#include "courier.h"
#include "event_logger.h"
#include "graph.h"
#include "restaurants.h"
#include "sim_clock.h"
#include "stop_signal.h"

// El hilo de una moto: duerme en su cv sin pedidos; con pedidos va al
// restaurante, espera la preparacion fuera de la fila, hace fila por un puesto,
// recoge, y entrega el destino mas cercano primero, tramo a tramo. Puede
// averiarse en un tramo: devuelve los pedidos no recogidos, se repara y sigue.
class CourierThread {
public:
    CourierThread(Courier& courier, const Graph& graph, Restaurants& restaurants, double speedKmh,
                  EventLogger& logger, const SimClock& clock, StopSignal& stop);
    ~CourierThread();
    CourierThread(const CourierThread&) = delete;
    CourierThread& operator=(const CourierThread&) = delete;

    void start();
    void join();  // despues de stop.request()

    void onFreed(std::function<void()> f) { onFreed_ = std::move(f); }  // antes de start(): aviso por entrega
    // Antes de start(): sorteo de averias de esta moto, y a quien devolver los pedidos no recogidos.
    void enableBreakdowns(std::uint64_t seed, std::size_t index, double probability) { roll_.emplace(seed, index, probability); }
    void onReleased(std::function<void(Order)> f) { onReleased_ = std::move(f); }
    std::int64_t breakdowns() const { return breakdowns_.load(std::memory_order_acquire); }
    bool assign(Order& order);  // reserva + orderAssigned; false y pedido intacto si no cabe
    std::int64_t delivered() const { return delivered_.load(std::memory_order_acquire); }

private:
    void run();
    bool serveBag();  // false si se pidio parar
    std::vector<BagItem> items();
    enum class Travel { Arrived, Stopped, NoRoute, Interrupted };  // Interrupted: averia atendida, replanificar
    Travel travelTo(const std::string& node, CourierState moving);
    bool breakDownAt(const std::string& node);  // false si la parada llega durante la reparacion
    bool pickUpAt(const std::string& restaurantId, const std::vector<BagItem>& bag);  // espera, fila, puesto, recogida
    bool deliver(const std::string& orderId);
    bool waitSim(std::int64_t simMs);  // false si se pidio parar

    Courier& courier_;
    const Graph& graph_;
    Restaurants& restaurants_;
    const double speedKmh_;
    EventLogger& logger_;
    const SimClock& clock_;
    StopSignal& stop_;
    std::mutex mutex_;  // protege el par (reserva, orderAssigned) y la espera del hilo
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<std::int64_t> delivered_{0};
    std::function<void()> onFreed_;
    std::function<void(Order)> onReleased_;
    std::optional<BreakdownRoll> roll_;
    std::atomic<std::int64_t> breakdowns_{0};
};
