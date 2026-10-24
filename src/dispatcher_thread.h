#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "config.h"
#include "event_logger.h"
#include "fleet.h"
#include "graph.h"
#include "order_book.h"
#include "sim_clock.h"
#include "stop_signal.h"
#include "thread_pool.h"

class DispatcherThread {
public:
    DispatcherThread(const RestaurantConfig& restaurant, OrderBook& book, Fleet& fleet, const Graph& graph,
                     double speedKmh, const DispatchConfig& dispatch, ThreadPool& estimators, EventLogger& logger,
                     const SimClock& clock, StopSignal& stop);
    ~DispatcherThread();
    DispatcherThread(const DispatcherThread&) = delete;
    DispatcherThread& operator=(const DispatcherThread&) = delete;

    void start();
    void join();

    std::int64_t assigned() const { return assigned_.load(std::memory_order_acquire); }
    std::int64_t retries() const { return retries_.load(std::memory_order_acquire); }
    std::int64_t waits() const { return waits_.load(std::memory_order_acquire); }
    std::int64_t rejected() const { return rejected_.load(std::memory_order_acquire); }
    std::int64_t lateQuotes() const { return lateQuotes_.load(std::memory_order_acquire); }

private:
    void run();
    void rejectExpired();
    enum class Outcome { Assigned, NoCourier, Stopped };
    Outcome dispatch(Order& order);

    const RestaurantConfig restaurant_;
    OrderBook& book_;
    Fleet& fleet_;
    const Graph& graph_;
    const double speedKmh_;
    const std::chrono::milliseconds quoteTimeout_;
    const std::string estimator_;
    ThreadPool& estimators_;
    EventLogger& logger_;
    const SimClock& clock_;
    StopSignal& stop_;
    std::thread thread_;
    std::atomic<std::int64_t> assigned_{0}, retries_{0}, waits_{0}, rejected_{0}, lateQuotes_{0};
};
