#include "delivery_kpis.h"

#include <algorithm>

#include "parallel_for.h"

namespace {

DeliveryKpis reduceRange(const int* ms, std::size_t begin, std::size_t end, int lateMs) {
    long long sum = 0;
    int highest = kKpiNoMax;
    unsigned late = 0;
    for (std::size_t i = begin; i < end; ++i) {
        const int v = ms[i];
        sum += v;
        highest = std::max(highest, v);
        late += v > lateMs ? 1u : 0u;
    }
    DeliveryKpis out;
    out.sumMs = sum;
    out.maxMs = highest;
    out.late = late;
    return out;
}

}

DeliveryKpis deliveryKpisCpu(const int* ms, std::size_t n, int lateMs) { return reduceRange(ms, 0, n, lateMs); }

DeliveryKpis deliveryKpisCpuParallel(const int* ms, std::size_t n, ThreadPool& pool, int lateMs) {
    std::vector<DeliveryKpis> perChunk(std::max<std::size_t>(1, pool.size()), noDeliveries());
    DeliveryKpis* all = perChunk.data();
    parallelFor(pool, n, [ms, all, lateMs](std::size_t begin, std::size_t end, std::size_t chunk) {
        all[chunk] = reduceRange(ms, begin, end, lateMs);
    });
    DeliveryKpis out = noDeliveries();
    for (const DeliveryKpis& part : perChunk) {
        out.sumMs += part.sumMs;
        out.maxMs = std::max(out.maxMs, part.maxMs);
        out.late += part.late;
    }
    return out;
}
