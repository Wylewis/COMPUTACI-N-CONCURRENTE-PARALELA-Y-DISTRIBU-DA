#include "response_field.h"

#include <utility>

#include "cuda_compat.h"
#include "gpu_occupancy.h"
#include "gpu_timing.h"

constexpr int MANY_OUT = 16;
constexpr int MANY_HALO = kResponseSweepsPerLaunch;
constexpr int MANY_TILE = MANY_OUT + 2 * MANY_HALO;
constexpr int MANY_CELLS = MANY_TILE * MANY_TILE;
constexpr int MANY_THREADS = MANY_OUT * MANY_OUT;
constexpr int MANY_PER_THREAD = MANY_CELLS / MANY_THREADS;

__host__ __device__ inline int lowestOf(int a, int b) { return a < b ? a : b; }

__global__ void sweepManyKernel(const int* in, int* out, const unsigned char* open, int grid, int stepX, int stepY) {
    __shared__ int even[MANY_CELLS];
    __shared__ int odd[MANY_CELLS];
    __shared__ unsigned char street[MANY_CELLS];

    const int t = static_cast<int>(threadIdx.y) * MANY_OUT + static_cast<int>(threadIdx.x);
    const int firstCol = static_cast<int>(blockIdx.x) * MANY_OUT - MANY_HALO;
    const int firstRow = static_cast<int>(blockIdx.y) * MANY_OUT - MANY_HALO;
    for (int k = 0; k < MANY_PER_THREAD; ++k) {
        const int cell = t + k * MANY_THREADS;
        const int row = firstRow + cell / MANY_TILE;
        const int col = firstCol + cell % MANY_TILE;
        const bool inside = row >= 0 && row < grid && col >= 0 && col < grid;
        even[cell] = inside ? in[row * grid + col] : kCoverFar;
        street[cell] = inside ? open[row * grid + col] : static_cast<unsigned char>(0);
    }
    __syncthreads();

    for (int sweep = 0; sweep < MANY_HALO; ++sweep) {
        const int* from = sweep % 2 == 0 ? even : odd;
        int* to = sweep % 2 == 0 ? odd : even;
        for (int k = 0; k < MANY_PER_THREAD; ++k) {
            const int cell = t + k * MANY_THREADS;
            const int tileRow = cell / MANY_TILE;
            const int tileCol = cell % MANY_TILE;
            int best = from[cell];
            if (street[cell]) {
                const int left = tileCol > 0 ? from[cell - 1] : kCoverFar;
                const int right = tileCol < MANY_TILE - 1 ? from[cell + 1] : kCoverFar;
                const int up = tileRow > 0 ? from[cell - MANY_TILE] : kCoverFar;
                const int down = tileRow < MANY_TILE - 1 ? from[cell + MANY_TILE] : kCoverFar;
                best = lowestOf(best, lowestOf(lowestOf(left, right) + stepX, lowestOf(up, down) + stepY));
            }
            to[cell] = best;
        }
        __syncthreads();
    }

    const int row = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    const int col = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (row < grid && col < grid) {
        const int* last = MANY_HALO % 2 == 0 ? even : odd;
        out[row * grid + col] = last[(static_cast<int>(threadIdx.y) + MANY_HALO) * MANY_TILE + static_cast<int>(threadIdx.x) + MANY_HALO];
    }
}

std::vector<int> responseFieldGpu(const CoverageMap& map, const int* seed, ResponseKernel kernel, GpuTimes* times, int* sweeps,
                                  int* launches) {
    if (kernel == ResponseKernel::OneSweep) {
        int done = 0;
        std::vector<int> field = coverageGpu(map, seed, CoverKernel::Basic, times, &done);
        if (sweeps) *sweeps = done;
        if (launches) *launches = done + done / kCoverCheckEvery;
        return field;
    }

    Timer total;
    const std::size_t cells = static_cast<std::size_t>(map.grid > 0 ? map.grid : 0) * static_cast<std::size_t>(map.grid > 0 ? map.grid : 0);
    std::vector<int> out(cells, kCoverFar);
    if (sweeps) *sweeps = 0;
    if (launches) *launches = 0;
    if (cells == 0) return out;

    int *current = nullptr, *next = nullptr;
    unsigned char* open = nullptr;
    unsigned* changed = nullptr;
    CHECK(cudaMalloc(&current, cells * sizeof(int)));
    CHECK(cudaMalloc(&next, cells * sizeof(int)));
    CHECK(cudaMalloc(&open, cells));
    CHECK(cudaMalloc(&changed, sizeof(unsigned)));
    CHECK(cudaMemcpy(current, seed, cells * sizeof(int), cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(open, map.open.data(), cells, cudaMemcpyHostToDevice));

    const unsigned side = cdiv(static_cast<unsigned>(map.grid), static_cast<unsigned>(MANY_OUT));
    const dim3 blocks(side, side), block(MANY_OUT, MANY_OUT);
    int done = 0, launched = 0;
    double kernelMs = 0.0;
    {
        DeviceTimer loop;
        unsigned changedBlocks = 1;
        while (changedBlocks != 0) {
            for (int k = 0; k < kCoverCheckEvery / kResponseSweepsPerLaunch; ++k) {
                launch(sweepManyKernel, blocks, block, static_cast<const int*>(current), next,
                       static_cast<const unsigned char*>(open), map.grid, map.stepXMs, map.stepYMs);
                CHECK(cudaGetLastError());
                std::swap(current, next);
                done += kResponseSweepsPerLaunch;
                ++launched;
            }
            changedBlocks = coverageChangedBlocks(current, next, cells, changed);
            ++launched;
        }
        kernelMs = loop.ms();
    }
    CHECK(cudaMemcpy(out.data(), current, cells * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(current));
    CHECK(cudaFree(next));
    CHECK(cudaFree(open));
    CHECK(cudaFree(changed));
    if (sweeps) *sweeps = done;
    if (launches) *launches = launched;
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return out;
}

std::vector<GpuKernelInfo> responseFieldKernelInfos() {
    return {kernelOccupancy("response_field", "sweepManyKernel", sweepManyKernel, MANY_THREADS)};
}
