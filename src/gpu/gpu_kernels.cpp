#include "gpu_kernel_info.h"

#include "argmin_rows.h"
#include "backlog_curve.h"
#include "coverage_field.h"
#include "delivery_histogram.h"
#include "delivery_kpis.h"
#include "demand_heatmap.h"
#include "demand_trend.h"
#include "eta_batch.h"
#include "group_orders.h"
#include "response_field.h"
#include "round_trip_reach.h"
#include "through_restaurant.h"
#include "restaurant_slots.h"
#include "travel_table.h"

std::vector<GpuKernelInfo> gpuKernelInfos() {
    std::vector<GpuKernelInfo> all{travelTableKernelInfo()};
    for (const GpuKernelInfo& k : etaBatchKernelInfos()) all.push_back(k);
    all.push_back(argminRowsKernelInfo());
    for (const GpuKernelInfo& k : groupOrdersKernelInfos()) all.push_back(k);
    for (const GpuKernelInfo& k : deliveryHistogramKernelInfos()) all.push_back(k);
    for (const GpuKernelInfo& k : demandHeatmapKernelInfos()) all.push_back(k);
    for (const GpuKernelInfo& k : coverageFieldKernelInfos()) all.push_back(k);
    for (const GpuKernelInfo& k : deliveryKpisKernelInfos()) all.push_back(k);
    for (const GpuKernelInfo& k : backlogCurveKernelInfos()) all.push_back(k);
    for (const GpuKernelInfo& k : restaurantSlotsKernelInfos()) all.push_back(k);
    for (const GpuKernelInfo& k : demandTrendKernelInfos()) all.push_back(k);
    for (const GpuKernelInfo& k : responseFieldKernelInfos()) all.push_back(k);
    for (const GpuKernelInfo& k : throughRestaurantKernelInfos()) all.push_back(k);
    for (const GpuKernelInfo& k : roundTripReachKernelInfos()) all.push_back(k);
    return all;
}
