#pragma once

#include <cstddef>
#include <string>

struct GpuDeviceInfo {
    std::string backend;
    std::string name;
    bool realGpu = false;
    int multiprocessors = 0;
    int maxThreadsPerBlock = 0;
    int warpSize = 0;
    std::size_t globalMemBytes = 0;
    std::size_t sharedMemPerBlock = 0;
};

GpuDeviceInfo gpuDeviceInfo();

bool gpuSelfTest(int n);
