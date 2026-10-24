#include "fleet.h"

#include <algorithm>

#include <optional>

#include "dispatch_select.h"

Fleet::Fleet(const FleetConfig& cfg, const Graph& graph, Restaurants& restaurants, EventLogger& logger,
             const SimClock& clock, StopSignal& stop)
    : counters_(static_cast<std::size_t>(std::max(0, cfg.couriers))), graph_(graph), restaurants_(restaurants), stop_(stop) {
    for (const std::string& id : courierIds(cfg.couriers)) {
        index_[id] = couriers_.size();
        couriers_.push_back(std::make_unique<Courier>(id, cfg.startNode, cfg.bagCapacity));
        threads_.push_back(std::make_unique<CourierThread>(*couriers_.back(), graph, restaurants, cfg.speedKmh, logger,
                                                           clock, stop));
        threads_.back()->counters(&counters_.at(couriers_.size() - 1));
        threads_.back()->onFreed([this] { freed(); });
        threads_.back()->onRescueNeeded([this](const std::string& id) { rescue(id); });
    }
}

std::vector<std::string> Fleet::courierIds(int couriers) {
    std::vector<std::string> ids;
    for (int i = 0; i < couriers; ++i) ids.push_back("c" + std::to_string(i));
    return ids;
}

void Fleet::start() {
    for (auto& t : threads_) t->start();
}

void Fleet::join() {
    wake();
    restaurants_.wake();
    for (auto& t : threads_) t->join();
}

void Fleet::wake() {
    { std::lock_guard<std::mutex> lock(mutex_); }
    cv_.notify_all();
}

std::vector<CourierSnapshot> Fleet::snapshots(std::int64_t nowMs) const {
    std::vector<CourierSnapshot> out;
    for (const auto& c : couriers_) out.push_back(c->estimateSnapshot(nowMs));
    return out;
}

std::vector<CourierView> Fleet::views() const {
    std::vector<CourierView> out;
    for (const auto& c : couriers_) out.push_back(c->view());
    return out;
}

bool Fleet::assign(const std::string& courierId, Order& order) {
    const auto it = index_.find(courierId);
    return it != index_.end() && threads_[it->second]->assign(order);
}

void Fleet::enableBreakdowns(const IncidentsConfig& incidents, std::uint64_t seed, std::function<void(Order)> release) {
    for (std::size_t i = 0; i < threads_.size(); ++i) {
        threads_[i]->enableBreakdowns(seed, i, incidents.breakdownProbability);
        threads_[i]->onReleased(release);
    }
}

std::int64_t Fleet::breakdowns() const {
    std::int64_t n = 0;
    for (const auto& t : threads_) n += t->breakdowns();
    return n;
}

std::int64_t Fleet::delivered() const {
    std::int64_t n = 0;
    for (const auto& t : threads_) n += t->delivered();
    return n;
}

std::uint64_t Fleet::version() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return version_;
}

void Fleet::notifyChange() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++version_;
    }
    cv_.notify_all();
}

void Fleet::waitForChange(std::uint64_t seen) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&] { return version_ != seen || stop_.requested(); });
}

bool Fleet::waitForChange(std::uint64_t seen, std::chrono::steady_clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_until(lock, deadline, [&] { return version_ != seen || stop_.requested(); });
    return version_ != seen;
}

void Fleet::rescue(const std::string& brokenId) {
    const Courier& broken = *couriers_[index_.at(brokenId)];
    std::optional<std::size_t> best;
    double bestM = 0.0;
    for (std::size_t i = 0; i < couriers_.size(); ++i) {
        const CourierSnapshot s = couriers_[i]->estimateSnapshot(0);
        if (s.id == brokenId || s.broken || s.bagCount >= s.bagCapacity) continue;
        const double d = straightLineM(graph_, s.node, broken.node());
        if (!best || d < bestM) best = i, bestM = d;
    }
    if (best) {
        threads_[*best]->rescue(*couriers_[index_.at(brokenId)]);
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    pendingRescues_.push_back(brokenId);
}

std::int64_t Fleet::rescues() const {
    std::int64_t n = 0;
    for (const auto& t : threads_) n += t->rescues();
    return n;
}

void Fleet::freed() {
    std::deque<std::string> retry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++version_;
        retry.swap(pendingRescues_);
    }
    cv_.notify_all();
    for (const std::string& id : retry)
        if (couriers_[index_.at(id)]->state() == CourierState::BrokenDown) rescue(id);
}
