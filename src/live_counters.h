#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

constexpr std::size_t kCacheLineBytes = 128;

struct alignas(kCacheLineBytes) CourierCounters {
    std::atomic<std::int64_t> delivered{0};
    std::atomic<std::int64_t> pickedUp{0};
    std::atomic<std::int64_t> breakdowns{0};
};
static_assert(sizeof(CourierCounters) % kCacheLineBytes == 0, "un contador por linea de cache");

struct CountersSnapshot {
    std::int64_t delivered = 0, pickedUp = 0, breakdowns = 0;
    std::vector<std::int64_t> deliveredPerCourier;
};

class LiveCounters {
public:
    explicit LiveCounters(std::size_t couriers);
    std::size_t size() const { return count_; }
    CourierCounters& at(std::size_t i) { return slots_[i]; }
    CountersSnapshot snapshot() const;

private:
    std::size_t count_;
    std::unique_ptr<CourierCounters[]> slots_;
};
