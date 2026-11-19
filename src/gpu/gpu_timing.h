#pragma once

#include "cuda_compat.h"

struct GpuTimes {
    double kernelMs = 0.0;
    double totalMs = 0.0;
};

#ifdef __CUDACC__
class DeviceTimer {
public:
    DeviceTimer() {
        cudaEventCreate(&start_);
        cudaEventCreate(&stop_);
        cudaEventRecord(start_);
    }
    ~DeviceTimer() {
        cudaEventDestroy(start_);
        cudaEventDestroy(stop_);
    }
    DeviceTimer(const DeviceTimer&) = delete;
    DeviceTimer& operator=(const DeviceTimer&) = delete;
    double ms() {
        cudaEventRecord(stop_);
        cudaEventSynchronize(stop_);
        float elapsed = 0.0f;
        cudaEventElapsedTime(&elapsed, start_, stop_);
        return static_cast<double>(elapsed);
    }

private:
    cudaEvent_t start_, stop_;
};
#else
class DeviceTimer {
public:
    double ms() { return timer_.ms(); }

private:
    Timer timer_;
};
#endif
