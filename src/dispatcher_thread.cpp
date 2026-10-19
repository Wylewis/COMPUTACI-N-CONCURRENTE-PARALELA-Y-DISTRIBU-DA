#include "dispatcher_thread.h"

#include <memory>
#include <utility>

#include "dispatch_select.h"

DispatcherThread::DispatcherThread(const RestaurantConfig& restaurant, OrderBook& book, Fleet& fleet,
                                   const Graph& graph, double speedKmh, const DispatchConfig& dispatch,
                                   ThreadPool& estimators, EventLogger& logger, const SimClock& clock, StopSignal& stop)
    : restaurant_(restaurant), book_(book), fleet_(fleet), graph_(graph), speedKmh_(speedKmh),
      acceptTimeoutMs_(dispatch.acceptTimeoutMs), quoteTimeout_(dispatch.quoteTimeoutMs), estimators_(estimators),
      logger_(logger), clock_(clock), stop_(stop) {}

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
    // Un pedido liberado por averia ya fue aceptado: no se rechaza, espera moto sin plazo (regla 11.4).
    const bool wasAssigned = !order.courierId.empty();
    const std::int64_t deadlineMs = order.createdAtMs + acceptTimeoutMs_;
    while (!stop_.requested()) {
        const std::uint64_t seen = fleet_.version();
        const std::int64_t now = clock_.nowMs();
        // Las tareas reciben una copia del pedido: una tardia puede seguir en el
        // pool cuando el original ya se movio a la moto.
        auto copy = std::make_shared<Order>();
        copy->id = order.id, copy->restaurantId = order.restaurantId, copy->deliveryNode = order.deliveryNode;
        copy->createdAtMs = order.createdAtMs, copy->readyAtMs = order.readyAtMs;
        const Mapper concurrent = [&, copy](const std::vector<CourierSnapshot>& ring) {
            AsyncEstimates r = estimateAllPooled(
                ring,
                [copy, this, now](const CourierSnapshot& c) { return estimateDelivery(c, *copy, restaurant_, graph_, speedKmh_, now); },
                estimators_, quoteTimeout_);
            lateQuotes_.fetch_add(r.late, std::memory_order_acq_rel);
            return r.estimates;
        };
        const auto sel = selectCourier(fleet_.snapshots(now), order, restaurant_, graph_, speedKmh_, now, kFirstRadiusM,
                                       concurrent);
        if (sel) {
            if (fleet_.assign(sel->estimate.courierId, order)) {
                assigned_.fetch_add(1, std::memory_order_acq_rel);
                return Outcome::Assigned;
            }
            retries_.fetch_add(1, std::memory_order_acq_rel);
            continue;
        }
        if (!wasAssigned && now >= deadlineMs) {
            order.reject(RejectReason::NoCourier);
            logger_.publish(Event::orderRejected(now, order.id, RejectReason::NoCourier));
            rejected_.fetch_add(1, std::memory_order_acq_rel);
            return Outcome::Rejected;
        }
        waits_.fetch_add(1, std::memory_order_acq_rel);
        if (wasAssigned) fleet_.waitForChange(seen);
        else fleet_.waitForChange(seen, clock_.realTimeFor(deadlineMs));
    }
    return Outcome::Stopped;
}
