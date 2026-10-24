#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "config.h"
#include "stop_signal.h"

struct QueueSnapshot {
    std::vector<std::string> waiting;
    int busy = 0;
    int slots = 1;
};

class Restaurant {
public:
    explicit Restaurant(const RestaurantConfig& cfg);
    Restaurant(const Restaurant&) = delete;
    Restaurant& operator=(const Restaurant&) = delete;

    const std::string& id() const { return id_; }
    const std::string& node() const { return node_; }
    int slots() const { return slots_; }

    std::size_t enterQueue(const std::string& courierId);

    bool waitForSlot(const std::string& courierId, const StopSignal& stop);
    void leaveSlot();
    void wake();

    QueueSnapshot snapshot() const;
    std::int64_t served() const;

private:
    const std::string id_, node_;
    const int slots_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    int busy_ = 0;
    std::int64_t served_ = 0;
};

std::vector<std::int64_t> expectedStartMs(const std::vector<std::int64_t>& serviceMs, int slots);
