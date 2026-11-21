#pragma once

#include <cstddef>
#include <vector>

#include "gpu_kernel_info.h"
#include "gpu_timing_types.h"
#include "thread_pool.h"

constexpr int kLateDeliveryMs = 45 * 60'000;
constexpr unsigned kKpiPerThread = 64;
constexpr int kKpiNoMax = -2147483647 - 1;

struct DeliveryKpis {
    long long sumMs;
    int maxMs;
    unsigned late;
};

inline DeliveryKpis noDeliveries() {
    DeliveryKpis none;
    none.sumMs = 0;
    none.maxMs = kKpiNoMax;
    none.late = 0;
    return none;
}

inline bool operator==(const DeliveryKpis& a, const DeliveryKpis& b) {
    return a.sumMs == b.sumMs && a.maxMs == b.maxMs && a.late == b.late;
}

DeliveryKpis deliveryKpisCpu(const int* ms, std::size_t n, int lateMs = kLateDeliveryMs);
DeliveryKpis deliveryKpisCpuParallel(const int* ms, std::size_t n, ThreadPool& pool, int lateMs = kLateDeliveryMs);
DeliveryKpis deliveryKpisGpu(const int* ms, std::size_t n, int lateMs = kLateDeliveryMs,
                             unsigned perThread = kKpiPerThread, GpuTimes* times = nullptr, int* launches = nullptr);

std::vector<GpuKernelInfo> deliveryKpisKernelInfos();
