#include "eta_service.h"

#include "gpu/argmin_rows.h"
#include "gpu/group_orders.h"

#include <algorithm>
#include <utility>

EtaService::EtaService(Fleet& fleet, const Graph& graph, const SimClock& clock, GpuWorker& gpu, EtaBatchGpu& device,
                       bool groupOnDevice)
    : fleet_(fleet), graph_(graph), clock_(clock), gpu_(gpu), device_(device), groupOnDevice_(groupOnDevice) {
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

        const int nodes = static_cast<int>(graph_.nodeCount());
        bool groupable = nodes > 0;
        for (const int node : in.orderRestaurantNode) groupable = groupable && node >= 0 && node < nodes;

        std::vector<EtaKey> best;
        std::vector<std::size_t> row(round.size());
        for (std::size_t i = 0; i < row.size(); ++i) row[i] = i;
        try {
            EtaBatchGpu* device = &device_;
            const EtaInput* input = &in;
            const std::vector<int>* turns = &rotation;
            const bool onDevice = groupOnDevice_;
            std::pair<std::vector<int>, std::vector<EtaKey>> grouped =
                gpu_.submit([device, input, turns, nodes, groupable, onDevice] {
                        std::vector<int> order;
                        if (groupable) {
                            const int* restaurant = input->orderRestaurantNode.data();
                            order = onDevice ? groupOrdersGpu(restaurant, input->orders(), nodes).order
                                             : groupOrdersCpu(restaurant, input->orders(), nodes).order;
                        }
                        std::vector<EtaKey> keys = groupable ? device->runBest(ordersPermuted(*input, order), *turns)
                                                             : device->runBest(*input, *turns);
                        return std::make_pair(std::move(order), std::move(keys));
                    })
                    .get();
            best = std::move(grouped.second);
            for (std::size_t k = 0; k < grouped.first.size() && k < row.size(); ++k)
                row[static_cast<std::size_t>(grouped.first[k])] = k;
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
            const std::size_t k = row[i];
            if (computed && keyHasCourier(best[k])) {
                const std::size_t chosen = static_cast<std::size_t>(courierOfKey(best[k], rotation[k], couriersCount));
                if (room[chosen] > 0) {
                    --room[chosen];
                    reply.found = true;
                    reply.courierId = fleet[chosen].id;
                    reply.etaMs = etaOfKey(best[k]);
                } else {
                    reply.retry = true;
                    conflicts_.fetch_add(1, std::memory_order_acq_rel);
                }
            }
            round[i].promise.set_value(std::move(reply));
        }
    }
}
