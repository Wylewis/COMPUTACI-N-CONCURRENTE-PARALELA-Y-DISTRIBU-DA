#include "gpu_backend.h"

#include <vector>

#include "cuda_compat.h"
#include "gpu_timing.h"

__global__ void selfTestKernel(const int* a, const int* b, int* out, int n) {
    const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (i < n) out[i] = a[i] + b[i];
}

GpuDeviceInfo gpuDeviceInfo() {
    GpuDeviceInfo info;
    info.backend = CUDA_BACKEND;
    info.realGpu = BY_BACKEND(true, false);
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess || count < 1) return info;
    cudaDeviceProp p;
    if (cudaGetDeviceProperties(&p, 0) != cudaSuccess) return info;
    info.name = p.name;
    info.multiprocessors = p.multiProcessorCount;
    info.maxThreadsPerBlock = p.maxThreadsPerBlock;
    info.warpSize = p.warpSize;
    info.globalMemBytes = p.totalGlobalMem;
    info.sharedMemPerBlock = p.sharedMemPerBlock;
    return info;
}

GpuTimes gpuVectorAdd(const int* a, const int* b, int* out, int n) {
    GpuTimes times;
    if (n <= 0) return times;
    Timer total;
    const std::size_t bytes = static_cast<std::size_t>(n) * sizeof(int);
    int *da = nullptr, *db = nullptr, *dout = nullptr;
    CHECK(cudaMalloc(&da, bytes));
    CHECK(cudaMalloc(&db, bytes));
    CHECK(cudaMalloc(&dout, bytes));
    CHECK(cudaMemcpy(da, a, bytes, cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(db, b, bytes, cudaMemcpyHostToDevice));
    const unsigned block = 256;
    {
        DeviceTimer kernel;
        launch(selfTestKernel, dim3(cdiv(static_cast<unsigned>(n), block)), dim3(block), da, db, dout, n);
        CHECK(cudaGetLastError());
        times.kernelMs = kernel.ms();
    }
    CHECK(cudaMemcpy(out, dout, bytes, cudaMemcpyDeviceToHost));
    CHECK(cudaFree(da));
    CHECK(cudaFree(db));
    CHECK(cudaFree(dout));
    times.totalMs = total.ms();
    return times;
}

GpuWake gpuWake(double capMs) {
    GpuWake wake;
    if (!BY_BACKEND(true, false)) return wake;
    static int *a = nullptr, *b = nullptr, *out = nullptr;
    static double best = 0.0;
    const int n = 1 << 22;
    const std::size_t bytes = static_cast<std::size_t>(n) * sizeof(int);
    const bool first = a == nullptr;
    if (first) {
        CHECK(cudaMalloc(&a, bytes));
        CHECK(cudaMalloc(&b, bytes));
        CHECK(cudaMalloc(&out, bytes));
        CHECK(cudaMemset(a, 0, bytes));
        CHECK(cudaMemset(b, 0, bytes));
    }
    const unsigned block = 256;
    const double calibrationMs = 1000.0;
    Timer spent;
    int inRegime = 0;
    wake.steady = false;
    while (spent.ms() < capMs) {
        double ms = 0.0;
        {
            DeviceTimer kernel;
            launch(selfTestKernel, dim3(cdiv(static_cast<unsigned>(n), block)), dim3(block), static_cast<const int*>(a),
                   static_cast<const int*>(b), out, n);
            CHECK(cudaGetLastError());
            ms = kernel.ms();
        }
        if (best == 0.0 || ms < best) best = ms;
        inRegime = ms <= 1.25 * best ? inRegime + 1 : 0;
        if (inRegime >= 3 && (!first || spent.ms() >= calibrationMs)) {
            wake.steady = true;
            break;
        }
    }
    wake.spentMs = spent.ms();
    return wake;
}

bool gpuSelfTest(int n) {
    if (n <= 0) return true;
    std::vector<int> a(static_cast<std::size_t>(n)), b(a.size()), out(a.size(), -1);
    for (int i = 0; i < n; ++i) a[static_cast<std::size_t>(i)] = i, b[static_cast<std::size_t>(i)] = 2 * i + 1;
    gpuVectorAdd(a.data(), b.data(), out.data(), n);
    for (int i = 0; i < n; ++i)
        if (out[static_cast<std::size_t>(i)] != 3 * i + 1) return false;
    return true;
}
