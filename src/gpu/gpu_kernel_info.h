#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct GpuKernelInfo {
    std::string application;
    std::string kernel;
    int threadsPerBlock = 0;
    bool measured = false;
    int blocksPerMultiprocessor = 0;
    int registersPerThread = 0;
    std::size_t sharedBytesPerBlock = 0;
    double occupancyPercent = 0.0;
    std::string limiter;
};

std::vector<GpuKernelInfo> gpuKernelInfos();
