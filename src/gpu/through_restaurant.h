#pragma once

#include <cstddef>
#include <vector>

#include "gpu_kernel_info.h"
#include "gpu_timing_types.h"
#include "thread_pool.h"
#include "travel_table.h"

enum class ThroughKernel { Tiled, Global };

struct RestaurantLegs {
    int nodes = 0;
    int restaurants = 0;
    std::vector<int> to;
    std::vector<int> from;
};

RestaurantLegs restaurantLegs(const std::vector<int>& tableMs, int nodes, const std::vector<int>& restaurantNodes);

std::vector<unsigned> throughRestaurantCpu(const RestaurantLegs& legs);
std::vector<unsigned> throughRestaurantCpuParallel(const RestaurantLegs& legs, ThreadPool& pool);
std::vector<unsigned> throughRestaurantGpu(const RestaurantLegs& legs, ThroughKernel kernel = ThroughKernel::Tiled,
                                           GpuTimes* times = nullptr);

std::size_t bestPlaced(const std::vector<unsigned>& wins);

std::vector<GpuKernelInfo> throughRestaurantKernelInfos();
