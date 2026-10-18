#include "courier.h"

#include <algorithm>
#include <utility>

const char* toString(CourierState state) {
    static const char* const names[] = {"idle", "toRestaurant", "queued", "pickingUp", "delivering", "brokenDown"};
    return names[static_cast<int>(state)];
}

Courier::Courier(std::string id, std::string startNode, int bagCapacity)
    : id_(std::move(id)), bagCapacity_(std::max(1, bagCapacity)), node_(std::move(startNode)) {}

int Courier::indexOf(const std::string& orderId) const {
    for (std::size_t i = 0; i < bag_.size(); ++i)
        if (bag_[i].id == orderId) return static_cast<int>(i);
    return -1;
}

bool Courier::reserve(Order& order) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == CourierState::BrokenDown || static_cast<int>(bag_.size()) >= bagCapacity_) return false;
    bag_.push_back(std::move(order));
    return true;
}

bool Courier::hasOrder(const std::string& orderId) const { std::lock_guard<std::mutex> lock(mutex_); return indexOf(orderId) >= 0; }

std::optional<Order> Courier::removeOrder(const std::string& orderId) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int i = indexOf(orderId);
    if (i < 0) return std::nullopt;
    Order out = std::move(bag_[i]);
    bag_.erase(bag_.begin() + i);
    return out;
}

std::vector<Order> Courier::takeBag() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Order> out = std::move(bag_);
    bag_.clear();
    return out;
}

int Courier::bagCount() const { std::lock_guard<std::mutex> lock(mutex_); return static_cast<int>(bag_.size()); }

bool Courier::markPickedUp(const std::string& orderId) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int i = indexOf(orderId);
    if (i < 0) return false;
    bag_[i].transitionTo(OrderState::PickedUp);
    return true;
}

void Courier::startTrip(std::shared_ptr<const RouteMotion> trip, CourierState newState) {
    std::lock_guard<std::mutex> lock(mutex_);
    trip_ = std::move(trip);
    state_ = newState;
}

void Courier::endTrip(const std::string& atNode, CourierState newState) {
    std::lock_guard<std::mutex> lock(mutex_);
    trip_.reset();
    node_ = atNode;
    state_ = newState;
}

void Courier::setState(CourierState state) { std::lock_guard<std::mutex> lock(mutex_); state_ = state; }
CourierState Courier::state() const { std::lock_guard<std::mutex> lock(mutex_); return state_; }
std::string Courier::node() const { std::lock_guard<std::mutex> lock(mutex_); return node_; }

CourierSnapshot Courier::estimateSnapshot(std::int64_t nowMs) const {
    std::lock_guard<std::mutex> lock(mutex_);
    CourierSnapshot s;
    s.id = id_;
    s.bagCount = static_cast<int>(bag_.size());
    s.bagCapacity = bagCapacity_;
    const bool moving = trip_ && !trip_->legs().empty();
    s.node = moving ? trip_->legs().back().to : node_;
    s.availableAtMs = moving ? std::max(nowMs, trip_->arrivalMs()) : nowMs;
    return s;
}

CourierView Courier::view() const {
    std::lock_guard<std::mutex> lock(mutex_);
    CourierView v{id_, state_, node_, trip_, static_cast<int>(bag_.size()), bagCapacity_, {}};
    for (const Order& o : bag_) v.deliveryNodes.push_back(o.deliveryNode);
    return v;
}

std::optional<std::string> Courier::nearestDelivery(const Graph& graph) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<std::string> best;
    double bestM = 0.0;
    for (const Order& o : bag_) {
        if (o.state != OrderState::PickedUp) continue;
        const auto route = graph.shortestPath(node_, o.deliveryNode);
        if (route && (!best || route->lengthM < bestM)) {
            best = o.deliveryNode;
            bestM = route->lengthM;
        }
    }
    return best;
}
