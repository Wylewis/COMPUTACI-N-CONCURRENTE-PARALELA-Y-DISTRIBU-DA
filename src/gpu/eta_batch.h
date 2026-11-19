#pragma once

#include <climits>
#include <cstddef>
#include <vector>

#include "gpu_kernel_info.h"
#include "gpu_timing_types.h"
#include "thread_pool.h"
#include "travel_table.h"

constexpr int kEtaPickupHandoverMs = 60'000;
constexpr int kEtaDeliveryHandoverMs = 60'000;
constexpr int kEtaUnavailable = INT_MAX;

struct EtaInput {
    std::vector<int> courierNode;
    std::vector<int> courierAvailableMs;
    std::vector<int> courierFree;
    std::vector<int> orderRestaurantNode;
    std::vector<int> orderDeliveryNode;
    std::vector<int> orderReadyMs;
    int nowMs = 0;

    std::size_t couriers() const { return courierNode.size(); }
    std::size_t orders() const { return orderRestaurantNode.size(); }
};

struct CourierRecord {
    int node;
    int availableAtMs;
    int bagCount;
    int bagCapacity;
    int broken;
    int reserved[3];
};

std::vector<CourierRecord> courierRecords(const EtaInput& in);

inline int etaOf(const int* table, int n, int courierNode, int courierAvailableMs, int courierFree, int restaurantNode,
                 int deliveryNode, int readyMs, int nowMs) {
    if (!courierFree) return kEtaUnavailable;
    const int toRestaurant = table[static_cast<std::size_t>(courierNode) * static_cast<std::size_t>(n) + restaurantNode];
    const int toCustomer = table[static_cast<std::size_t>(restaurantNode) * static_cast<std::size_t>(n) + deliveryNode];
    if (toRestaurant >= kUnreachableMs || toCustomer >= kUnreachableMs) return kEtaUnavailable;
    const int depart = courierAvailableMs > nowMs ? courierAvailableMs : nowMs;
    const int arrive = depart + toRestaurant;
    const int pickup = arrive > readyMs ? arrive : readyMs;
    return pickup + kEtaPickupHandoverMs + toCustomer + kEtaDeliveryHandoverMs;
}

void etaBatchCpu(const TravelTable& table, const EtaInput& in, int* eta);
void etaBatchCpuParallel(const TravelTable& table, const EtaInput& in, int* eta, ThreadPool& pool);

enum class EtaLayout { StructureOfArrays, ArrayOfStructures };

class EtaBatchGpu {
public:
    explicit EtaBatchGpu(const TravelTable& table);
    ~EtaBatchGpu();
    EtaBatchGpu(const EtaBatchGpu&) = delete;
    EtaBatchGpu& operator=(const EtaBatchGpu&) = delete;

    std::vector<int> run(const EtaInput& in, EtaLayout layout = EtaLayout::StructureOfArrays, GpuTimes* times = nullptr);

    double tableUploadMs() const { return tableUploadMs_; }
    std::size_t tableBytes() const { return tableBytes_; }

private:
    int* table_ = nullptr;
    int n_ = 0;
    std::size_t tableBytes_ = 0;
    double tableUploadMs_ = 0.0;
};

std::vector<GpuKernelInfo> etaBatchKernelInfos();
