#include "through_restaurant.h"

#include <algorithm>

#include "parallel_for.h"

namespace {

void countRows(const RestaurantLegs& legs, std::size_t begin, std::size_t end, unsigned* wins) {
    const std::size_t n = static_cast<std::size_t>(legs.nodes);
    const std::size_t restaurants = static_cast<std::size_t>(legs.restaurants);
    std::vector<int> best(n), who(n);
    for (std::size_t i = begin; i < end; ++i) {
        std::fill(best.begin(), best.end(), kUnreachableMs);
        std::fill(who.begin(), who.end(), -1);
        const int* toRow = legs.to.data() + i * restaurants;
        for (std::size_t r = 0; r < restaurants; ++r) {
            const int first = toRow[r];
            const int* fromRow = legs.from.data() + r * n;
            const int id = static_cast<int>(r);
            for (std::size_t j = 0; j < n; ++j) {
                const int through = first + fromRow[j];
                const bool better = through < best[j];
                best[j] = better ? through : best[j];
                who[j] = better ? id : who[j];
            }
        }
        for (std::size_t j = 0; j < n; ++j)
            if (who[j] >= 0) ++wins[static_cast<std::size_t>(who[j])];
    }
}

}

RestaurantLegs restaurantLegs(const std::vector<int>& tableMs, int nodes, const std::vector<int>& restaurantNodes) {
    RestaurantLegs legs;
    legs.nodes = nodes > 0 ? nodes : 0;
    legs.restaurants = static_cast<int>(restaurantNodes.size());
    const std::size_t n = static_cast<std::size_t>(legs.nodes);
    const std::size_t restaurants = restaurantNodes.size();
    legs.to.assign(n * restaurants, kUnreachableMs);
    legs.from.assign(restaurants * n, kUnreachableMs);
    for (std::size_t r = 0; r < restaurants; ++r) {
        const std::size_t at = static_cast<std::size_t>(restaurantNodes[r]);
        for (std::size_t i = 0; i < n; ++i) {
            legs.to[i * restaurants + r] = std::min(tableMs[i * n + at], kUnreachableMs);
            legs.from[r * n + i] = std::min(tableMs[at * n + i], kUnreachableMs);
        }
    }
    return legs;
}

std::vector<unsigned> throughRestaurantCpu(const RestaurantLegs& legs) {
    std::vector<unsigned> wins(static_cast<std::size_t>(legs.restaurants), 0);
    if (legs.nodes <= 0 || legs.restaurants <= 0) return wins;
    countRows(legs, 0, static_cast<std::size_t>(legs.nodes), wins.data());
    return wins;
}

std::vector<unsigned> throughRestaurantCpuParallel(const RestaurantLegs& legs, ThreadPool& pool) {
    const std::size_t restaurants = static_cast<std::size_t>(legs.restaurants);
    std::vector<unsigned> wins(restaurants, 0);
    if (legs.nodes <= 0 || legs.restaurants <= 0) return wins;
    const std::size_t chunks = std::max<std::size_t>(1, pool.size());
    std::vector<unsigned> perChunk(chunks * restaurants, 0);
    unsigned* all = perChunk.data();
    parallelFor(pool, static_cast<std::size_t>(legs.nodes), [&legs, all, restaurants](std::size_t begin, std::size_t end, std::size_t chunk) {
        countRows(legs, begin, end, all + chunk * restaurants);
    });
    for (std::size_t c = 0; c < chunks; ++c)
        for (std::size_t r = 0; r < restaurants; ++r) wins[r] += perChunk[c * restaurants + r];
    return wins;
}

std::size_t bestPlaced(const std::vector<unsigned>& wins) {
    return static_cast<std::size_t>(std::max_element(wins.begin(), wins.end()) - wins.begin());
}
