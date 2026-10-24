#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "event_logger.h"
#include "order_book.h"
#include "order_generator.h"
#include "sim_clock.h"
#include "stop_signal.h"

class GeneratorThread {
public:
    GeneratorThread(OrderGenerator& generator, OrderBook& book, EventLogger& logger,
                    const SimClock& clock, StopSignal& stop);
    ~GeneratorThread();

    GeneratorThread(const GeneratorThread&) = delete;
    GeneratorThread& operator=(const GeneratorThread&) = delete;

    void start();
    void join();

    std::int64_t batches() const { return batches_.load(std::memory_order_acquire); }
    std::int64_t created() const { return created_.load(std::memory_order_acquire); }
    std::int64_t rejected() const { return rejected_.load(std::memory_order_acquire); }

private:
    void run();

    OrderGenerator& generator_;
    OrderBook& book_;
    EventLogger& logger_;
    const SimClock& clock_;
    StopSignal& stop_;
    std::thread thread_;
    std::atomic<std::int64_t> batches_{0};
    std::atomic<std::int64_t> created_{0};
    std::atomic<std::int64_t> rejected_{0};
};
