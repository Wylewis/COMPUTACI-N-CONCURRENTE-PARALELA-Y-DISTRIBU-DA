#include "delivery_histogram.h"

#include "cuda_compat.h"
#include "gpu_occupancy.h"
#include "gpu_timing.h"

constexpr unsigned HIST_BLOCK = 256;
constexpr unsigned HIST_PER_THREAD = 64;

__host__ __device__ inline int binOnDevice(int ms, int binMs, int bins) {
    const int bin = ms <= 0 ? 0 : ms / binMs;
    return bin >= bins ? bins - 1 : bin;
}

__global__ void histogramAtomicKernel(const int* ms, unsigned n, int binMs, int bins, unsigned* counts) {
    const unsigned base = blockIdx.x * blockDim.x * HIST_PER_THREAD;
    for (unsigned k = 0; k < HIST_PER_THREAD; ++k) {
        const unsigned i = base + k * blockDim.x + threadIdx.x;
        if (i < n) atomicAdd(&counts[binOnDevice(ms[i], binMs, bins)], 1u);
    }
}

__global__ void histogramPrivatizedKernel(const int* ms, unsigned n, int binMs, int bins, unsigned* counts) {
    __shared__ unsigned local[kHistogramMaxBins];

    const unsigned t = threadIdx.x;
    for (unsigned b = t; b < static_cast<unsigned>(kHistogramMaxBins); b += blockDim.x) local[b] = 0;
    __syncthreads();

    const unsigned base = blockIdx.x * blockDim.x * HIST_PER_THREAD;
    for (unsigned k = 0; k < HIST_PER_THREAD; ++k) {
        const unsigned i = base + k * blockDim.x + t;
        if (i < n) atomicAdd(&local[binOnDevice(ms[i], binMs, bins)], 1u);
    }
    __syncthreads();

    for (unsigned b = t; b < static_cast<unsigned>(bins); b += blockDim.x)
        if (local[b] != 0) atomicAdd(&counts[b], local[b]);
}

std::vector<unsigned> deliveryHistogramGpu(const int* ms, std::size_t n, int bins, HistogramMode mode, GpuTimes* times,
                                           int binMs) {
    const int width = histogramBins(bins);
    std::vector<unsigned> counts(static_cast<std::size_t>(width), 0);
    if (n == 0) return counts;

    Timer total;
    int* device = nullptr;
    unsigned* countsDevice = nullptr;
    CHECK(cudaMalloc(&device, n * sizeof(int)));
    CHECK(cudaMalloc(&countsDevice, counts.size() * sizeof(unsigned)));
    CHECK(cudaMemcpy(device, ms, n * sizeof(int), cudaMemcpyHostToDevice));
    CHECK(cudaMemset(countsDevice, 0, counts.size() * sizeof(unsigned)));
    const dim3 grid(cdiv(static_cast<unsigned>(n), HIST_BLOCK * HIST_PER_THREAD));
    double kernelMs = 0.0;
    {
        DeviceTimer kernel;
        if (mode == HistogramMode::Privatized)
            launch(histogramPrivatizedKernel, grid, dim3(HIST_BLOCK), static_cast<const int*>(device),
                   static_cast<unsigned>(n), binMs, width, countsDevice);
        else
            launch(histogramAtomicKernel, grid, dim3(HIST_BLOCK), static_cast<const int*>(device), static_cast<unsigned>(n),
                   binMs, width, countsDevice);
        CHECK(cudaGetLastError());
        kernelMs = kernel.ms();
    }
    CHECK(cudaMemcpy(counts.data(), countsDevice, counts.size() * sizeof(unsigned), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(device));
    CHECK(cudaFree(countsDevice));
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return counts;
}

std::vector<GpuKernelInfo> deliveryHistogramKernelInfos() {
    return {
        kernelOccupancy("delivery_histogram", "histogramPrivatizedKernel", histogramPrivatizedKernel, HIST_BLOCK),
        kernelOccupancy("delivery_histogram", "histogramAtomicKernel", histogramAtomicKernel, HIST_BLOCK),
    };
}
