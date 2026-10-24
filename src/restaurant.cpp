#include "restaurant.h"

#include <algorithm>

#include "scan.h"

Restaurant::Restaurant(const RestaurantConfig& cfg) : id_(cfg.id), node_(cfg.node), slots_(std::max(1, cfg.pickupSlots)) {}

std::size_t Restaurant::enterQueue(const std::string& courierId) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(courierId);
    return queue_.size() - 1;
}

bool Restaurant::waitForSlot(const std::string& courierId, const StopSignal& stop) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&] { return stop.requested() || (!queue_.empty() && queue_.front() == courierId && busy_ < slots_); });
    const auto it = std::find(queue_.begin(), queue_.end(), courierId);
    if (it != queue_.end()) queue_.erase(it);
    if (stop.requested()) {
        lock.unlock();
        cv_.notify_all();
        return false;
    }
    ++busy_;
    ++served_;
    lock.unlock();
    cv_.notify_all();
    return true;
}

void Restaurant::leaveSlot() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        --busy_;
    }
    cv_.notify_all();
}

void Restaurant::wake() {
    { std::lock_guard<std::mutex> lock(mutex_); }
    cv_.notify_all();
}

QueueSnapshot Restaurant::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {std::vector<std::string>(queue_.begin(), queue_.end()), busy_, slots_};
}

std::int64_t Restaurant::served() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return served_;
}

std::vector<std::int64_t> expectedStartMs(const std::vector<std::int64_t>& serviceMs, int slots) {
    std::vector<std::int64_t> starts = exclusiveScan(serviceMs);
    for (std::int64_t& s : starts) s /= std::max(1, slots);
    return starts;
}
