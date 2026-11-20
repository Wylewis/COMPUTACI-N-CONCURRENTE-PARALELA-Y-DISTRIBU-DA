#pragma once

#include <cstddef>
#include <vector>

#include "gpu_kernel_info.h"
#include "gpu_timing_types.h"
#include "thread_pool.h"

constexpr int kHistogramMaxBins = 256;
constexpr int kDeliveryBinMs = 60'000;

enum class HistogramMode { Privatized, Atomic };

inline int histogramBins(int bins) { return bins < 1 ? 1 : (bins > kHistogramMaxBins ? kHistogramMaxBins : bins); }

inline int deliveryBinOf(int ms, int binMs, int bins) {
    const int bin = ms <= 0 ? 0 : ms / binMs;
    return bin >= bins ? bins - 1 : bin;
}

std::vector<unsigned> deliveryHistogramCpu(const int* ms, std::size_t n, int bins, int binMs = kDeliveryBinMs);
std::vector<unsigned> deliveryHistogramCpuParallel(const int* ms, std::size_t n, int bins, ThreadPool& pool,
                                                   int binMs = kDeliveryBinMs);
std::vector<unsigned> deliveryHistogramGpu(const int* ms, std::size_t n, int bins,
                                           HistogramMode mode = HistogramMode::Privatized, GpuTimes* times = nullptr,
                                           int binMs = kDeliveryBinMs);

int histogramPercentileBin(const std::vector<unsigned>& counts, double fraction);

std::vector<GpuKernelInfo> deliveryHistogramKernelInfos();
