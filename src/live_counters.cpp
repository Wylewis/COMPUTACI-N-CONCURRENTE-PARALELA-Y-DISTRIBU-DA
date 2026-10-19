#include "live_counters.h"

LiveCounters::LiveCounters(std::size_t couriers) : count_(couriers), slots_(new CourierCounters[couriers]) {}

CountersSnapshot LiveCounters::snapshot() const {
    CountersSnapshot s;
    s.deliveredPerCourier.reserve(count_);
    for (std::size_t i = 0; i < count_; ++i) {
        const std::int64_t d = slots_[i].delivered.load(std::memory_order_relaxed);
        s.deliveredPerCourier.push_back(d);
        s.delivered += d;
        s.pickedUp += slots_[i].pickedUp.load(std::memory_order_relaxed);
        s.breakdowns += slots_[i].breakdowns.load(std::memory_order_relaxed);
    }
    return s;
}
