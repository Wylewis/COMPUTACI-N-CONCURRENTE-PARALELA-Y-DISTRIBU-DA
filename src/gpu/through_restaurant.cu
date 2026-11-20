#include "through_restaurant.h"

#include "cuda_compat.h"
#include "gpu_occupancy.h"
#include "gpu_timing.h"

constexpr int THROUGH_TILE = 16;

__global__ void throughTiledKernel(const int* to, const int* from, int n, int restaurants, unsigned* wins) {
    __shared__ int toTile[THROUGH_TILE][THROUGH_TILE];
    __shared__ int fromTile[THROUGH_TILE][THROUGH_TILE];

    const int tx = static_cast<int>(threadIdx.x);
    const int ty = static_cast<int>(threadIdx.y);
    const int row = static_cast<int>(blockIdx.y) * THROUGH_TILE + ty;
    const int col = static_cast<int>(blockIdx.x) * THROUGH_TILE + tx;

    int best = kUnreachableMs;
    int who = -1;
    const int phases = (restaurants + THROUGH_TILE - 1) / THROUGH_TILE;
    for (int ph = 0; ph < phases; ++ph) {
        const int first = ph * THROUGH_TILE;
        toTile[ty][tx] = (row < n && first + tx < restaurants) ? to[row * restaurants + first + tx] : kUnreachableMs;
        fromTile[ty][tx] = (first + ty < restaurants && col < n) ? from[(first + ty) * n + col] : kUnreachableMs;
        __syncthreads();
        for (int k = 0; k < THROUGH_TILE; ++k) {
            const int through = toTile[ty][k] + fromTile[k][tx];
            if (through < best) {
                best = through;
                who = first + k;
            }
        }
        __syncthreads();
    }
    if (row < n && col < n && who >= 0) atomicAdd(&wins[who], 1u);
}

__global__ void throughGlobalKernel(const int* to, const int* from, int n, int restaurants, unsigned* wins) {
    const int row = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    const int col = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (row >= n || col >= n) return;
    int best = kUnreachableMs;
    int who = -1;
    for (int r = 0; r < restaurants; ++r) {
        const int through = to[row * restaurants + r] + from[r * n + col];
        if (through < best) {
            best = through;
            who = r;
        }
    }
    if (who >= 0) atomicAdd(&wins[who], 1u);
}

std::vector<unsigned> throughRestaurantGpu(const RestaurantLegs& legs, ThroughKernel kernel, GpuTimes* times) {
    Timer total;
    const std::size_t restaurants = static_cast<std::size_t>(legs.restaurants > 0 ? legs.restaurants : 0);
    std::vector<unsigned> wins(restaurants, 0);
    if (legs.nodes <= 0 || restaurants == 0) return wins;

    const std::size_t n = static_cast<std::size_t>(legs.nodes);
    int *to = nullptr, *from = nullptr;
    unsigned* winsDevice = nullptr;
    CHECK(cudaMalloc(&to, n * restaurants * sizeof(int)));
    CHECK(cudaMalloc(&from, restaurants * n * sizeof(int)));
    CHECK(cudaMalloc(&winsDevice, restaurants * sizeof(unsigned)));
    CHECK(cudaMemcpy(to, legs.to.data(), n * restaurants * sizeof(int), cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(from, legs.from.data(), restaurants * n * sizeof(int), cudaMemcpyHostToDevice));
    CHECK(cudaMemset(winsDevice, 0, restaurants * sizeof(unsigned)));
    const unsigned side = cdiv(static_cast<unsigned>(legs.nodes), static_cast<unsigned>(THROUGH_TILE));
    const dim3 blocks(side, side), block(THROUGH_TILE, THROUGH_TILE);
    double kernelMs = 0.0;
    {
        DeviceTimer timer;
        if (kernel == ThroughKernel::Global)
            launch(throughGlobalKernel, blocks, block, static_cast<const int*>(to), static_cast<const int*>(from), legs.nodes,
                   legs.restaurants, winsDevice);
        else
            launch(throughTiledKernel, blocks, block, static_cast<const int*>(to), static_cast<const int*>(from), legs.nodes,
                   legs.restaurants, winsDevice);
        CHECK(cudaGetLastError());
        kernelMs = timer.ms();
    }
    CHECK(cudaMemcpy(wins.data(), winsDevice, restaurants * sizeof(unsigned), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(to));
    CHECK(cudaFree(from));
    CHECK(cudaFree(winsDevice));
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return wins;
}

std::vector<GpuKernelInfo> throughRestaurantKernelInfos() {
    return {
        kernelOccupancy("through_restaurant", "throughTiledKernel", throughTiledKernel, THROUGH_TILE * THROUGH_TILE),
        kernelOccupancy("through_restaurant", "throughGlobalKernel", throughGlobalKernel, THROUGH_TILE * THROUGH_TILE),
    };
}
