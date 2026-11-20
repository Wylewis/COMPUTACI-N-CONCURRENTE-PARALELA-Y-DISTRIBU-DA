#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "event_log.h"

class EventLogger {
public:
    EventLogger(const std::string& path, const std::vector<std::string>& courierIds = {});
    ~EventLogger();

    EventLogger(const EventLogger&) = delete;
    EventLogger& operator=(const EventLogger&) = delete;

    bool publish(Event event);

    void close();

    bool closed() const { return closedFlag_.load(std::memory_order_acquire); }

    std::int64_t published() const { return published_.load(std::memory_order_acquire); }
    std::int64_t written() const { return written_.load(std::memory_order_acquire); }

    Summary summary() const;
    std::vector<int> deliveryTimesMs() const;
    std::vector<std::int64_t> createdTimesMs() const;
    std::vector<std::int64_t> closedTimesMs() const;
    std::vector<int> createdRestaurants() const;
    std::vector<std::string> restaurantNames() const;

private:
    void writerLoop();

    EventLog log_;
    std::deque<Event> queue_;
    std::mutex mutex_;
    std::condition_variable available_;
    bool closed_ = false;
    std::atomic<bool> closedFlag_{false};
    std::atomic<std::int64_t> published_{0};
    std::atomic<std::int64_t> written_{0};
    std::thread writer_;
};
