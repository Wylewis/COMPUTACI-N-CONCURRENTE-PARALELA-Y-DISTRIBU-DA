#include "eta_batch.h"

#include "parallel_for.h"

std::vector<CourierRecord> courierRecords(const EtaInput& in) {
    std::vector<CourierRecord> records(in.couriers());
    for (std::size_t c = 0; c < records.size(); ++c) {
        records[c] = CourierRecord{in.courierNode[c], in.courierAvailableMs[c], in.courierFree[c] ? 0 : 1, 1, 0, {0, 0, 0}};
    }
    return records;
}

namespace {

void etaRows(const TravelTable& table, const EtaInput& in, int* eta, std::size_t firstOrder, std::size_t lastOrder) {
    const int* data = table.data().data();
    const int n = table.size();
    const std::size_t couriers = in.couriers();
    for (std::size_t o = firstOrder; o < lastOrder; ++o) {
        const int restaurant = in.orderRestaurantNode[o];
        const int delivery = in.orderDeliveryNode[o];
        const int ready = in.orderReadyMs[o];
        int* row = eta + o * couriers;
        for (std::size_t c = 0; c < couriers; ++c) {
            row[c] = etaOf(data, n, in.courierNode[c], in.courierAvailableMs[c], in.courierFree[c], restaurant, delivery,
                           ready, in.nowMs);
        }
    }
}

}

void etaBatchCpu(const TravelTable& table, const EtaInput& in, int* eta) {
    etaRows(table, in, eta, 0, in.orders());
}

void etaBatchCpuParallel(const TravelTable& table, const EtaInput& in, int* eta, ThreadPool& pool) {
    parallelFor(pool, in.orders(), [&table, &in, eta](std::size_t begin, std::size_t end, std::size_t) {
        etaRows(table, in, eta, begin, end);
    });
}

EtaInput ordersPermuted(const EtaInput& in, const std::vector<int>& order) {
    EtaInput out = in;
    for (std::size_t k = 0; k < order.size() && k < in.orders(); ++k) {
        const std::size_t from = static_cast<std::size_t>(order[k]);
        out.orderRestaurantNode[k] = in.orderRestaurantNode[from];
        out.orderDeliveryNode[k] = in.orderDeliveryNode[from];
        out.orderReadyMs[k] = in.orderReadyMs[from];
    }
    return out;
}
