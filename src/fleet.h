#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "courier.h"
#include "courier_thread.h"
#include "event_logger.h"
#include "graph.h"
#include "sim_clock.h"
#include "stop_signal.h"

// Las motos y sus hilos. Da copias de toda la flota (snapshots, views), asigna
// por id, y lleva una version que cada entrega incrementa: un despachador que
// no encontro moto duerme en waitForChange hasta que la flota cambie.
class Fleet {
public:
    Fleet(const FleetConfig& cfg, const Graph& graph, const std::vector<RestaurantConfig>& restaurants,
          EventLogger& logger, const SimClock& clock, StopSignal& stop);
    Fleet(const Fleet&) = delete;
    Fleet& operator=(const Fleet&) = delete;

    static std::vector<std::string> courierIds(int couriers);  // c0..c(n-1)

    void start();
    void join();  // despues de stop.request()
    void wake();  // despierta a los despachadores dormidos en waitForChange

    std::size_t size() const { return couriers_.size(); }
    const Courier& courier(std::size_t i) const { return *couriers_[i]; }
    std::vector<CourierSnapshot> snapshots(std::int64_t nowMs) const;
    std::vector<CourierView> views() const;
    bool assign(const std::string& courierId, Order& order);
    std::int64_t delivered() const;

    std::uint64_t version() const;
    void waitForChange(std::uint64_t seen);  // hasta version != seen o parada

private:
    void freed();

    std::vector<std::unique_ptr<Courier>> couriers_;
    std::vector<std::unique_ptr<CourierThread>> threads_;
    std::unordered_map<std::string, std::size_t> index_;
    StopSignal& stop_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::uint64_t version_ = 0;
};
