#include "coverage_field.h"

#include <utility>

#include "cuda_compat.h"
#include "gpu_occupancy.h"
#include "gpu_timing.h"

constexpr unsigned COVER_BLOCK = 16;
constexpr int COVER_IN = 16;
constexpr int COVER_OUT = COVER_IN - 2;
constexpr unsigned CHECK_BLOCK = 256;
constexpr unsigned CHECK_PER_THREAD = 64;

__host__ __device__ inline int lowerOf(int a, int b) { return a < b ? a : b; }

__global__ void sweepBasicKernel(const int* in, int* out, const unsigned char* open, int grid, int stepX, int stepY) {
    const int col = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int row = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (col >= grid || row >= grid) return;
    const int p = row * grid + col;
    const int cur = in[p];
    int best = cur;
    if (open[p]) {
        const int left = col > 0 ? in[p - 1] : kCoverFar;
        const int right = col < grid - 1 ? in[p + 1] : kCoverFar;
        const int up = row > 0 ? in[p - grid] : kCoverFar;
        const int down = row < grid - 1 ? in[p + grid] : kCoverFar;
        best = lowerOf(best, lowerOf(lowerOf(left, right) + stepX, lowerOf(up, down) + stepY));
    }
    out[p] = best;
}

__global__ void sweepTiledKernel(const int* in, int* out, const unsigned char* open, int grid, int stepX, int stepY) {
    __shared__ int tile[COVER_IN][COVER_IN];

    const int tx = static_cast<int>(threadIdx.x), ty = static_cast<int>(threadIdx.y);
    const int col = static_cast<int>(blockIdx.x) * COVER_OUT + tx - 1;
    const int row = static_cast<int>(blockIdx.y) * COVER_OUT + ty - 1;
    const bool inside = col >= 0 && col < grid && row >= 0 && row < grid;
    tile[ty][tx] = inside ? in[row * grid + col] : kCoverFar;
    __syncthreads();

    if (inside && tx >= 1 && tx < COVER_IN - 1 && ty >= 1 && ty < COVER_IN - 1) {
        const int p = row * grid + col;
        const int cur = tile[ty][tx];
        int best = cur;
        if (open[p])
            best = lowerOf(best, lowerOf(lowerOf(tile[ty][tx - 1], tile[ty][tx + 1]) + stepX,
                                         lowerOf(tile[ty - 1][tx], tile[ty + 1][tx]) + stepY));
        out[p] = best;
    }
}

__global__ void countChangedKernel(const int* a, const int* b, unsigned cells, unsigned* changedBlocks) {
    __shared__ unsigned differs[CHECK_BLOCK];

    const unsigned t = threadIdx.x;
    const unsigned base = blockIdx.x * blockDim.x * CHECK_PER_THREAD;
    unsigned mine = 0;
    for (unsigned k = 0; k < CHECK_PER_THREAD; ++k) {
        const unsigned i = base + k * blockDim.x + t;
        if (i < cells && a[i] != b[i]) mine = 1;
    }
    differs[t] = mine;
    __syncthreads();
    for (unsigned stride = blockDim.x / 2; stride > 0; stride /= 2) {
        if (t < stride) differs[t] |= differs[t + stride];
        __syncthreads();
    }
    if (t == 0 && differs[0] != 0) atomicAdd(changedBlocks, 1u);
}

std::vector<int> coverageGpu(const CoverageMap& map, const int* seed, CoverKernel kernel, GpuTimes* times, int* sweeps) {
    const std::size_t cells = static_cast<std::size_t>(map.grid > 0 ? map.grid : 0) * static_cast<std::size_t>(map.grid > 0 ? map.grid : 0);
    Timer total;
    std::vector<int> out(cells, kCoverFar);
    if (sweeps) *sweeps = 0;
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

    const unsigned side = static_cast<unsigned>(map.grid);
    const dim3 block(COVER_BLOCK, COVER_BLOCK);
    const dim3 basicBlocks(cdiv(side, COVER_BLOCK), cdiv(side, COVER_BLOCK));
    const dim3 tiledBlocks(cdiv(side, static_cast<unsigned>(COVER_OUT)), cdiv(side, static_cast<unsigned>(COVER_OUT)));
    const dim3 checkBlocks(cdiv(static_cast<unsigned>(cells), CHECK_BLOCK * CHECK_PER_THREAD));
    int done = 0;
    double kernelMs = 0.0;
    {
        DeviceTimer loop;
        unsigned changedBlocks = 1;
        while (changedBlocks != 0) {
            for (int k = 0; k < kCoverCheckEvery; ++k) {
                if (kernel == CoverKernel::Tiled)
                    launch(sweepTiledKernel, tiledBlocks, block, static_cast<const int*>(current), next,
                           static_cast<const unsigned char*>(open), map.grid, map.stepXMs, map.stepYMs);
                else
                    launch(sweepBasicKernel, basicBlocks, block, static_cast<const int*>(current), next,
                           static_cast<const unsigned char*>(open), map.grid, map.stepXMs, map.stepYMs);
                CHECK(cudaGetLastError());
                std::swap(current, next);
                ++done;
            }
            CHECK(cudaMemset(changed, 0, sizeof(unsigned)));
            launch(countChangedKernel, checkBlocks, dim3(CHECK_BLOCK), static_cast<const int*>(current),
                   static_cast<const int*>(next), static_cast<unsigned>(cells), changed);
            CHECK(cudaGetLastError());
            CHECK(cudaMemcpy(&changedBlocks, changed, sizeof(unsigned), cudaMemcpyDeviceToHost));
        }
        kernelMs = loop.ms();
    }
    CHECK(cudaMemcpy(out.data(), current, cells * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(current));
    CHECK(cudaFree(next));
    CHECK(cudaFree(open));
    CHECK(cudaFree(changed));
    if (sweeps) *sweeps = done;
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return out;
}

std::vector<GpuKernelInfo> coverageFieldKernelInfos() {
    return {
        kernelOccupancy("coverage_field", "sweepBasicKernel", sweepBasicKernel, COVER_BLOCK * COVER_BLOCK),
        kernelOccupancy("coverage_field", "sweepTiledKernel", sweepTiledKernel, COVER_BLOCK * COVER_BLOCK),
        kernelOccupancy("coverage_field", "countChangedKernel", countChangedKernel, CHECK_BLOCK),
    };
}
