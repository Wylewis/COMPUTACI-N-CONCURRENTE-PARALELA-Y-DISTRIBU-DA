#pragma once

#include <cstddef>
#include <vector>

#include "coverage_field.h"

constexpr int kResponseSweepsPerLaunch = 8;

enum class ResponseKernel { ManySweeps, OneSweep };

std::vector<int> responseSeed(const CoverageMap& map, const float* x, const float* y, const int* waitMs, std::size_t points);

std::vector<int> responseFieldGpu(const CoverageMap& map, const int* seed, ResponseKernel kernel = ResponseKernel::ManySweeps,
                                  GpuTimes* times = nullptr, int* sweeps = nullptr, int* launches = nullptr);

std::vector<GpuKernelInfo> responseFieldKernelInfos();
