#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gpu_kernel_info.h"
#include "gpu_timing_types.h"
#include "thread_pool.h"

constexpr std::int64_t kBacklogSlotMs = 1000;

enum class ScanKernel { KoggeStone, BrentKung };

struct BacklogPeak {
    std::size_t slot = 0;
    int waiting = 0;
};

std::vector<int> backlogDeltas(const std::vector<std::int64_t>& createdMs, const std::vector<std::int64_t>& closedMs,
                               std::int64_t slotMs = kBacklogSlotMs);

std::vector<int> backlogCurveCpu(const int* delta, std::size_t slots);
std::vector<int> backlogCurveCpuParallel(const int* delta, std::size_t slots, ThreadPool& pool);
std::vector<int> backlogCurveGpu(const int* delta, std::size_t slots, ScanKernel kernel = ScanKernel::KoggeStone,
                                 GpuTimes* times = nullptr, int* levels = nullptr);

BacklogPeak backlogPeak(const std::vector<int>& curve);

std::vector<GpuKernelInfo> backlogCurveKernelInfos();
