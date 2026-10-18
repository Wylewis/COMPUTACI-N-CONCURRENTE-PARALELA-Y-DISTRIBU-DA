#include "dispatcher_thread.h"

#include <utility>

#include "dispatch_select.h"

DispatcherThread::DispatcherThread(const RestaurantConfig& restaurant, OrderBook& book, Fleet& fleet,
                                   const Graph& graph, double speedKmh, std::int64_t acceptTimeoutMs,
                                   EventLogger& logger, const SimClock& clock, StopSignal& stop)
    : restaurant_(restaurant), book_(book), fleet_(fleet), graph_(graph), speedKmh_(speedKmh),
      acceptTimeoutMs_(acceptTimeoutMs), logger_(logger), clock_(clock), stop_(stop) {}

DispatcherThread::~DispatcherThread() { join(); }

void DispatcherThread::start() { thread_ = std::thread([this] { run(); }); }

void DispatcherThread::join() {
    fleet_.wake();
    if (thread_.joinable()) thread_.join();
}

void DispatcherThread::run() {
    while (!stop_.requested()) {
        std::optional<Order> order = book_.take(restaurant_.id);
        if (!order) return;
        if (dispatch(*order) == Outcome::Stopped) {
            book_.putBack(std::move(*order));
            return;
        }
    }
}

DispatcherThread::Outcome DispatcherThread::dispatch(Order& order) {
    const std::int64_t deadlineMs = order.createdAtMs + acceptTimeoutMs_;
    while (!stop_.requested()) {
        const std::uint64_t seen = fleet_.version();
        const std::int64_t now = clock_.nowMs();
        const auto sel = selectCourier(fleet_.snapshots(now), order, restaurant_, graph_, speedKmh_, now);
        if (sel) {
            if (fleet_.assign(sel->estimate.courierId, order)) {
                assigned_.fetch_add(1, std::memory_order_acq_rel);
                return Outcome::Assigned;
            }
            retries_.fetch_add(1, std::memory_order_acq_rel);
            continue;
        }
        if (now >= deadlineMs) {
            order.reject(RejectReason::NoCourier);
            logger_.publish(Event::orderRejected(now, order.id, RejectReason::NoCourier));
            rejected_.fetch_add(1, std::memory_order_acq_rel);
            return Outcome::Rejected;
        }
        waits_.fetch_add(1, std::memory_order_acq_rel);
        fleet_.waitForChange(seen, clock_.realTimeFor(deadlineMs));
    }
    return Outcome::Stopped;
}
