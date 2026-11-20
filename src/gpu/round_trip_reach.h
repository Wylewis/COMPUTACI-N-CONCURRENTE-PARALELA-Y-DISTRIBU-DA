#pragma once

#include <cstddef>
#include <vector>

#include "gpu_kernel_info.h"
#include "gpu_timing_types.h"
#include "thread_pool.h"
#include "travel_table.h"

constexpr int kRoundTripLimitMs = 600'000;

enum class RoundTripLayout { Turned, Column };

std::vector<int> turnedTable(const std::vector<int>& table, int n);

std::vector<unsigned> roundTripReachCpu(const int* table, const int* turned, int n, int limitMs = kRoundTripLimitMs);
std::vector<unsigned> roundTripReachCpuColumn(const int* table, int n, int limitMs = kRoundTripLimitMs);
std::vector<unsigned> roundTripReachCpuParallel(const int* table, const int* turned, int n, ThreadPool& pool,
                                                int limitMs = kRoundTripLimitMs);
std::vector<unsigned> roundTripReachGpu(const int* table, const int* turned, int n,
                                        RoundTripLayout layout = RoundTripLayout::Turned, GpuTimes* times = nullptr,
                                        int limitMs = kRoundTripLimitMs);

std::size_t widestReach(const std::vector<unsigned>& reach);

std::vector<GpuKernelInfo> roundTripReachKernelInfos();
