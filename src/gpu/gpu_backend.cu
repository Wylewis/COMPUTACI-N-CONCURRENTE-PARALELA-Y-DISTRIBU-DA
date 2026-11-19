#include "gpu_backend.h"

#include <vector>

#include "cuda_compat.h"

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

bool gpuSelfTest(int n) {
    if (n <= 0) return true;
    std::vector<int> a(static_cast<std::size_t>(n)), b(a.size()), out(a.size(), -1);
    for (int i = 0; i < n; ++i) a[static_cast<std::size_t>(i)] = i, b[static_cast<std::size_t>(i)] = 2 * i + 1;
    const std::size_t bytes = a.size() * sizeof(int);
    int *da = nullptr, *db = nullptr, *dout = nullptr;
    CHECK(cudaMalloc(&da, bytes));
    CHECK(cudaMalloc(&db, bytes));
    CHECK(cudaMalloc(&dout, bytes));
    CHECK(cudaMemcpy(da, a.data(), bytes, cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(db, b.data(), bytes, cudaMemcpyHostToDevice));
    const unsigned block = 64;
    launch(selfTestKernel, dim3(cdiv(static_cast<unsigned>(n), block)), dim3(block), da, db, dout, n);
    CHECK(cudaGetLastError());
    CHECK(cudaDeviceSynchronize());
    CHECK(cudaMemcpy(out.data(), dout, bytes, cudaMemcpyDeviceToHost));
    CHECK(cudaFree(da));
    CHECK(cudaFree(db));
    CHECK(cudaFree(dout));
    for (int i = 0; i < n; ++i)
        if (out[static_cast<std::size_t>(i)] != 3 * i + 1) return false;
    return true;
}
