#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
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

    std::optional<Order> take(const std::string& restaurantId);

    void putBack(Order order);

    void close();
    bool closed() const;

    std::vector<Order> drain();

    std::size_t pending() const;
    std::size_t pendingAt(const std::string& restaurantId) const;
    std::size_t maxPending() const { return maxPending_; }

private:
    struct RestaurantQueue {
        std::deque<Order> orders;
        std::condition_variable available;
    };

    RestaurantQueue& queueOf(const std::string& restaurantId);

    const std::size_t maxPending_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<RestaurantQueue>> queues_;
    std::vector<std::string> restaurantOrder_;
    std::size_t pending_ = 0;
    bool closed_ = false;
};
