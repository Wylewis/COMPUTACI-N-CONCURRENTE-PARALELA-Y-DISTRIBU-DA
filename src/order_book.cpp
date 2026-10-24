#include "order_book.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "scan.h"

OrderBook::OrderBook(const Config& cfg) : maxPending_(static_cast<std::size_t>(std::max(0, cfg.orders.maxPending))) {
    for (const auto& r : cfg.restaurants) {
        queues_[r.id] = std::make_unique<RestaurantQueue>();
        restaurantOrder_.push_back(r.id);
    }
}

OrderBook::RestaurantQueue& OrderBook::queueOf(const std::string& restaurantId) {
    auto it = queues_.find(restaurantId);
    if (it == queues_.end()) {
        throw std::out_of_range("restaurante desconocido en el libro de pedidos: " + restaurantId);
    }
    return *it->second;
}

SubmitOutcome OrderBook::submit(OrderBatch&& batch) {
    SubmitOutcome outcome;
    std::vector<Order> candidates;
    for (Order& o : batch.orders) {
        if (o.state == OrderState::Rejected) {
            outcome.rejected.push_back(std::move(o));
        } else {
            candidates.push_back(std::move(o));
        }
    }
    if (candidates.empty()) return outcome;

    std::vector<RestaurantQueue*> notify;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<Order> accepted;
        for (Order& o : candidates) {
            if (closed_ || pending_ + accepted.size() >= maxPending_) {
                o.reject(RejectReason::QueueFull);
                outcome.rejected.push_back(std::move(o));
            } else {
                accepted.push_back(std::move(o));
            }
        }

        std::vector<std::size_t> position(accepted.size(), 0);
        for (const std::string& rid : restaurantOrder_) {
            std::vector<std::size_t> flags(accepted.size(), 0);
            bool any = false;
            for (std::size_t i = 0; i < accepted.size(); ++i) {
                if (accepted[i].restaurantId == rid) {
                    flags[i] = 1;
                    any = true;
                }
            }
            if (!any) continue;
            const std::vector<std::size_t> offset = exclusiveScan(flags);
            const std::size_t already = queueOf(rid).orders.size();
            for (std::size_t i = 0; i < accepted.size(); ++i) {
                if (flags[i]) position[i] = already + offset[i];
            }
        }

        for (std::size_t i = 0; i < accepted.size(); ++i) {
            RestaurantQueue& q = queueOf(accepted[i].restaurantId);
            outcome.accepted.push_back(Placement{accepted[i].id, accepted[i].restaurantId, position[i]});
            q.orders.push_back(std::move(accepted[i]));
            ++pending_;
            notify.push_back(&q);
        }
    }

    for (RestaurantQueue* q : notify) q->available.notify_one();
    return outcome;
}

std::optional<Order> OrderBook::take(const std::string& restaurantId) {
    std::unique_lock<std::mutex> lock(mutex_);
    RestaurantQueue& q = queueOf(restaurantId);
    q.available.wait(lock, [&] { return closed_ || !q.orders.empty(); });
    if (q.orders.empty()) return std::nullopt;
    Order o = std::move(q.orders.front());
    q.orders.pop_front();
    --pending_;
    return o;
}

void OrderBook::putBack(Order order) {
    RestaurantQueue* q = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        q = &queueOf(order.restaurantId);
        q->orders.push_front(std::move(order));
        ++pending_;
    }
    q->available.notify_one();
}

void OrderBook::close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return;
        closed_ = true;
    }
    for (const std::string& rid : restaurantOrder_) queueOf(rid).available.notify_all();
}

bool OrderBook::closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
}

std::vector<Order> OrderBook::drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Order> out;
    for (const std::string& rid : restaurantOrder_) {
        auto& orders = queueOf(rid).orders;
        while (!orders.empty()) {
            out.push_back(std::move(orders.front()));
            orders.pop_front();
        }
    }
    pending_ = 0;
    return out;
}

std::size_t OrderBook::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_;
}

std::size_t OrderBook::pendingAt(const std::string& restaurantId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(restaurantId);
    if (it == queues_.end()) {
        throw std::out_of_range("restaurante desconocido en el libro de pedidos: " + restaurantId);
    }
    return it->second->orders.size();
}
