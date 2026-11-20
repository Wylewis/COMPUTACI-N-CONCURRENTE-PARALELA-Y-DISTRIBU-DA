#include "round_trip_reach.h"

#include "cuda_compat.h"
#include "gpu_occupancy.h"
#include "gpu_timing.h"

constexpr unsigned REACH_BLOCK = 256;

__global__ void reachTurnedKernel(const int* table, const int* turned, int n, int limitMs, unsigned* reach) {
    __shared__ unsigned counts[REACH_BLOCK];

    const unsigned t = threadIdx.x;
    const size_t row = static_cast<size_t>(blockIdx.x) * static_cast<size_t>(n);
    unsigned mine = 0;
    for (int j = static_cast<int>(t); j < n; j += static_cast<int>(REACH_BLOCK))
        mine += table[row + static_cast<size_t>(j)] + turned[row + static_cast<size_t>(j)] <= limitMs ? 1u : 0u;
    counts[t] = mine;
    __syncthreads();
    for (unsigned stride = REACH_BLOCK / 2; stride > 0; stride /= 2) {
        if (t < stride) counts[t] += counts[t + stride];
        __syncthreads();
    }
    if (t == 0) reach[blockIdx.x] = counts[0];
}

__global__ void reachColumnKernel(const int* table, int n, int limitMs, unsigned* reach) {
    __shared__ unsigned counts[REACH_BLOCK];

    const unsigned t = threadIdx.x;
    const size_t side = static_cast<size_t>(n);
    const size_t row = static_cast<size_t>(blockIdx.x) * side;
    unsigned mine = 0;
    for (int j = static_cast<int>(t); j < n; j += static_cast<int>(REACH_BLOCK))
        mine += table[row + static_cast<size_t>(j)] + table[static_cast<size_t>(j) * side + blockIdx.x] <= limitMs ? 1u : 0u;
    counts[t] = mine;
    __syncthreads();
    for (unsigned stride = REACH_BLOCK / 2; stride > 0; stride /= 2) {
        if (t < stride) counts[t] += counts[t + stride];
        __syncthreads();
    }
    if (t == 0) reach[blockIdx.x] = counts[0];
}

std::vector<unsigned> roundTripReachGpu(const int* table, const int* turned, int n, RoundTripLayout layout, GpuTimes* times,
                                        int limitMs) {
    Timer total;
    const std::size_t side = static_cast<std::size_t>(n > 0 ? n : 0);
    std::vector<unsigned> reach(side, 0);
    if (side == 0) return reach;

    const std::size_t cells = side * side;
    int *tableDevice = nullptr, *turnedDevice = nullptr;
    unsigned* reachDevice = nullptr;
    CHECK(cudaMalloc(&tableDevice, cells * sizeof(int)));
    CHECK(cudaMalloc(&reachDevice, side * sizeof(unsigned)));
    CHECK(cudaMemcpy(tableDevice, table, cells * sizeof(int), cudaMemcpyHostToDevice));
    if (layout == RoundTripLayout::Turned) {
        CHECK(cudaMalloc(&turnedDevice, cells * sizeof(int)));
        CHECK(cudaMemcpy(turnedDevice, turned, cells * sizeof(int), cudaMemcpyHostToDevice));
    }
    double kernelMs = 0.0;
    {
        DeviceTimer timer;
        if (layout == RoundTripLayout::Turned)
            launch(reachTurnedKernel, dim3(static_cast<unsigned>(side)), dim3(REACH_BLOCK), static_cast<const int*>(tableDevice),
                   static_cast<const int*>(turnedDevice), n, limitMs, reachDevice);
        else
            launch(reachColumnKernel, dim3(static_cast<unsigned>(side)), dim3(REACH_BLOCK), static_cast<const int*>(tableDevice), n,
                   limitMs, reachDevice);
        CHECK(cudaGetLastError());
        kernelMs = timer.ms();
    }
    CHECK(cudaMemcpy(reach.data(), reachDevice, side * sizeof(unsigned), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(tableDevice));
    CHECK(cudaFree(reachDevice));
    if (turnedDevice) CHECK(cudaFree(turnedDevice));
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return reach;
}

std::vector<GpuKernelInfo> roundTripReachKernelInfos() {
    return {
        kernelOccupancy("round_trip_reach", "reachTurnedKernel", reachTurnedKernel, REACH_BLOCK),
        kernelOccupancy("round_trip_reach", "reachColumnKernel", reachColumnKernel, REACH_BLOCK),
    };
}
