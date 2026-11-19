#include "gpu_kernel_info.h"

#include "travel_table.h"

std::vector<GpuKernelInfo> gpuKernelInfos() {
    return {
        travelTableKernelInfo(),
    };
}
