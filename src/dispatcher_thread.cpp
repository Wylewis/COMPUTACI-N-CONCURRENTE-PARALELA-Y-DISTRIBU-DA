#include "dispatcher_thread.h"

#include <memory>
#include <utility>

#include "dispatch_select.h"
#include "estimate.h"
#include "eta_service.h"

static_assert(kEtaPickupHandoverMs == kPickupHandoverMs && kEtaDeliveryHandoverMs == kDeliveryHandoverMs,
              "la estimacion por lote usa los mismos tiempos de entrega en mano que la estimacion por ruta");

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

void DispatcherThread::useTravelTable(const TravelTable* table, EtaService* service) {
    table_ = table;
    etaService_ = service;
}

DispatcherThread::Outcome DispatcherThread::dispatchWithTable(Order& order) {
    const int restaurantNode = static_cast<int>(graph_.nodeIndex(restaurant_.node));
    const int deliveryNode = static_cast<int>(graph_.nodeIndex(order.deliveryNode));
    while (!stop_.requested()) {
        std::string chosen;
        if (estimator_ == "gpu") {
            auto pending = etaService_->request(restaurantNode, deliveryNode, static_cast<int>(order.readyAtMs));
            if (pending.wait_for(quoteTimeout_) != std::future_status::ready) {
                lateQuotes_.fetch_add(1, std::memory_order_acq_rel);
                return Outcome::NoCourier;
            }
            const EtaService::Reply reply = pending.get();
            if (!reply.found) return Outcome::NoCourier;
            chosen = reply.courierId;
        } else {
            const std::int64_t now = clock_.nowMs();
            const std::vector<CourierSnapshot> fleet = fleet_.snapshots(now);
            EtaInput input;
            input.nowMs = static_cast<int>(now);
            input.orderRestaurantNode = {restaurantNode};
            input.orderDeliveryNode = {deliveryNode};
            input.orderReadyMs = {static_cast<int>(order.readyAtMs)};
            for (const CourierSnapshot& c : fleet) {
                input.courierNode.push_back(static_cast<int>(graph_.nodeIndex(c.node)));
                input.courierAvailableMs.push_back(static_cast<int>(c.availableAtMs));
                input.courierFree.push_back(!c.broken && c.bagCount < c.bagCapacity ? 1 : 0);
            }
            std::vector<int> eta(fleet.size(), kEtaUnavailable);
            etaBatchCpu(*table_, input, eta.data());
            std::size_t best = fleet.size();
            for (std::size_t c = 0; c < eta.size(); ++c)
                if (eta[c] != kEtaUnavailable && (best == fleet.size() || eta[c] < eta[best])) best = c;
            if (best == fleet.size()) return Outcome::NoCourier;
            chosen = fleet[best].id;
        }
        if (fleet_.assign(chosen, order)) {
            assigned_.fetch_add(1, std::memory_order_acq_rel);
            return Outcome::Assigned;
        }
        retries_.fetch_add(1, std::memory_order_acq_rel);
    }
    return Outcome::Stopped;
}

DispatcherThread::Outcome DispatcherThread::dispatch(Order& order) {
    if (table_ && (estimator_ == "table" || (estimator_ == "gpu" && etaService_))) return dispatchWithTable(order);
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
