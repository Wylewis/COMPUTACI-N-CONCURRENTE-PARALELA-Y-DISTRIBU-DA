#include "demand_heatmap.h"

#include "cuda_compat.h"
#include "gpu_occupancy.h"
#include "gpu_timing.h"

constexpr unsigned HEAT_BLOCK = 16;

__constant__ float HEAT_WEIGHTS[kHeatDim * kHeatDim];

__global__ void smoothConstantKernel(const float* counts, float* out, int grid) {
    const int col = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int row = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (col >= grid || row >= grid) return;
    float sum = 0.0f;
    for (int dy = -kHeatRadius; dy <= kHeatRadius; ++dy) {
        const int r = row + dy;
        if (r < 0 || r >= grid) continue;
        for (int dx = -kHeatRadius; dx <= kHeatRadius; ++dx) {
            const int c = col + dx;
            if (c < 0 || c >= grid) continue;
            sum += HEAT_WEIGHTS[(dy + kHeatRadius) * kHeatDim + dx + kHeatRadius] * counts[r * grid + c];
        }
    }
    out[row * grid + col] = sum;
}

__global__ void smoothGlobalKernel(const float* counts, float* out, int grid, const float* weights) {
    const int col = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int row = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (col >= grid || row >= grid) return;
    float sum = 0.0f;
    for (int dy = -kHeatRadius; dy <= kHeatRadius; ++dy) {
        const int r = row + dy;
        if (r < 0 || r >= grid) continue;
        for (int dx = -kHeatRadius; dx <= kHeatRadius; ++dx) {
            const int c = col + dx;
            if (c < 0 || c >= grid) continue;
            sum += weights[(dy + kHeatRadius) * kHeatDim + dx + kHeatRadius] * counts[r * grid + c];
        }
    }
    out[row * grid + col] = sum;
}

std::vector<float> smoothGpu(const float* counts, int grid, const float* weights, HeatWeights where, GpuTimes* times) {
    const std::size_t cells = static_cast<std::size_t>(grid > 0 ? grid : 0) * static_cast<std::size_t>(grid > 0 ? grid : 0);
    Timer total;
    std::vector<float> out(cells, 0.0f);
    if (cells == 0) return out;

    const std::size_t weightBytes = static_cast<std::size_t>(kHeatDim * kHeatDim) * sizeof(float);
    float *in = nullptr, *smoothed = nullptr, *weightsDevice = nullptr;
    CHECK(cudaMalloc(&in, cells * sizeof(float)));
    CHECK(cudaMalloc(&smoothed, cells * sizeof(float)));
    CHECK(cudaMemcpy(in, counts, cells * sizeof(float), cudaMemcpyHostToDevice));
    if (where == HeatWeights::Constant) {
        CHECK(cudaMemcpyToSymbol(HEAT_WEIGHTS, weights, weightBytes));
    } else {
        CHECK(cudaMalloc(&weightsDevice, weightBytes));
        CHECK(cudaMemcpy(weightsDevice, weights, weightBytes, cudaMemcpyHostToDevice));
    }
    const unsigned side = cdiv(static_cast<unsigned>(grid), HEAT_BLOCK);
    const dim3 blocks(side, side), block(HEAT_BLOCK, HEAT_BLOCK);
    double kernelMs = 0.0;
    {
        DeviceTimer kernel;
        if (where == HeatWeights::Constant)
            launch(smoothConstantKernel, blocks, block, static_cast<const float*>(in), smoothed, grid);
        else
            launch(smoothGlobalKernel, blocks, block, static_cast<const float*>(in), smoothed, grid,
                   static_cast<const float*>(weightsDevice));
        CHECK(cudaGetLastError());
        kernelMs = kernel.ms();
    }
    CHECK(cudaMemcpy(out.data(), smoothed, cells * sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(in));
    CHECK(cudaFree(smoothed));
    if (weightsDevice) CHECK(cudaFree(weightsDevice));
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return out;
}

std::vector<GpuKernelInfo> demandHeatmapKernelInfos() {
    return {
        kernelOccupancy("demand_heatmap", "smoothConstantKernel", smoothConstantKernel, HEAT_BLOCK * HEAT_BLOCK),
        kernelOccupancy("demand_heatmap", "smoothGlobalKernel", smoothGlobalKernel, HEAT_BLOCK * HEAT_BLOCK),
    };
}
