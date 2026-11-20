#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gpu_kernel_info.h"
#include "gpu_timing_types.h"
#include "thread_pool.h"

constexpr int kTrendRadius = 7;
constexpr int kTrendDim = 2 * kTrendRadius + 1;
constexpr float kTrendTolerance = 1e-4f;
constexpr std::int64_t kTrendSlotMs = 60'000;

enum class TrendKernel { Global, Tiled };

std::vector<float> trendWeights(float sigma = 3.0f);

std::vector<float> ordersPerSlot(const std::vector<std::int64_t>& createdMs, std::int64_t slotMs = kTrendSlotMs);

std::vector<float> trendCpu(const float* counts, std::size_t n, const float* weights);
std::vector<float> trendCpuParallel(const float* counts, std::size_t n, const float* weights, ThreadPool& pool);
std::vector<float> trendGpu(const float* counts, std::size_t n, const float* weights, TrendKernel kernel = TrendKernel::Global,
                            GpuTimes* times = nullptr);

bool trendsMatch(const std::vector<float>& a, const std::vector<float>& b, float tolerance = kTrendTolerance);
std::size_t trendPeak(const std::vector<float>& trend);

std::vector<GpuKernelInfo> demandTrendKernelInfos();
