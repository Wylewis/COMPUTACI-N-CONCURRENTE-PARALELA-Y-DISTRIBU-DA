#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
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
#include "live_counters.h"
#include "restaurants.h"
#include "sim_clock.h"
#include "stop_signal.h"

class Fleet {
public:
    Fleet(const FleetConfig& cfg, const Graph& graph, Restaurants& restaurants, EventLogger& logger,
          const SimClock& clock, StopSignal& stop);
    Fleet(const Fleet&) = delete;
    Fleet& operator=(const Fleet&) = delete;

    static std::vector<std::string> courierIds(int couriers);

    void start();
    void join();
    void wake();

    std::size_t size() const { return couriers_.size(); }
    const Courier& courier(std::size_t i) const { return *couriers_[i]; }
    std::vector<CourierSnapshot> snapshots(std::int64_t nowMs) const;
    std::vector<CourierView> views() const;
    bool assign(const std::string& courierId, Order& order);

    void enableBreakdowns(const IncidentsConfig& incidents, std::uint64_t seed, std::function<void(Order)> release);
    std::int64_t breakdowns() const;

    void rescue(const std::string& brokenId);
    std::int64_t rescues() const;
    std::int64_t delivered() const;
    const LiveCounters& counters() const { return counters_; }

    std::uint64_t version() const;
    void waitForChange(std::uint64_t seen);

    bool waitForChange(std::uint64_t seen, std::chrono::steady_clock::time_point deadline);

private:
    void freed();

    LiveCounters counters_;
    std::vector<std::unique_ptr<Courier>> couriers_;
    std::vector<std::unique_ptr<CourierThread>> threads_;
    std::unordered_map<std::string, std::size_t> index_;
    const Graph& graph_;
    Restaurants& restaurants_;
    StopSignal& stop_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::uint64_t version_ = 0;
    std::deque<std::string> pendingRescues_;
};
