#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "fleet.h"
#include "gpu/eta_batch.h"
#include "gpu/gpu_worker.h"
#include "graph.h"
#include "sim_clock.h"

class EtaService {
public:
    struct Reply {
        bool found = false;
        std::string courierId;
        int etaMs = kEtaUnavailable;
    };

    EtaService(Fleet& fleet, const Graph& graph, const SimClock& clock, GpuWorker& gpu, EtaBatchGpu& device);
    ~EtaService();
    EtaService(const EtaService&) = delete;
    EtaService& operator=(const EtaService&) = delete;

    std::future<Reply> request(int restaurantNode, int deliveryNode, int readyMs);
    void stop();

    std::int64_t rounds() const { return rounds_.load(std::memory_order_acquire); }
    std::int64_t requests() const { return requests_.load(std::memory_order_acquire); }
    std::int64_t largestRound() const { return largest_.load(std::memory_order_acquire); }

private:
    struct Pending {
        int restaurantNode;
        int deliveryNode;
        int readyMs;
        std::promise<Reply> promise;
    };

    void loop();

    Fleet& fleet_;
    const Graph& graph_;
    const SimClock& clock_;
    GpuWorker& gpu_;
    EtaBatchGpu& device_;
    std::mutex mutex_;
    std::condition_variable available_;
    std::deque<Pending> pending_;
    bool stopping_ = false;
    std::atomic<std::int64_t> rounds_{0}, requests_{0}, largest_{0};
    std::thread thread_;
};
