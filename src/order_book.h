#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "order.h"
#include "order_generator.h"

struct Placement {
    std::string orderId;
    std::string restaurantId;
    std::size_t position = 0;
};

struct SubmitOutcome {
    std::vector<Placement> accepted;
    std::vector<Order> rejected;
};

class OrderBook {
public:
    explicit OrderBook(const Config& cfg);

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    SubmitOutcome submit(OrderBatch&& batch);
    void onSubmitted(std::function<void()> f) { onSubmitted_ = std::move(f); }

    std::optional<Order> take(const std::string& restaurantId, std::int64_t nowMs);
    std::vector<Order> takeExpired(const std::string& restaurantId, std::int64_t nowMs);
    std::optional<std::int64_t> nextWakeMs(const std::string& restaurantId, std::int64_t nowMs) const;

    void putBack(Order order);

    void close();
    bool closed() const;

    std::vector<Order> drain();

    std::size_t pending() const;
    std::size_t pendingAt(const std::string& restaurantId) const;
    std::size_t maxPending() const { return maxPending_; }

    int priorityLevel(const Order& order, std::int64_t nowMs) const;
    static bool olderFirst(const Order& a, const Order& b);

private:
    struct RestaurantQueue {
        std::deque<Order> orders;
        std::condition_variable available;
    };

    RestaurantQueue& queueOf(const std::string& restaurantId);
    const RestaurantQueue& queueOf(const std::string& restaurantId) const;
    bool expired(const Order& order, std::int64_t nowMs) const;

    const std::size_t maxPending_;
    const std::int64_t agingMs_;
    const std::int64_t acceptTimeoutMs_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<RestaurantQueue>> queues_;
    std::vector<std::string> restaurantOrder_;
    std::size_t pending_ = 0;
    bool closed_ = false;
    std::function<void()> onSubmitted_;
};
