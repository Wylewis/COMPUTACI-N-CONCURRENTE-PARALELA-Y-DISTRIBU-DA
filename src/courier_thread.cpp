#include "courier_thread.h"

#include <algorithm>
#include <memory>

#include "estimate.h"
#include "route_motion.h"

CourierThread::CourierThread(Courier& courier, const Graph& graph, Restaurants& restaurants, double speedKmh,
                             EventLogger& logger, const SimClock& clock, StopSignal& stop)
    : courier_(courier), graph_(graph), restaurants_(restaurants), speedKmh_(speedKmh), logger_(logger), clock_(clock),
      stop_(stop) {}

CourierThread::~CourierThread() { join(); }

void CourierThread::start() { thread_ = std::thread([this] { run(); }); }

void CourierThread::join() {
    { std::lock_guard<std::mutex> lock(mutex_); }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

bool CourierThread::assign(Order& order) {
    const std::string id = order.id;
    const std::string from = order.courierId;  // no vacio: viene de una moto averiada
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!courier_.reserve(order)) return false;
        if (from.empty() || from == courier_.id())  // nuevo, o la misma moto lo retoma tras repararse
            logger_.publish(Event::orderAssigned(clock_.nowMs(), id, courier_.id()));
        else
            logger_.publish(Event::orderReassigned(clock_.nowMs(), id, from, courier_.id()));
    }
    cv_.notify_one();
    return true;
}

std::vector<BagItem> CourierThread::items() {
    std::lock_guard<std::mutex> lock(mutex_);
    return courier_.bagItems();
}

bool CourierThread::waitSim(std::int64_t simMs) { return !stop_.waitUntil(clock_.realTimeFor(simMs)); }

void CourierThread::rescue(Courier& broken) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        rescues_queue_.push_back(&broken);
    }
    cv_.notify_one();
}

void CourierThread::run() {
    int parked = 0;  // pedidos que quedaron en la mochila al dormirse: solo un cambio lo despierta
    for (;;) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&] { return stop_.requested() || !rescues_queue_.empty() || courier_.bagCount() != parked; });
        if (stop_.requested()) return;
        Courier* broken = nullptr;
        if (!rescues_queue_.empty()) {
            broken = rescues_queue_.front();
            rescues_queue_.pop_front();
        }
        lock.unlock();
        if (broken && !serveRescue(*broken)) return;
        if (!serveBag()) return;
        parked = courier_.bagCount();
    }
}

bool CourierThread::serveRescue(Courier& broken) {
    const Travel t = travelTo(broken.node(), CourierState::ToRestaurant);  // la averiada es un punto de recogida
    if (t == Travel::Stopped) return false;
    if (t != Travel::Arrived) {
        if (onRescueNeeded_) onRescueNeeded_(broken.id());  // que lo intente otra moto
        return true;
    }
    for (const std::string& id : courier_.takeOverFrom(broken))
        logger_.publish(Event::orderReassigned(clock_.nowMs(), id, broken.id(), courier_.id()));
    rescues_.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

bool CourierThread::serveBag() {
    for (;;) {
        const std::vector<BagItem> bag = items();
        const auto first = std::find_if(bag.begin(), bag.end(),
                                        [](const BagItem& b) { return b.state == OrderState::Assigned; });
        if (first != bag.end()) {
            const std::string restaurant = first->restaurantId;
            const Travel t = travelTo(restaurants_.at(restaurant).node(), CourierState::ToRestaurant);
            if (t == Travel::Stopped) return false;
            if (t == Travel::NoRoute) return true;
            if (t == Travel::Arrived && !pickUpAt(restaurant, bag)) return false;
            continue;
        }
        const auto next = courier_.nearestDelivery(graph_);
        if (!next) {
            courier_.setState(CourierState::Idle);
            return true;
        }
        const Travel t = travelTo(*next, CourierState::Delivering);
        if (t == Travel::Stopped) return false;
        if (t == Travel::NoRoute) return true;
        if (t == Travel::Interrupted) continue;
        for (const BagItem& b : bag)
            if (b.state == OrderState::PickedUp && b.deliveryNode == *next && !deliver(b.id)) return false;
    }
}

CourierThread::Travel CourierThread::travelTo(const std::string& node, CourierState moving) {
    const std::string from = courier_.node();
    if (from == node) return Travel::Arrived;
    const auto route = graph_.shortestPath(from, node);
    if (!route) return Travel::NoRoute;
    const auto trip = std::make_shared<const RouteMotion>(*route, graph_, speedKmh_, clock_.nowMs());
    courier_.startTrip(trip, moving);
    std::int64_t legEndMs = trip->startMs();
    const std::vector<std::int64_t> durations = trip->legDurationsMs();
    const std::optional<std::size_t> breakLeg = roll_ ? roll_->roll(durations.size()) : std::nullopt;
    for (std::size_t i = 0; i < durations.size(); ++i) {
        legEndMs += durations[i];
        if (!waitSim(legEndMs)) {
            courier_.endTrip(trip->legs()[i].from, CourierState::Idle);
            return Travel::Stopped;
        }
        if (breakLeg && *breakLeg == i) return breakDownAt(trip->legs()[i].to) ? Travel::Interrupted : Travel::Stopped;
    }
    courier_.endTrip(node, moving);
    return Travel::Arrived;
}

bool CourierThread::breakDownAt(const std::string& node) {
    courier_.endTrip(node, CourierState::BrokenDown);
    breakdowns_.fetch_add(1, std::memory_order_acq_rel);
    logger_.publish(Event::courierBreakdown(clock_.nowMs(), courier_.id()));
    for (const BagItem& b : courier_.bagItems()) {  // las reservas no recogidas se liberan
        if (b.state != OrderState::Assigned) continue;
        std::optional<Order> o = courier_.removeOrder(b.id);
        if (!o) continue;
        o->release();
        if (onReleased_) onReleased_(std::move(*o));
    }
    if (courier_.bagCount() > 0 && onRescueNeeded_) onRescueNeeded_(courier_.id());  // quedan recogidos
    if (!waitSim(clock_.nowMs() + kRepairMs)) return false;
    courier_.setState(CourierState::Idle);
    if (onFreed_) onFreed_();  // vuelve al servicio: la flota cambio
    return true;
}

bool CourierThread::pickUpAt(const std::string& restaurantId, const std::vector<BagItem>& bag) {
    std::vector<BagItem> mine;
    std::int64_t readyMs = 0;
    for (const BagItem& b : bag)
        if (b.state == OrderState::Assigned && b.restaurantId == restaurantId) {
            mine.push_back(b);
            readyMs = std::max(readyMs, b.readyAtMs);
        }
    if (!waitSim(readyMs)) return false;  // la preparacion se espera fuera de la fila

    Restaurant& r = restaurants_.at(restaurantId);
    r.enterQueue(courier_.id());
    courier_.setState(CourierState::Queued);
    if (!r.waitForSlot(courier_.id(), stop_)) return false;
    struct SlotGuard {  // el puesto se devuelve en toda salida, tambien por parada
        Restaurant& r;
        ~SlotGuard() { r.leaveSlot(); }
    } guard{r};
    courier_.setState(CourierState::PickingUp);
    for (const BagItem& b : mine) {
        logger_.publish(Event::pickupStarted(clock_.nowMs(), b.id, courier_.id(), restaurantId));
        if (!waitSim(clock_.nowMs() + kPickupHandoverMs)) return false;
        courier_.markPickedUp(b.id);
        logger_.publish(Event::orderPickedUp(clock_.nowMs(), b.id, courier_.id(), restaurantId));
    }
    return true;
}

bool CourierThread::deliver(const std::string& orderId) {
    if (!waitSim(clock_.nowMs() + kDeliveryHandoverMs)) return false;
    std::optional<Order> order = courier_.removeOrder(orderId);
    if (!order) return true;
    order->markDelivered(clock_.nowMs());
    logger_.publish(Event::orderDelivered(order->deliveredAtMs, order->id, courier_.id()));
    delivered_.fetch_add(1, std::memory_order_acq_rel);
    if (onFreed_) onFreed_();
    return true;
}
