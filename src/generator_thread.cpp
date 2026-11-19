#include "generator_thread.h"

#include <string>
#include <utility>
#include <vector>

GeneratorThread::GeneratorThread(OrderGenerator& generator, OrderBook& book, EventLogger& logger,
                                 const SimClock& clock, StopSignal& stop)
    : generator_(generator), book_(book), logger_(logger), clock_(clock), stop_(stop) {}

GeneratorThread::~GeneratorThread() {
    join();
}

void GeneratorThread::start() {
    thread_ = std::thread([this] { run(); });
}

void GeneratorThread::join() {
    if (thread_.joinable()) thread_.join();
}

void GeneratorThread::run() {
    while (!stop_.requested()) {

        if (stop_.waitUntil(clock_.realTimeFor(generator_.nextAtMs()))) return;

        OrderBatch batch = generator_.next();
        batches_.fetch_add(1, std::memory_order_acq_rel);

        struct Created {
            std::string id;
            std::string restaurant;
            bool urgent;
        };
        std::vector<Created> created;
        created.reserve(batch.orders.size());
        for (const Order& o : batch.orders) created.push_back({o.id, o.restaurantId, o.urgent});

        for (const Created& c : created) {
            logger_.publish(Event::orderCreated(clock_.nowMs(), c.id, c.restaurant, c.urgent));
        }
        created_.fetch_add(static_cast<std::int64_t>(created.size()), std::memory_order_acq_rel);

        const SubmitOutcome outcome = book_.submit(std::move(batch));
        for (const Order& o : outcome.rejected) {
            logger_.publish(Event::orderRejected(clock_.nowMs(), o.id, *o.rejectReason));
        }
        rejected_.fetch_add(static_cast<std::int64_t>(outcome.rejected.size()), std::memory_order_acq_rel);
    }
}
