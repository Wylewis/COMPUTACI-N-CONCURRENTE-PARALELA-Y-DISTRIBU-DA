#include "eta_batch.h"

#include "argmin_rows.h"

#include "cuda_compat.h"
#include "gpu_occupancy.h"
#include "gpu_timing.h"

constexpr unsigned ETA_BLOCK_X = 32;
constexpr unsigned ETA_BLOCK_Y = 8;

struct CourierArrays {
    const int* node;
    const int* availableAtMs;
    const int* free;
};

struct OrderArrays {
    const int* restaurantNode;
    const int* deliveryNode;
    const int* readyMs;
};

__host__ __device__ inline int etaFromTable(const int* table, int n, int courierNode, int available, int isFree,
                                             int restaurant, int delivery, int ready, int now) {
    if (!isFree) return kEtaUnavailable;
    const int toRestaurant = table[static_cast<size_t>(courierNode) * static_cast<size_t>(n) + restaurant];
    const int toCustomer = table[static_cast<size_t>(restaurant) * static_cast<size_t>(n) + delivery];
    if (toRestaurant >= kUnreachableMs || toCustomer >= kUnreachableMs) return kEtaUnavailable;
    const int depart = available > now ? available : now;
    const int arrive = depart + toRestaurant;
    const int pickup = arrive > ready ? arrive : ready;
    return pickup + kEtaPickupHandoverMs + toCustomer + kEtaDeliveryHandoverMs;
}

__global__ void etaSoaKernel(const int* table, int n, CourierArrays couriers, OrderArrays orders, int couriersCount,
                             int ordersCount, int now, int* eta) {
    const int c = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int o = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (c >= couriersCount || o >= ordersCount) return;
    eta[static_cast<size_t>(o) * static_cast<size_t>(couriersCount) + c] =
        etaFromTable(table, n, couriers.node[c], couriers.availableAtMs[c], couriers.free[c], orders.restaurantNode[o],
                     orders.deliveryNode[o], orders.readyMs[o], now);
}

__global__ void etaAosKernel(const int* table, int n, const CourierRecord* couriers, OrderArrays orders,
                             int couriersCount, int ordersCount, int now, int* eta) {
    const int c = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int o = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (c >= couriersCount || o >= ordersCount) return;
    const CourierRecord r = couriers[c];
    const int isFree = !r.broken && r.bagCount < r.bagCapacity;
    eta[static_cast<size_t>(o) * static_cast<size_t>(couriersCount) + c] =
        etaFromTable(table, n, r.node, r.availableAtMs, isFree, orders.restaurantNode[o], orders.deliveryNode[o],
                     orders.readyMs[o], now);
}

namespace {

template <class T>
T* toDevice(const T* host, size_t count) {
    T* device = nullptr;
    CHECK(cudaMalloc(&device, count * sizeof(T)));
    CHECK(cudaMemcpy(device, host, count * sizeof(T), cudaMemcpyHostToDevice));
    return device;
}

}

EtaBatchGpu::EtaBatchGpu(const TravelTable& table) : n_(table.size()) {
    if (n_ == 0) return;
    Timer upload;
    tableBytes_ = table.data().size() * sizeof(int);
    table_ = toDevice(table.data().data(), table.data().size());
    tableUploadMs_ = upload.ms();
}

EtaBatchGpu::~EtaBatchGpu() {
    if (table_) cudaFree(table_);
    if (matrix_) cudaFree(matrix_);
}

std::vector<int> EtaBatchGpu::run(const EtaInput& in, EtaLayout layout, GpuTimes* times) {
    const size_t couriersCount = in.couriers();
    const size_t ordersCount = in.orders();
    std::vector<int> eta(couriersCount * ordersCount, kEtaUnavailable);
    if (eta.empty() || !table_) return eta;

    Timer total;
    int* etaDevice = nullptr;
    CHECK(cudaMalloc(&etaDevice, eta.size() * sizeof(int)));
    OrderArrays orders{toDevice(in.orderRestaurantNode.data(), ordersCount), toDevice(in.orderDeliveryNode.data(), ordersCount),
                       toDevice(in.orderReadyMs.data(), ordersCount)};
    const dim3 block(ETA_BLOCK_X, ETA_BLOCK_Y);
    const dim3 grid(cdiv(static_cast<unsigned>(couriersCount), ETA_BLOCK_X), cdiv(static_cast<unsigned>(ordersCount), ETA_BLOCK_Y));
    double kernelMs = 0.0;

    if (layout == EtaLayout::StructureOfArrays) {
        CourierArrays couriers{toDevice(in.courierNode.data(), couriersCount), toDevice(in.courierAvailableMs.data(), couriersCount),
                               toDevice(in.courierFree.data(), couriersCount)};
        {
            DeviceTimer kernel;
            launch(etaSoaKernel, grid, block, static_cast<const int*>(table_), n_, couriers, orders,
                   static_cast<int>(couriersCount), static_cast<int>(ordersCount), in.nowMs, etaDevice);
            CHECK(cudaGetLastError());
            kernelMs = kernel.ms();
        }
        CHECK(cudaFree(const_cast<int*>(couriers.node)));
        CHECK(cudaFree(const_cast<int*>(couriers.availableAtMs)));
        CHECK(cudaFree(const_cast<int*>(couriers.free)));
    } else {
        const std::vector<CourierRecord> records = courierRecords(in);
        CourierRecord* couriers = toDevice(records.data(), records.size());
        {
            DeviceTimer kernel;
            launch(etaAosKernel, grid, block, static_cast<const int*>(table_), n_, static_cast<const CourierRecord*>(couriers),
                   orders, static_cast<int>(couriersCount), static_cast<int>(ordersCount), in.nowMs, etaDevice);
            CHECK(cudaGetLastError());
            kernelMs = kernel.ms();
        }
        CHECK(cudaFree(couriers));
    }

    CHECK(cudaMemcpy(eta.data(), etaDevice, eta.size() * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(etaDevice));
    CHECK(cudaFree(const_cast<int*>(orders.restaurantNode)));
    CHECK(cudaFree(const_cast<int*>(orders.deliveryNode)));
    CHECK(cudaFree(const_cast<int*>(orders.readyMs)));
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return eta;
}

std::vector<unsigned long long> EtaBatchGpu::runBest(const EtaInput& in, const std::vector<int>& rotation, GpuTimes* times) {
    const size_t couriersCount = in.couriers();
    const size_t ordersCount = in.orders();
    std::vector<unsigned long long> best(ordersCount, etaKeyOf(kEtaUnavailable, 0, 0, 0));
    if (couriersCount == 0 || ordersCount == 0 || !table_) return best;

    Timer total;
    const size_t cells = couriersCount * ordersCount;
    if (cells > matrixCells_) {
        if (matrix_) CHECK(cudaFree(matrix_));
        matrix_ = nullptr;
        matrixCells_ = 0;
        CHECK(cudaMalloc(&matrix_, cells * sizeof(int)));
        matrixCells_ = cells;
    }
    int* etaDevice = matrix_;
    unsigned long long* bestDevice = nullptr;
    CHECK(cudaMalloc(&bestDevice, ordersCount * sizeof(unsigned long long)));
    OrderArrays orders{toDevice(in.orderRestaurantNode.data(), ordersCount), toDevice(in.orderDeliveryNode.data(), ordersCount),
                       toDevice(in.orderReadyMs.data(), ordersCount)};
    CourierArrays couriers{toDevice(in.courierNode.data(), couriersCount), toDevice(in.courierAvailableMs.data(), couriersCount),
                           toDevice(in.courierFree.data(), couriersCount)};
    int* rotationDevice = rotation.size() == ordersCount ? toDevice(rotation.data(), ordersCount) : nullptr;
    const dim3 block(ETA_BLOCK_X, ETA_BLOCK_Y);
    const dim3 grid(cdiv(static_cast<unsigned>(couriersCount), ETA_BLOCK_X), cdiv(static_cast<unsigned>(ordersCount), ETA_BLOCK_Y));
    double kernelMs = 0.0;
    {
        DeviceTimer kernel;
        launch(etaSoaKernel, grid, block, static_cast<const int*>(table_), n_, couriers, orders,
               static_cast<int>(couriersCount), static_cast<int>(ordersCount), in.nowMs, etaDevice);
        CHECK(cudaGetLastError());
        argminRowsOnDevice(etaDevice, static_cast<int>(ordersCount), static_cast<int>(couriersCount), rotationDevice, bestDevice);
        kernelMs = kernel.ms();
    }
    CHECK(cudaMemcpy(best.data(), bestDevice, ordersCount * sizeof(unsigned long long), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(bestDevice));
    if (rotationDevice) CHECK(cudaFree(rotationDevice));
    CHECK(cudaFree(const_cast<int*>(couriers.node)));
    CHECK(cudaFree(const_cast<int*>(couriers.availableAtMs)));
    CHECK(cudaFree(const_cast<int*>(couriers.free)));
    CHECK(cudaFree(const_cast<int*>(orders.restaurantNode)));
    CHECK(cudaFree(const_cast<int*>(orders.deliveryNode)));
    CHECK(cudaFree(const_cast<int*>(orders.readyMs)));
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return best;
}

std::vector<GpuKernelInfo> etaBatchKernelInfos() {
    return {
        kernelOccupancy("eta_batch", "etaSoaKernel", etaSoaKernel, ETA_BLOCK_X * ETA_BLOCK_Y),
        kernelOccupancy("eta_batch", "etaAosKernel", etaAosKernel, ETA_BLOCK_X * ETA_BLOCK_Y),
    };
}
