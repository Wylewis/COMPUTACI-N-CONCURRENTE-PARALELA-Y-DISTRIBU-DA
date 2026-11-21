#include "restaurant_slots.h"

#include <algorithm>

#include "parallel_for.h"

namespace {

std::size_t cellsOf(int restaurants, int slots) {
    return static_cast<std::size_t>(restaurants > 0 ? restaurants : 0) * static_cast<std::size_t>(slots > 0 ? slots : 0);
}

}

std::vector<unsigned> restaurantSlotsCpu(const int* restaurant, const int* ms, std::size_t n, int restaurants, int slots,
                                         int slotMs) {
    std::vector<unsigned> counts(cellsOf(restaurants, slots), 0);
    if (counts.empty()) return counts;
    for (std::size_t i = 0; i < n; ++i) ++counts[static_cast<std::size_t>(demandCell(restaurant[i], ms[i], slotMs, slots))];
    return counts;
}

std::vector<unsigned> restaurantSlotsCpuParallel(const int* restaurant, const int* ms, std::size_t n, int restaurants,
                                                 int slots, ThreadPool& pool, int slotMs) {
    const std::size_t cells = cellsOf(restaurants, slots);
    std::vector<unsigned> counts(cells, 0);
    if (cells == 0) return counts;
    const std::size_t chunks = std::max<std::size_t>(1, pool.size());
    std::vector<unsigned> perChunk(chunks * cells, 0);
    unsigned* all = perChunk.data();
    parallelFor(pool, n, [restaurant, ms, all, cells, slots, slotMs](std::size_t begin, std::size_t end, std::size_t chunk) {
        unsigned* mine = all + chunk * cells;
        for (std::size_t i = begin; i < end; ++i) ++mine[static_cast<std::size_t>(demandCell(restaurant[i], ms[i], slotMs, slots))];
    });
    for (std::size_t c = 0; c < chunks; ++c)
        for (std::size_t b = 0; b < cells; ++b) counts[b] += perChunk[c * cells + b];
    return counts;
}

BusiestSlot busiestSlot(const std::vector<unsigned>& counts, int slots) {
    BusiestSlot busiest;
    if (slots <= 0) return busiest;
    for (std::size_t b = 0; b < counts.size(); ++b) {
        if (counts[b] > busiest.orders) {
            busiest.orders = counts[b];
            busiest.restaurant = static_cast<int>(b / static_cast<std::size_t>(slots));
            busiest.slot = static_cast<int>(b % static_cast<std::size_t>(slots));
        }
    }
    return busiest;
}
