#include "travel_table.h"

#include "cuda_compat.h"
#include "gpu_timing.h"

constexpr int TRAVEL_TILE = 16;

__global__ void minPlusTiledKernel(const int* A, int* C, int n, int* changed) {
    __shared__ int As[TRAVEL_TILE][TRAVEL_TILE];
    __shared__ int Bs[TRAVEL_TILE][TRAVEL_TILE];

    const int tx = static_cast<int>(threadIdx.x);
    const int ty = static_cast<int>(threadIdx.y);
    const int row = static_cast<int>(blockIdx.y) * TRAVEL_TILE + ty;
    const int col = static_cast<int>(blockIdx.x) * TRAVEL_TILE + tx;

    int best = kUnreachableMs;
    const int phases = (n + TRAVEL_TILE - 1) / TRAVEL_TILE;
    for (int ph = 0; ph < phases; ++ph) {
        const int aCol = ph * TRAVEL_TILE + tx;
        const int bRow = ph * TRAVEL_TILE + ty;
        As[ty][tx] = (row < n && aCol < n) ? A[row * n + aCol] : kUnreachableMs;
        Bs[ty][tx] = (bRow < n && col < n) ? A[bRow * n + col] : kUnreachableMs;
        __syncthreads();
        for (int k = 0; k < TRAVEL_TILE; ++k) {
            const int through = As[ty][k] + Bs[k][tx];
            if (through < best) best = through;
        }
        __syncthreads();
    }

    if (row < n && col < n) {
        const int previous = A[row * n + col];
        C[row * n + col] = best;
        if (best != previous) {
            cuda::atomic_ref<int, cuda::thread_scope_device> flag(*changed);
            if (flag.load(cuda::memory_order_relaxed) == 0) flag.store(1, cuda::memory_order_relaxed);
        }
    }
}

std::vector<int> travelTableGpu(const CsrGraph& g, GpuTimes* times, int* squarings) {
    const std::size_t n = static_cast<std::size_t>(g.n);
    std::vector<int> table(n * n, kUnreachableMs);
    if (squarings) *squarings = 0;
    if (n == 0) return table;
    for (int u = 0; u < g.n; ++u) {
        table[static_cast<std::size_t>(u) * n + static_cast<std::size_t>(u)] = 0;
        for (int e = g.rowStart[static_cast<std::size_t>(u)]; e < g.rowStart[static_cast<std::size_t>(u) + 1]; ++e) {
            int& cell = table[static_cast<std::size_t>(u) * n + static_cast<std::size_t>(g.to[static_cast<std::size_t>(e)])];
            if (g.weightMs[static_cast<std::size_t>(e)] < cell) cell = g.weightMs[static_cast<std::size_t>(e)];
        }
    }

    Timer total;
    const std::size_t bytes = n * n * sizeof(int);
    int *current = nullptr, *next = nullptr, *changed = nullptr;
    CHECK(cudaMalloc(&current, bytes));
    CHECK(cudaMalloc(&next, bytes));
    CHECK(cudaMalloc(&changed, sizeof(int)));
    CHECK(cudaMemcpy(current, table.data(), bytes, cudaMemcpyHostToDevice));

    const unsigned blocksPerSide = cdiv(static_cast<unsigned>(g.n), TRAVEL_TILE);
    const dim3 grid(blocksPerSide, blocksPerSide);
    const dim3 block(TRAVEL_TILE, TRAVEL_TILE);
    int rounds = 0;
    double kernelMs = 0.0;
    {
        DeviceTimer kernel;
        for (int reach = 1; reach < g.n; reach *= 2) {
            CHECK(cudaMemset(changed, 0, sizeof(int)));
            launch(minPlusTiledKernel, grid, block, current, next, g.n, changed);
            CHECK(cudaGetLastError());
            ++rounds;
            int flag = 0;
            CHECK(cudaMemcpy(&flag, changed, sizeof(int), cudaMemcpyDeviceToHost));
            int* swap = current;
            current = next;
            next = swap;
            if (flag == 0) break;
        }
        kernelMs = kernel.ms();
    }
    CHECK(cudaMemcpy(table.data(), current, bytes, cudaMemcpyDeviceToHost));
    CHECK(cudaFree(current));
    CHECK(cudaFree(next));
    CHECK(cudaFree(changed));
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    if (squarings) *squarings = rounds;
    return table;
}
