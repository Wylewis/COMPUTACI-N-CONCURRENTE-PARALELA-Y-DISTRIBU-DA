#include "delivery_kpis.h"

#include <utility>

#include "cuda_compat.h"
#include "gpu_occupancy.h"
#include "gpu_timing.h"

constexpr unsigned KPI_BLOCK = 256;

__host__ __device__ inline DeliveryKpis combined(const DeliveryKpis& a, const DeliveryKpis& b) {
    DeliveryKpis out;
    out.sumMs = a.sumMs + b.sumMs;
    out.maxMs = a.maxMs > b.maxMs ? a.maxMs : b.maxMs;
    out.late = a.late + b.late;
    return out;
}

__global__ void kpiReduceKernel(const int* ms, unsigned n, int lateMs, unsigned perThread, DeliveryKpis* partial) {
    __shared__ DeliveryKpis tree[KPI_BLOCK];

    const unsigned t = threadIdx.x;
    const unsigned base = blockIdx.x * blockDim.x * perThread;
    DeliveryKpis mine;
    mine.sumMs = 0;
    mine.maxMs = kKpiNoMax;
    mine.late = 0;
    for (unsigned k = 0; k < perThread; ++k) {
        const unsigned i = base + k * blockDim.x + t;
        if (i < n) {
            const int v = ms[i];
            mine.sumMs += v;
            mine.maxMs = v > mine.maxMs ? v : mine.maxMs;
            mine.late += v > lateMs ? 1u : 0u;
        }
    }
    tree[t] = mine;
    __syncthreads();
    for (unsigned stride = blockDim.x / 2; stride > 0; stride /= 2) {
        if (t < stride) tree[t] = combined(tree[t], tree[t + stride]);
        __syncthreads();
    }
    if (t == 0) partial[blockIdx.x] = tree[0];
}

__global__ void kpiCombineKernel(const DeliveryKpis* in, unsigned count, unsigned perThread, DeliveryKpis* out) {
    __shared__ DeliveryKpis tree[KPI_BLOCK];

    const unsigned t = threadIdx.x;
    const unsigned base = blockIdx.x * blockDim.x * perThread;
    DeliveryKpis mine;
    mine.sumMs = 0;
    mine.maxMs = kKpiNoMax;
    mine.late = 0;
    for (unsigned k = 0; k < perThread; ++k) {
        const unsigned i = base + k * blockDim.x + t;
        if (i < count) mine = combined(mine, in[i]);
    }
    tree[t] = mine;
    __syncthreads();
    for (unsigned stride = blockDim.x / 2; stride > 0; stride /= 2) {
        if (t < stride) tree[t] = combined(tree[t], tree[t + stride]);
        __syncthreads();
    }
    if (t == 0) out[blockIdx.x] = tree[0];
}

DeliveryKpis deliveryKpisGpu(const int* ms, std::size_t n, int lateMs, unsigned perThread, GpuTimes* times, int* launches) {
    DeliveryKpis result = noDeliveries();
    if (launches) *launches = 0;
    if (n == 0) return result;
    const unsigned each = perThread < 1 ? 1 : perThread;

    Timer total;
    const unsigned perBlock = KPI_BLOCK * each;
    unsigned count = cdiv(static_cast<unsigned>(n), perBlock);
    int* device = nullptr;
    DeliveryKpis *current = nullptr, *next = nullptr;
    CHECK(cudaMalloc(&device, n * sizeof(int)));
    CHECK(cudaMalloc(&current, count * sizeof(DeliveryKpis)));
    CHECK(cudaMalloc(&next, cdiv(count, perBlock) * sizeof(DeliveryKpis)));
    CHECK(cudaMemcpy(device, ms, n * sizeof(int), cudaMemcpyHostToDevice));
    int done = 0;
    double kernelMs = 0.0;
    {
        DeviceTimer kernel;
        launch(kpiReduceKernel, dim3(count), dim3(KPI_BLOCK), static_cast<const int*>(device), static_cast<unsigned>(n),
               lateMs, each, current);
        CHECK(cudaGetLastError());
        ++done;
        while (count > 1) {
            const unsigned blocks = cdiv(count, perBlock);
            launch(kpiCombineKernel, dim3(blocks), dim3(KPI_BLOCK), static_cast<const DeliveryKpis*>(current), count, each,
                   next);
            CHECK(cudaGetLastError());
            std::swap(current, next);
            count = blocks;
            ++done;
        }
        kernelMs = kernel.ms();
    }
    CHECK(cudaMemcpy(&result, current, sizeof(DeliveryKpis), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(device));
    CHECK(cudaFree(current));
    CHECK(cudaFree(next));
    if (launches) *launches = done;
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return result;
}

std::vector<GpuKernelInfo> deliveryKpisKernelInfos() {
    return {
        kernelOccupancy("delivery_kpis", "kpiReduceKernel", kpiReduceKernel, KPI_BLOCK),
        kernelOccupancy("delivery_kpis", "kpiCombineKernel", kpiCombineKernel, KPI_BLOCK),
    };
}
