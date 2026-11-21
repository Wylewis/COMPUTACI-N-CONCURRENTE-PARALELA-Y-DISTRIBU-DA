#include "argmin_rows.h"

#include "parallel_for.h"

namespace {

void argminRange(const int* eta, std::size_t couriers, const int* rotation, EtaKey* best, std::size_t first,
                 std::size_t last) {
    for (std::size_t o = first; o < last; ++o) {
        const int rot = rotation ? rotation[o] : 0;
        const int* row = eta + o * couriers;
        EtaKey winner = etaKeyOf(kEtaUnavailable, 0, 0, 0);
        for (std::size_t c = 0; c < couriers; ++c) {
            const EtaKey key = etaKeyOf(row[c], static_cast<int>(c), rot, static_cast<int>(couriers));
            if (key < winner) winner = key;
        }
        best[o] = winner;
    }
}

}

std::vector<EtaKey> argminRowsCpu(const int* eta, std::size_t orders, std::size_t couriers, const int* rotation) {
    std::vector<EtaKey> best(orders);
    argminRange(eta, couriers, rotation, best.data(), 0, orders);
    return best;
}

std::vector<EtaKey> argminRowsCpuParallel(const int* eta, std::size_t orders, std::size_t couriers, ThreadPool& pool,
                                          const int* rotation) {
    std::vector<EtaKey> best(orders);
    EtaKey* out = best.data();
    parallelFor(pool, orders, [eta, couriers, rotation, out](std::size_t begin, std::size_t end, std::size_t) {
        argminRange(eta, couriers, rotation, out, begin, end);
    });
    return best;
}
