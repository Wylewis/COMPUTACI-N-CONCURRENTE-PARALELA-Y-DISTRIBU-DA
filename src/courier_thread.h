#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
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
#include "live_counters.h"
#include "restaurants.h"
#include "sim_clock.h"
#include "stop_signal.h"

class CourierThread {
public:
    CourierThread(Courier& courier, const Graph& graph, Restaurants& restaurants, double speedKmh,
                  EventLogger& logger, const SimClock& clock, StopSignal& stop);
    ~CourierThread();
    CourierThread(const CourierThread&) = delete;
    CourierThread& operator=(const CourierThread&) = delete;

    void start();
    void join();

    void onFreed(std::function<void()> f) { onFreed_ = std::move(f); }

    void enableBreakdowns(std::uint64_t seed, std::size_t index, double probability) { roll_.emplace(seed, index, probability); }
    void onReleased(std::function<void(Order)> f) { onReleased_ = std::move(f); }
    void counters(CourierCounters* c) { counters_ = c; }

    void onRescueNeeded(std::function<void(const std::string&)> f) { onRescueNeeded_ = std::move(f); }

    void rescue(Courier& broken);
    std::int64_t rescues() const { return rescues_.load(std::memory_order_acquire); }
    std::int64_t breakdowns() const { return breakdowns_.load(std::memory_order_acquire); }
    bool assign(Order& order);
    std::int64_t delivered() const { return delivered_.load(std::memory_order_acquire); }

private:
    void run();
    bool serveBag();
    std::vector<BagItem> items();
    enum class Travel { Arrived, Stopped, NoRoute, Interrupted };
    Travel travelTo(const std::string& node, CourierState moving);
    bool breakDownAt(const std::string& node);
    bool serveRescue(Courier& broken);
    bool pickUpAt(const std::string& restaurantId, const std::vector<BagItem>& bag);
    bool deliver(const std::string& orderId);
    bool waitSim(std::int64_t simMs);

    Courier& courier_;
    const Graph& graph_;
    Restaurants& restaurants_;
    const double speedKmh_;
    EventLogger& logger_;
    const SimClock& clock_;
    StopSignal& stop_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<std::int64_t> delivered_{0};
    std::function<void()> onFreed_;
    std::function<void(Order)> onReleased_;
    CourierCounters* counters_ = nullptr;
    std::function<void(const std::string&)> onRescueNeeded_;
    std::deque<Courier*> rescues_queue_;
    std::atomic<std::int64_t> rescues_{0};
    std::optional<BreakdownRoll> roll_;
    std::atomic<std::int64_t> breakdowns_{0};
};
