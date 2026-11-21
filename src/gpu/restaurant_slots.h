#pragma once

#include <cstddef>
#include <vector>

#include "gpu_kernel_info.h"
#include "gpu_timing_types.h"
#include "thread_pool.h"

constexpr int kDemandSlotMs = 600'000;

enum class SlotKernel { Atomic, Aggregated };

struct BusiestSlot {
    int restaurant = -1;
    int slot = -1;
    unsigned orders = 0;
};

inline int demandCell(int restaurant, int ms, int slotMs, int slots) {
    int slot = ms <= 0 ? 0 : ms / slotMs;
    if (slot >= slots) slot = slots - 1;
    return restaurant * slots + slot;
}

std::vector<unsigned> restaurantSlotsCpu(const int* restaurant, const int* ms, std::size_t n, int restaurants, int slots,
                                         int slotMs = kDemandSlotMs);
std::vector<unsigned> restaurantSlotsCpuParallel(const int* restaurant, const int* ms, std::size_t n, int restaurants,
                                                 int slots, ThreadPool& pool, int slotMs = kDemandSlotMs);
std::vector<unsigned> restaurantSlotsGpu(const int* restaurant, const int* ms, std::size_t n, int restaurants, int slots,
                                         SlotKernel kernel = SlotKernel::Atomic, GpuTimes* times = nullptr,
                                         int slotMs = kDemandSlotMs);

BusiestSlot busiestSlot(const std::vector<unsigned>& counts, int slots);

std::vector<GpuKernelInfo> restaurantSlotsKernelInfos();
