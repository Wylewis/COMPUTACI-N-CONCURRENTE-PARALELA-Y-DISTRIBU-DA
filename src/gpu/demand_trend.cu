#include "demand_trend.h"

#include "cuda_compat.h"
#include "gpu_occupancy.h"
#include "gpu_timing.h"

constexpr unsigned TREND_BLOCK = 256;
constexpr unsigned TREND_HALO = 2 * kTrendRadius;

__constant__ float TREND_WEIGHTS[kTrendDim];

__global__ void trendGlobalKernel(const float* counts, float* out, int n) {
    const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (i >= n) return;
    float sum = 0.0f;
    for (int k = -kTrendRadius; k <= kTrendRadius; ++k) {
        const int j = i + k;
        if (j < 0 || j >= n) continue;
        sum += TREND_WEIGHTS[k + kTrendRadius] * counts[j];
    }
    out[i] = sum;
}

__global__ void trendTiledKernel(const float* counts, float* out, int n) {
    __shared__ float tile[TREND_BLOCK + TREND_HALO];

    const unsigned t = threadIdx.x;
    const int first = static_cast<int>(blockIdx.x * blockDim.x) - kTrendRadius;
    const int mine = first + static_cast<int>(t);
    tile[t] = mine >= 0 && mine < n ? counts[mine] : 0.0f;
    if (t < TREND_HALO) {
        const int extra = first + static_cast<int>(TREND_BLOCK + t);
        tile[TREND_BLOCK + t] = extra >= 0 && extra < n ? counts[extra] : 0.0f;
    }
    __syncthreads();

    const int i = static_cast<int>(blockIdx.x * blockDim.x + t);
    if (i >= n) return;
    float sum = 0.0f;
    for (int k = 0; k < kTrendDim; ++k) sum += TREND_WEIGHTS[k] * tile[t + static_cast<unsigned>(k)];
    out[i] = sum;
}

std::vector<float> trendGpu(const float* counts, std::size_t n, const float* weights, TrendKernel kernel, GpuTimes* times) {
    Timer total;
    std::vector<float> out(n, 0.0f);
    if (n == 0) return out;

    float *in = nullptr, *smoothed = nullptr;
    CHECK(cudaMalloc(&in, n * sizeof(float)));
    CHECK(cudaMalloc(&smoothed, n * sizeof(float)));
    CHECK(cudaMemcpy(in, counts, n * sizeof(float), cudaMemcpyHostToDevice));
    CHECK(cudaMemcpyToSymbol(TREND_WEIGHTS, weights, static_cast<std::size_t>(kTrendDim) * sizeof(float)));
    const dim3 blocks(cdiv(static_cast<unsigned>(n), TREND_BLOCK));
    double kernelMs = 0.0;
    {
        DeviceTimer timer;
        if (kernel == TrendKernel::Tiled)
            launch(trendTiledKernel, blocks, dim3(TREND_BLOCK), static_cast<const float*>(in), smoothed, static_cast<int>(n));
        else
            launch(trendGlobalKernel, blocks, dim3(TREND_BLOCK), static_cast<const float*>(in), smoothed, static_cast<int>(n));
        CHECK(cudaGetLastError());
        kernelMs = timer.ms();
    }
    CHECK(cudaMemcpy(out.data(), smoothed, n * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(in));
    CHECK(cudaFree(smoothed));
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return out;
}

std::vector<GpuKernelInfo> demandTrendKernelInfos() {
    return {
        kernelOccupancy("demand_trend", "trendGlobalKernel", trendGlobalKernel, TREND_BLOCK),
        kernelOccupancy("demand_trend", "trendTiledKernel", trendTiledKernel, TREND_BLOCK),
    };
}
