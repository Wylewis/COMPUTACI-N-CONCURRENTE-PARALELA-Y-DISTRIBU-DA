#pragma once

#include <cstddef>
#include <vector>

#include "eta_batch.h"
#include "gpu_kernel_info.h"
#include "gpu_timing_types.h"
#include "thread_pool.h"

using EtaKey = unsigned long long;

inline EtaKey etaKeyOf(int eta, int courier, int rotation, int couriers) {
    const int rotated = couriers > 0 ? (courier + rotation) % couriers : courier;
    return (static_cast<EtaKey>(static_cast<unsigned>(eta)) << 32) | static_cast<EtaKey>(static_cast<unsigned>(rotated));
}

inline int etaOfKey(EtaKey key) { return static_cast<int>(key >> 32); }

inline int courierOfKey(EtaKey key, int rotation, int couriers) {
    const int rotated = static_cast<int>(key & 0xffffffffULL);
    return couriers > 0 ? ((rotated - rotation % couriers) + couriers) % couriers : rotated;
}

inline bool keyHasCourier(EtaKey key) { return etaOfKey(key) != kEtaUnavailable; }

std::vector<EtaKey> argminRowsCpu(const int* eta, std::size_t orders, std::size_t couriers, const int* rotation = nullptr);
std::vector<EtaKey> argminRowsCpuParallel(const int* eta, std::size_t orders, std::size_t couriers, ThreadPool& pool,
                                          const int* rotation = nullptr);
std::vector<EtaKey> argminRowsGpu(const int* eta, std::size_t orders, std::size_t couriers, const int* rotation = nullptr,
                                  GpuTimes* times = nullptr);

void argminRowsOnDevice(const int* etaDevice, int orders, int couriers, const int* rotationDevice, EtaKey* bestDevice);

GpuKernelInfo argminRowsKernelInfo();
