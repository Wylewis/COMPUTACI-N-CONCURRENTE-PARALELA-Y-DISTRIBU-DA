#pragma once

#include <cstddef>
#include <vector>

#include "gpu_kernel_info.h"
#include "gpu_timing_types.h"
#include "thread_pool.h"

constexpr int kHeatRadius = 3;
constexpr int kHeatDim = 2 * kHeatRadius + 1;
constexpr float kHeatTolerance = 1e-4f;

enum class HeatWeights { Constant, Global };

std::vector<float> gaussianWeights(float sigma = 1.5f);

std::vector<float> binDemand(const float* x, const float* y, std::size_t points, int grid);

std::vector<float> smoothCpu(const float* counts, int grid, const float* weights);
std::vector<float> smoothCpuParallel(const float* counts, int grid, const float* weights, ThreadPool& pool);
std::vector<float> smoothGpu(const float* counts, int grid, const float* weights, HeatWeights where = HeatWeights::Constant,
                             GpuTimes* times = nullptr);

bool heatmapsMatch(const std::vector<float>& a, const std::vector<float>& b, float tolerance = kHeatTolerance);
std::size_t hottestCell(const std::vector<float>& heat);

std::vector<GpuKernelInfo> demandHeatmapKernelInfos();
