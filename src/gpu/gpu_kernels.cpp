#include "gpu_kernel_info.h"

#include "argmin_rows.h"
#include "eta_batch.h"
#include "travel_table.h"

std::vector<GpuKernelInfo> gpuKernelInfos() {
    std::vector<GpuKernelInfo> all{travelTableKernelInfo()};
    for (const GpuKernelInfo& k : etaBatchKernelInfos()) all.push_back(k);
    all.push_back(argminRowsKernelInfo());
    return all;
}
