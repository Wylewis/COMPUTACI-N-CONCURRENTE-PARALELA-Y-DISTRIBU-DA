#include "dispatcher_thread.h"

#include <utility>

#include "dispatch_select.h"

DispatcherThread::DispatcherThread(const RestaurantConfig& restaurant, OrderBook& book, Fleet& fleet,
                                   const Graph& graph, double speedKmh, const SimClock& clock, StopSignal& stop)
    : restaurant_(restaurant), book_(book), fleet_(fleet), graph_(graph), speedKmh_(speedKmh), clock_(clock),
      stop_(stop) {}

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
        if (!dispatch(*order)) {
            book_.putBack(std::move(*order));
            return;
        }
    }
}

bool DispatcherThread::dispatch(Order& order) {
    while (!stop_.requested()) {
        const std::uint64_t seen = fleet_.version();
        const std::int64_t now = clock_.nowMs();
        const auto sel = selectCourier(fleet_.snapshots(now), order, restaurant_, graph_, speedKmh_, now);
        if (!sel) {
            waits_.fetch_add(1, std::memory_order_acq_rel);
            fleet_.waitForChange(seen);
            continue;
        }
        if (fleet_.assign(sel->estimate.courierId, order)) {
            assigned_.fetch_add(1, std::memory_order_acq_rel);
            return true;
        }
        retries_.fetch_add(1, std::memory_order_acq_rel);
    }
    return false;
}
