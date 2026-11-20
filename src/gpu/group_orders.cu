#include "group_orders.h"

#include "cuda_compat.h"
#include "gpu_occupancy.h"
#include "gpu_timing.h"

constexpr unsigned SCAN_SECTION = 256;
constexpr unsigned GROUP_BLOCK = 256;

__global__ void countGroupsKernel(const int* group, int orders, int* counts) {
    const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (i < orders) atomicAdd(&counts[group[i]], 1);
}

__global__ void scanSectionKernel(const int* in, int n, int* out, int* sums) {
    __shared__ int section[SCAN_SECTION];

    const unsigned t = threadIdx.x;
    const int i = static_cast<int>(blockIdx.x * SCAN_SECTION + t);
    section[t] = i < n ? in[i] : 0;
    for (unsigned stride = 1; stride < SCAN_SECTION; stride *= 2) {
        __syncthreads();
        int value = section[t];
        if (t >= stride) value += section[t - stride];
        __syncthreads();
        section[t] = value;
    }
    __syncthreads();
    if (i < n) out[i] = t > 0 ? section[t - 1] : 0;
    if (t == SCAN_SECTION - 1) sums[blockIdx.x] = section[t];
}

__global__ void addOffsetsKernel(int* out, int n, const int* offsets) {
    const int i = static_cast<int>(blockIdx.x * SCAN_SECTION + threadIdx.x);
    if (i < n) out[i] += offsets[blockIdx.x];
}

__global__ void scatterOrdersKernel(const int* group, int orders, const int* start, int* cursor, int* grouped) {
    const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (i >= orders) return;
    const int g = group[i];
    grouped[start[g] + atomicAdd(&cursor[g], 1)] = i;
}

namespace {

void exclusiveScanOnDevice(const int* in, int n, int* out) {
    if (n <= 0) return;
    const unsigned sections = cdiv(static_cast<unsigned>(n), SCAN_SECTION);
    int* sums = nullptr;
    CHECK(cudaMalloc(&sums, sections * sizeof(int)));
    launch(scanSectionKernel, dim3(sections), dim3(SCAN_SECTION), in, n, out, sums);
    CHECK(cudaGetLastError());
    if (sections > 1) {
        int* offsets = nullptr;
        CHECK(cudaMalloc(&offsets, sections * sizeof(int)));
        exclusiveScanOnDevice(sums, static_cast<int>(sections), offsets);
        launch(addOffsetsKernel, dim3(sections), dim3(SCAN_SECTION), out, n, static_cast<const int*>(offsets));
        CHECK(cudaGetLastError());
        CHECK(cudaFree(offsets));
    }
    CHECK(cudaFree(sums));
}

}

std::vector<int> exclusiveScanGpu(const std::vector<int>& values, GpuTimes* times) {
    std::vector<int> padded(values);
    padded.push_back(0);
    std::vector<int> out(padded.size(), 0);
    Timer total;
    const int n = static_cast<int>(padded.size());
    int *in = nullptr, *scanned = nullptr;
    CHECK(cudaMalloc(&in, padded.size() * sizeof(int)));
    CHECK(cudaMalloc(&scanned, padded.size() * sizeof(int)));
    CHECK(cudaMemcpy(in, padded.data(), padded.size() * sizeof(int), cudaMemcpyHostToDevice));
    double kernelMs = 0.0;
    {
        DeviceTimer kernel;
        exclusiveScanOnDevice(in, n, scanned);
        kernelMs = kernel.ms();
    }
    CHECK(cudaMemcpy(out.data(), scanned, out.size() * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(in));
    CHECK(cudaFree(scanned));
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return out;
}

OrderGroups groupOrdersGpu(const int* group, std::size_t orders, int groups, GpuTimes* times) {
    OrderGroups out;
    const std::size_t bins = static_cast<std::size_t>(groups > 0 ? groups : 0) + 1;
    out.start.assign(bins, 0);
    out.order.assign(orders, 0);
    if (orders == 0 || groups <= 0) return out;

    Timer total;
    int *groupDevice = nullptr, *counts = nullptr, *start = nullptr, *cursor = nullptr, *grouped = nullptr;
    CHECK(cudaMalloc(&groupDevice, orders * sizeof(int)));
    CHECK(cudaMalloc(&counts, bins * sizeof(int)));
    CHECK(cudaMalloc(&start, bins * sizeof(int)));
    CHECK(cudaMalloc(&cursor, bins * sizeof(int)));
    CHECK(cudaMalloc(&grouped, orders * sizeof(int)));
    CHECK(cudaMemcpy(groupDevice, group, orders * sizeof(int), cudaMemcpyHostToDevice));
    CHECK(cudaMemset(counts, 0, bins * sizeof(int)));
    CHECK(cudaMemset(cursor, 0, bins * sizeof(int)));

    const dim3 grid(cdiv(static_cast<unsigned>(orders), GROUP_BLOCK));
    const int ordersCount = static_cast<int>(orders);
    double kernelMs = 0.0;
    {
        DeviceTimer kernel;
        launch(countGroupsKernel, grid, dim3(GROUP_BLOCK), static_cast<const int*>(groupDevice), ordersCount, counts);
        CHECK(cudaGetLastError());
        exclusiveScanOnDevice(counts, static_cast<int>(bins), start);
        launch(scatterOrdersKernel, grid, dim3(GROUP_BLOCK), static_cast<const int*>(groupDevice), ordersCount,
               static_cast<const int*>(start), cursor, grouped);
        CHECK(cudaGetLastError());
        kernelMs = kernel.ms();
    }
    CHECK(cudaMemcpy(out.start.data(), start, bins * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(cudaMemcpy(out.order.data(), grouped, orders * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(groupDevice));
    CHECK(cudaFree(counts));
    CHECK(cudaFree(start));
    CHECK(cudaFree(cursor));
    CHECK(cudaFree(grouped));
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return out;
}

std::vector<GpuKernelInfo> groupOrdersKernelInfos() {
    return {
        kernelOccupancy("group_orders", "countGroupsKernel", countGroupsKernel, GROUP_BLOCK),
        kernelOccupancy("group_orders", "scanSectionKernel", scanSectionKernel, SCAN_SECTION),
        kernelOccupancy("group_orders", "addOffsetsKernel", addOffsetsKernel, SCAN_SECTION),
        kernelOccupancy("group_orders", "scatterOrdersKernel", scatterOrdersKernel, GROUP_BLOCK),
    };
}
