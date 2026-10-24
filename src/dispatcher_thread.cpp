#include "dispatcher_thread.h"

#include <memory>
#include <utility>

#include "dispatch_select.h"

DispatcherThread::DispatcherThread(const RestaurantConfig& restaurant, OrderBook& book, Fleet& fleet,
                                   const Graph& graph, double speedKmh, const DispatchConfig& dispatch,
                                   ThreadPool& estimators, EventLogger& logger, const SimClock& clock, StopSignal& stop)
    : restaurant_(restaurant), book_(book), fleet_(fleet), graph_(graph), speedKmh_(speedKmh),
      quoteTimeout_(dispatch.quoteTimeoutMs), estimator_(dispatch.estimator), estimators_(estimators),
      logger_(logger), clock_(clock), stop_(stop) {}

DispatcherThread::~DispatcherThread() { join(); }

void DispatcherThread::start() { thread_ = std::thread([this] { run(); }); }

void DispatcherThread::join() {
    fleet_.wake();
    if (thread_.joinable()) thread_.join();
}

void DispatcherThread::run() {
    while (!stop_.requested()) {
        rejectExpired();
        std::optional<Order> order = book_.take(restaurant_.id, clock_.nowMs());
        if (!order) return;
        const std::uint64_t seen = fleet_.version();
        const Outcome outcome = dispatch(*order);
        if (outcome == Outcome::Assigned) continue;
        book_.putBack(std::move(*order));
        if (outcome == Outcome::Stopped) return;
        waits_.fetch_add(1, std::memory_order_acq_rel);
        if (const auto wake = book_.nextWakeMs(restaurant_.id, clock_.nowMs()))
            fleet_.waitForChange(seen, clock_.realTimeFor(*wake));
        else
            fleet_.waitForChange(seen);
    }
}

void DispatcherThread::rejectExpired() {
    const std::int64_t now = clock_.nowMs();
    std::vector<Order> expired = book_.takeExpired(restaurant_.id, now);
    for (Order& o : expired) {
        o.reject(RejectReason::NoCourier);
        logger_.publish(Event::orderRejected(now, o.id, RejectReason::NoCourier));
        rejected_.fetch_add(1, std::memory_order_acq_rel);
    }
}

DispatcherThread::Outcome DispatcherThread::dispatch(Order& order) {
    while (!stop_.requested()) {
        const std::int64_t now = clock_.nowMs();

        auto copy = std::make_shared<Order>();
        copy->id = order.id, copy->restaurantId = order.restaurantId, copy->deliveryNode = order.deliveryNode;
        copy->createdAtMs = order.createdAtMs, copy->readyAtMs = order.readyAtMs;
        const OneEstimate one = [copy, this, now](const CourierSnapshot& c) {
            return estimateDelivery(c, *copy, restaurant_, graph_, speedKmh_, now);
        };
        Mapper mapper;
        if (estimator_ == "pool")
            mapper = [&](const std::vector<CourierSnapshot>& ring) {
                AsyncEstimates r = estimateAllPooled(ring, one, estimators_, quoteTimeout_);
                lateQuotes_.fetch_add(r.late, std::memory_order_acq_rel);
                return r.estimates;
            };
        else if (estimator_ == "async")
            mapper = [&](const std::vector<CourierSnapshot>& ring) {
                AsyncEstimates r = estimateAllAsync(ring, one, quoteTimeout_);
                lateQuotes_.fetch_add(r.late, std::memory_order_acq_rel);
                return r.estimates;
            };
        const auto sel = selectCourier(fleet_.snapshots(now), order, restaurant_, graph_, speedKmh_, now, kFirstRadiusM, mapper);
        if (!sel) return Outcome::NoCourier;
        if (fleet_.assign(sel->estimate.courierId, order)) {
            assigned_.fetch_add(1, std::memory_order_acq_rel);
            return Outcome::Assigned;
        }
        retries_.fetch_add(1, std::memory_order_acq_rel);
    }
    return Outcome::Stopped;
}
