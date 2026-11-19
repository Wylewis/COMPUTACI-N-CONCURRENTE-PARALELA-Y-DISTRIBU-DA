#include "gpu_kernel_info.h"

#include "eta_batch.h"
#include "travel_table.h"

std::vector<GpuKernelInfo> gpuKernelInfos() {
    std::vector<GpuKernelInfo> all{travelTableKernelInfo()};
    for (const GpuKernelInfo& k : etaBatchKernelInfos()) all.push_back(k);
    return all;
}
