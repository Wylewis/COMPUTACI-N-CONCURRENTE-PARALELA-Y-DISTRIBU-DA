#include "gpu_kernel_info.h"

#include "argmin_rows.h"
#include "eta_batch.h"
#include "group_orders.h"
#include "travel_table.h"

std::vector<GpuKernelInfo> gpuKernelInfos() {
    std::vector<GpuKernelInfo> all{travelTableKernelInfo()};
    for (const GpuKernelInfo& k : etaBatchKernelInfos()) all.push_back(k);
    all.push_back(argminRowsKernelInfo());
    for (const GpuKernelInfo& k : groupOrdersKernelInfos()) all.push_back(k);
    return all;
}
