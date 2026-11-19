#pragma once

#include <algorithm>
#include <cstddef>

#include "cuda_compat.h"
#include "gpu_kernel_info.h"

template <class Kernel>
GpuKernelInfo kernelOccupancy(const char* application, const char* name, Kernel kernel, int threadsPerBlock) {
    GpuKernelInfo info;
    info.application = application;
    info.kernel = name;
    info.threadsPerBlock = threadsPerBlock;
#ifdef __CUDACC__
    int blocks = 0;
    cudaFuncAttributes attributes;
    cudaDeviceProp device;
    if (cudaOccupancyMaxActiveBlocksPerMultiprocessor(&blocks, kernel, threadsPerBlock, 0) != cudaSuccess ||
        cudaFuncGetAttributes(&attributes, kernel) != cudaSuccess ||
        cudaGetDeviceProperties(&device, 0) != cudaSuccess) {
        return info;
    }
    info.measured = true;
    info.blocksPerMultiprocessor = blocks;
    info.registersPerThread = attributes.numRegs;
    info.sharedBytesPerBlock = attributes.sharedSizeBytes;
    info.occupancyPercent = 100.0 * blocks * threadsPerBlock / device.maxThreadsPerMultiProcessor;
    const int byThreads = device.maxThreadsPerMultiProcessor / threadsPerBlock;
    const int byRegisters = attributes.numRegs > 0 ? device.regsPerMultiprocessor / (attributes.numRegs * threadsPerBlock)
                                                   : byThreads + 1;
    const int byShared = attributes.sharedSizeBytes > 0
                             ? static_cast<int>(device.sharedMemPerMultiprocessor / attributes.sharedSizeBytes)
                             : byThreads + 1;
    const int tightest = std::min(byThreads, std::min(byRegisters, byShared));
    if (blocks < tightest) info.limiter = "bloques por multiprocesador";
    else if (tightest == byThreads) info.limiter = "hilos";
    else if (tightest == byRegisters) info.limiter = "registros";
    else info.limiter = "memoria compartida";
#else
    (void)kernel;
    info.limiter = "no aplica en la emulacion";
#endif
    return info;
}
