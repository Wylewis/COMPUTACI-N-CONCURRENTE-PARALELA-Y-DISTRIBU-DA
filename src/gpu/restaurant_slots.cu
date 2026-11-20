#include "restaurant_slots.h"

#include "cuda_compat.h"
#include "gpu_occupancy.h"
#include "gpu_timing.h"

constexpr unsigned SLOTS_BLOCK = 256;
constexpr unsigned SLOTS_PER_THREAD = 64;

__host__ __device__ inline int cellOnDevice(int restaurant, int ms, int slotMs, int slots) {
    int slot = ms <= 0 ? 0 : ms / slotMs;
    if (slot >= slots) slot = slots - 1;
    return restaurant * slots + slot;
}

__global__ void slotsAtomicKernel(const int* restaurant, const int* ms, unsigned n, int slotMs, int slots, unsigned* counts) {
    const unsigned base = blockIdx.x * blockDim.x * SLOTS_PER_THREAD;
    for (unsigned k = 0; k < SLOTS_PER_THREAD; ++k) {
        const unsigned i = base + k * blockDim.x + threadIdx.x;
        if (i < n) atomicAdd(&counts[cellOnDevice(restaurant[i], ms[i], slotMs, slots)], 1u);
    }
}

__global__ void slotsAggregatedKernel(const int* restaurant, const int* ms, unsigned n, int slotMs, int slots,
                                      unsigned* counts) {
    const unsigned base = (blockIdx.x * blockDim.x + threadIdx.x) * SLOTS_PER_THREAD;
    int last = -1;
    unsigned run = 0;
    for (unsigned k = 0; k < SLOTS_PER_THREAD; ++k) {
        const unsigned i = base + k;
        if (i >= n) break;
        const int cell = cellOnDevice(restaurant[i], ms[i], slotMs, slots);
        if (cell == last) {
            ++run;
        } else {
            if (run > 0) atomicAdd(&counts[last], run);
            last = cell;
            run = 1;
        }
    }
    if (run > 0) atomicAdd(&counts[last], run);
}

std::vector<unsigned> restaurantSlotsGpu(const int* restaurant, const int* ms, std::size_t n, int restaurants, int slots,
                                         SlotKernel kernel, GpuTimes* times, int slotMs) {
    Timer total;
    const std::size_t cells =
        static_cast<std::size_t>(restaurants > 0 ? restaurants : 0) * static_cast<std::size_t>(slots > 0 ? slots : 0);
    std::vector<unsigned> counts(cells, 0);
    if (n == 0 || cells == 0) return counts;

    int *restaurantDevice = nullptr, *msDevice = nullptr;
    unsigned* countsDevice = nullptr;
    CHECK(cudaMalloc(&restaurantDevice, n * sizeof(int)));
    CHECK(cudaMalloc(&msDevice, n * sizeof(int)));
    CHECK(cudaMalloc(&countsDevice, cells * sizeof(unsigned)));
    CHECK(cudaMemcpy(restaurantDevice, restaurant, n * sizeof(int), cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(msDevice, ms, n * sizeof(int), cudaMemcpyHostToDevice));
    CHECK(cudaMemset(countsDevice, 0, cells * sizeof(unsigned)));
    const dim3 grid(cdiv(static_cast<unsigned>(n), SLOTS_BLOCK * SLOTS_PER_THREAD));
    double kernelMs = 0.0;
    {
        DeviceTimer timer;
        if (kernel == SlotKernel::Aggregated)
            launch(slotsAggregatedKernel, grid, dim3(SLOTS_BLOCK), static_cast<const int*>(restaurantDevice),
                   static_cast<const int*>(msDevice), static_cast<unsigned>(n), slotMs, slots, countsDevice);
        else
            launch(slotsAtomicKernel, grid, dim3(SLOTS_BLOCK), static_cast<const int*>(restaurantDevice),
                   static_cast<const int*>(msDevice), static_cast<unsigned>(n), slotMs, slots, countsDevice);
        CHECK(cudaGetLastError());
        kernelMs = timer.ms();
    }
    CHECK(cudaMemcpy(counts.data(), countsDevice, cells * sizeof(unsigned), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(restaurantDevice));
    CHECK(cudaFree(msDevice));
    CHECK(cudaFree(countsDevice));
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return counts;
}

std::vector<GpuKernelInfo> restaurantSlotsKernelInfos() {
    return {
        kernelOccupancy("restaurant_slots", "slotsAtomicKernel", slotsAtomicKernel, SLOTS_BLOCK),
        kernelOccupancy("restaurant_slots", "slotsAggregatedKernel", slotsAggregatedKernel, SLOTS_BLOCK),
    };
}
