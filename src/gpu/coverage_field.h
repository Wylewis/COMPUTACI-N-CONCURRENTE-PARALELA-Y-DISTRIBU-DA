#pragma once

#include <cstddef>
#include <vector>

#include "gpu_kernel_info.h"
#include "gpu_timing_types.h"
#include "thread_pool.h"

constexpr int kCoverFar = 1 << 29;
constexpr int kCoverCheckEvery = 16;

enum class CoverKernel { Basic, Tiled };

struct CoverageMap {
    int grid = 0;
    int stepXMs = 1;
    int stepYMs = 1;
    std::vector<unsigned char> open;
};

struct CoverageWorst {
    std::size_t cell = 0;
    int ms = 0;
    std::size_t reached = 0;
    std::size_t unreached = 0;
};

std::vector<int> coverageSeed(const CoverageMap& map, const float* x, const float* y, std::size_t points);

std::vector<int> coverageDijkstra(const CoverageMap& map, const int* seed);
std::vector<int> coverageCpu(const CoverageMap& map, const int* seed, int* sweeps = nullptr);
std::vector<int> coverageCpuParallel(const CoverageMap& map, const int* seed, ThreadPool& pool, int* sweeps = nullptr);
std::vector<int> coverageGpu(const CoverageMap& map, const int* seed, CoverKernel kernel = CoverKernel::Basic,
                             GpuTimes* times = nullptr, int* sweeps = nullptr);

CoverageWorst worstCovered(const CoverageMap& map, const std::vector<int>& field);

unsigned coverageChangedBlocks(const int* aDevice, const int* bDevice, std::size_t cells, unsigned* changedDevice);

std::vector<GpuKernelInfo> coverageFieldKernelInfos();
