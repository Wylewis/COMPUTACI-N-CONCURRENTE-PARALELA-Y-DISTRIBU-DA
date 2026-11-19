#include "order_book.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

#include "scan.h"

namespace {

std::int64_t idNumber(const std::string& id) {
    std::size_t i = 0;
    while (i < id.size() && !std::isdigit(static_cast<unsigned char>(id[i]))) ++i;
    if (i == id.size()) return -1;
    std::int64_t n = 0;
    for (; i < id.size() && std::isdigit(static_cast<unsigned char>(id[i])); ++i) n = n * 10 + (id[i] - '0');
    return n;
}

}

OrderBook::OrderBook(const Config& cfg)
    : maxPending_(static_cast<std::size_t>(std::max(0, cfg.orders.maxPending))),
      agingMs_(std::max<std::int64_t>(0, cfg.orders.agingMs)),
      acceptTimeoutMs_(std::max<std::int64_t>(0, cfg.dispatch.acceptTimeoutMs)) {
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

const OrderBook::RestaurantQueue& OrderBook::queueOf(const std::string& restaurantId) const {
    auto it = queues_.find(restaurantId);
    if (it == queues_.end()) {
        throw std::out_of_range("restaurante desconocido en el libro de pedidos: " + restaurantId);
    }
    return *it->second;
}

int OrderBook::priorityLevel(const Order& order, std::int64_t nowMs) const {
    if (!order.urgent && agingMs_ > 0 && nowMs - order.createdAtMs >= agingMs_) return 2;
    return order.urgent ? 1 : 0;
}

bool OrderBook::olderFirst(const Order& a, const Order& b) {
    if (a.createdAtMs != b.createdAtMs) return a.createdAtMs < b.createdAtMs;
    const std::int64_t na = idNumber(a.id), nb = idNumber(b.id);
    if (na >= 0 && nb >= 0 && na != nb) return na < nb;
    return a.id < b.id;
}

bool OrderBook::expired(const Order& order, std::int64_t nowMs) const {
    return order.courierId.empty() && nowMs >= order.createdAtMs + acceptTimeoutMs_;
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
    if (!notify.empty() && onSubmitted_) onSubmitted_();
    return outcome;
}

std::optional<Order> OrderBook::take(const std::string& restaurantId, std::int64_t nowMs) {
    std::unique_lock<std::mutex> lock(mutex_);
    RestaurantQueue& q = queueOf(restaurantId);
    q.available.wait(lock, [&] { return closed_ || !q.orders.empty(); });
    if (q.orders.empty()) return std::nullopt;
    auto best = q.orders.begin();
    int bestLevel = priorityLevel(*best, nowMs);
    for (auto it = std::next(best); it != q.orders.end(); ++it) {
        const int level = priorityLevel(*it, nowMs);
        if (level > bestLevel || (level == bestLevel && olderFirst(*it, *best))) {
            best = it;
            bestLevel = level;
        }
    }
    Order o = std::move(*best);
    q.orders.erase(best);
    --pending_;
    return o;
}

std::vector<Order> OrderBook::takeExpired(const std::string& restaurantId, std::int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    RestaurantQueue& q = queueOf(restaurantId);
    std::vector<Order> out;
    for (auto it = q.orders.begin(); it != q.orders.end();) {
        if (expired(*it, nowMs)) {
            out.push_back(std::move(*it));
            it = q.orders.erase(it);
            --pending_;
        } else {
            ++it;
        }
    }
    return out;
}

std::optional<std::int64_t> OrderBook::nextWakeMs(const std::string& restaurantId, std::int64_t nowMs) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<std::int64_t> wake;
    auto consider = [&](std::int64_t t) {
        if (!wake || t < *wake) wake = t;
    };
    for (const Order& o : queueOf(restaurantId).orders) {
        if (o.courierId.empty()) consider(o.createdAtMs + acceptTimeoutMs_);
        if (!o.urgent && agingMs_ > 0 && priorityLevel(o, nowMs) < 2) consider(o.createdAtMs + agingMs_);
    }
    return wake;
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
    return queueOf(restaurantId).orders.size();
}
