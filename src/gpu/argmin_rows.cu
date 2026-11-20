#include "argmin_rows.h"

#include "cuda_compat.h"
#include "gpu_occupancy.h"
#include "gpu_timing.h"

constexpr unsigned ARGMIN_BLOCK = 256;

__global__ void argminRowsKernel(const int* eta, int couriers, const int* rotation, EtaKey* best) {
    __shared__ EtaKey keys[ARGMIN_BLOCK];

    const unsigned t = threadIdx.x;
    const size_t row = blockIdx.x;
    const int rot = rotation ? rotation[row] : 0;
    const int* etaRow = eta + row * static_cast<size_t>(couriers);

    EtaKey mine = (static_cast<EtaKey>(static_cast<unsigned>(kEtaUnavailable)) << 32);
    for (int c = static_cast<int>(t); c < couriers; c += static_cast<int>(ARGMIN_BLOCK)) {
        const int rotated = (c + rot) % couriers;
        const EtaKey key = (static_cast<EtaKey>(static_cast<unsigned>(etaRow[c])) << 32) |
                           static_cast<EtaKey>(static_cast<unsigned>(rotated));
        if (key < mine) mine = key;
    }
    keys[t] = mine;
    __syncthreads();

    for (unsigned stride = ARGMIN_BLOCK / 2; stride >= 1; stride /= 2) {
        if (t < stride && keys[t + stride] < keys[t]) keys[t] = keys[t + stride];
        __syncthreads();
    }
    if (t == 0) best[row] = keys[0];
}

void argminRowsOnDevice(const int* etaDevice, int orders, int couriers, const int* rotationDevice, EtaKey* bestDevice) {
    if (orders <= 0 || couriers <= 0) return;
    launch(argminRowsKernel, dim3(static_cast<unsigned>(orders)), dim3(ARGMIN_BLOCK), etaDevice, couriers, rotationDevice,
           bestDevice);
    CHECK(cudaGetLastError());
}

std::vector<EtaKey> argminRowsGpu(const int* eta, std::size_t orders, std::size_t couriers, const int* rotation,
                                  GpuTimes* times) {
    std::vector<EtaKey> best(orders, etaKeyOf(kEtaUnavailable, 0, 0, 0));
    if (orders == 0 || couriers == 0) return best;
    Timer total;
    const size_t cells = orders * couriers;
    int *etaDevice = nullptr, *rotationDevice = nullptr;
    EtaKey* bestDevice = nullptr;
    CHECK(cudaMalloc(&etaDevice, cells * sizeof(int)));
    CHECK(cudaMalloc(&bestDevice, orders * sizeof(EtaKey)));
    CHECK(cudaMemcpy(etaDevice, eta, cells * sizeof(int), cudaMemcpyHostToDevice));
    if (rotation) {
        CHECK(cudaMalloc(&rotationDevice, orders * sizeof(int)));
        CHECK(cudaMemcpy(rotationDevice, rotation, orders * sizeof(int), cudaMemcpyHostToDevice));
    }
    double kernelMs = 0.0;
    {
        DeviceTimer kernel;
        argminRowsOnDevice(etaDevice, static_cast<int>(orders), static_cast<int>(couriers), rotationDevice, bestDevice);
        kernelMs = kernel.ms();
    }
    CHECK(cudaMemcpy(best.data(), bestDevice, orders * sizeof(EtaKey), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(etaDevice));
    CHECK(cudaFree(bestDevice));
    if (rotationDevice) CHECK(cudaFree(rotationDevice));
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return best;
}

GpuKernelInfo argminRowsKernelInfo() {
    return kernelOccupancy("dispatch_argmin", "argminRowsKernel", argminRowsKernel, ARGMIN_BLOCK);
}
