#pragma once

#include <cstddef>
#include <vector>

#include "gpu_kernel_info.h"
#include "gpu_timing_types.h"
#include "thread_pool.h"

struct OrderGroups {
    std::vector<int> start;
    std::vector<int> order;

    bool operator==(const OrderGroups& other) const { return start == other.start && order == other.order; }
};

std::vector<int> exclusiveScanCpu(const std::vector<int>& values);
std::vector<int> exclusiveScanCpuParallel(const std::vector<int>& values, ThreadPool& pool);
std::vector<int> exclusiveScanGpu(const std::vector<int>& values, GpuTimes* times = nullptr);

OrderGroups groupOrdersCpu(const int* group, std::size_t orders, int groups);
OrderGroups groupOrdersCpuParallel(const int* group, std::size_t orders, int groups, ThreadPool& pool);
OrderGroups groupOrdersGpu(const int* group, std::size_t orders, int groups, GpuTimes* times = nullptr);

void sortWithinGroups(OrderGroups& groups);

std::vector<GpuKernelInfo> groupOrdersKernelInfos();
