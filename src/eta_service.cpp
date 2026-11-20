#include "eta_service.h"

#include "gpu/argmin_rows.h"

#include <algorithm>
#include <utility>

EtaService::EtaService(Fleet& fleet, const Graph& graph, const SimClock& clock, GpuWorker& gpu, EtaBatchGpu& device)
    : fleet_(fleet), graph_(graph), clock_(clock), gpu_(gpu), device_(device) {
    thread_ = std::thread([this] { loop(); });
}

EtaService::~EtaService() { stop(); }

std::future<EtaService::Reply> EtaService::request(int restaurantNode, int deliveryNode, int readyMs) {
    Pending p{restaurantNode, deliveryNode, readyMs, {}};
    std::future<Reply> reply = p.promise.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            p.promise.set_value(Reply{});
            return reply;
        }
        pending_.push_back(std::move(p));
    }
    available_.notify_one();
    return reply;
}

void EtaService::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    available_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void EtaService::loop() {
    for (;;) {
        std::deque<Pending> round;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            available_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (pending_.empty()) return;
            round.swap(pending_);
        }

        const std::int64_t now = clock_.nowMs();
        const std::vector<CourierSnapshot> fleet = fleet_.snapshots(now);
        std::vector<int> room;
        EtaInput in;
        in.nowMs = static_cast<int>(now);
        for (const CourierSnapshot& c : fleet) {
            room.push_back(c.broken ? 0 : std::max(0, c.bagCapacity - c.bagCount));
            in.courierNode.push_back(static_cast<int>(graph_.nodeIndex(c.node)));
            in.courierAvailableMs.push_back(static_cast<int>(c.availableAtMs));
            in.courierFree.push_back(!c.broken && c.bagCount < c.bagCapacity ? 1 : 0);
        }
        for (const Pending& p : round) {
            in.orderRestaurantNode.push_back(p.restaurantNode);
            in.orderDeliveryNode.push_back(p.deliveryNode);
            in.orderReadyMs.push_back(p.readyMs);
        }

        const int couriersCount = static_cast<int>(fleet.size());
        std::vector<int> rotation(round.size(), 0);
        if (couriersCount > 0)
            for (std::size_t i = 0; i < round.size(); ++i) rotation[i] = static_cast<int>((i * 7919) % fleet.size());

        std::vector<EtaKey> best;
        try {
            EtaBatchGpu* device = &device_;
            const EtaInput* input = &in;
            const std::vector<int>* turns = &rotation;
            best = gpu_.submit([device, input, turns] { return device->runBest(*input, *turns); }).get();
        } catch (const std::exception&) {
            best.clear();
        }

        rounds_.fetch_add(1, std::memory_order_acq_rel);
        requests_.fetch_add(static_cast<std::int64_t>(round.size()), std::memory_order_acq_rel);
        if (static_cast<std::int64_t>(round.size()) > largest_.load(std::memory_order_acquire))
            largest_.store(static_cast<std::int64_t>(round.size()), std::memory_order_release);

        const bool computed = best.size() == round.size() && couriersCount > 0;
        for (std::size_t i = 0; i < round.size(); ++i) {
            Reply reply;
            if (computed && keyHasCourier(best[i])) {
                const std::size_t chosen = static_cast<std::size_t>(courierOfKey(best[i], rotation[i], couriersCount));
                if (room[chosen] > 0) {
                    --room[chosen];
                    reply.found = true;
                    reply.courierId = fleet[chosen].id;
                    reply.etaMs = etaOfKey(best[i]);
                } else {
                    reply.retry = true;
                    conflicts_.fetch_add(1, std::memory_order_acq_rel);
                }
            }
            round[i].promise.set_value(std::move(reply));
        }
    }
}
