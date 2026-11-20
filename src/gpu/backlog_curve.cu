#include "backlog_curve.h"

#include "cuda_compat.h"
#include "gpu_occupancy.h"
#include "gpu_timing.h"

constexpr unsigned CURVE_SECTION = 256;

__global__ void koggeStoneSectionKernel(const int* in, unsigned n, int* out, int* sums) {
    __shared__ int section[CURVE_SECTION];

    const unsigned t = threadIdx.x;
    const unsigned i = blockIdx.x * CURVE_SECTION + t;
    section[t] = i < n ? in[i] : 0;
    for (unsigned stride = 1; stride < CURVE_SECTION; stride *= 2) {
        __syncthreads();
        int value = section[t];
        if (t >= stride) value += section[t - stride];
        __syncthreads();
        section[t] = value;
    }
    __syncthreads();
    if (i < n) out[i] = section[t];
    if (t == CURVE_SECTION - 1) sums[blockIdx.x] = section[t];
}

__global__ void brentKungSectionKernel(const int* in, unsigned n, int* out, int* sums) {
    __shared__ int section[CURVE_SECTION];

    const unsigned t = threadIdx.x;
    const unsigned i = blockIdx.x * CURVE_SECTION + t;
    section[t] = i < n ? in[i] : 0;
    for (unsigned stride = 1; stride < CURVE_SECTION; stride *= 2) {
        __syncthreads();
        const unsigned at = (t + 1) * 2 * stride - 1;
        if (at < CURVE_SECTION) section[at] += section[at - stride];
    }
    for (unsigned stride = CURVE_SECTION / 4; stride > 0; stride /= 2) {
        __syncthreads();
        const unsigned at = (t + 1) * 2 * stride - 1;
        if (at + stride < CURVE_SECTION) section[at + stride] += section[at];
    }
    __syncthreads();
    if (i < n) out[i] = section[t];
    if (t == CURVE_SECTION - 1) sums[blockIdx.x] = section[t];
}

__global__ void addPreviousSectionsKernel(int* out, unsigned n, const int* scannedSums) {
    const unsigned i = blockIdx.x * CURVE_SECTION + threadIdx.x;
    if (blockIdx.x > 0 && i < n) out[i] += scannedSums[blockIdx.x - 1];
}

namespace {

int inclusiveScanOnDevice(const int* in, unsigned n, int* out, ScanKernel kernel) {
    const unsigned sections = cdiv(n, CURVE_SECTION);
    int* sums = nullptr;
    CHECK(cudaMalloc(&sums, sections * sizeof(int)));
    if (kernel == ScanKernel::BrentKung)
        launch(brentKungSectionKernel, dim3(sections), dim3(CURVE_SECTION), in, n, out, sums);
    else
        launch(koggeStoneSectionKernel, dim3(sections), dim3(CURVE_SECTION), in, n, out, sums);
    CHECK(cudaGetLastError());
    int levels = 1;
    if (sections > 1) {
        int* scannedSums = nullptr;
        CHECK(cudaMalloc(&scannedSums, sections * sizeof(int)));
        levels += inclusiveScanOnDevice(sums, sections, scannedSums, kernel);
        launch(addPreviousSectionsKernel, dim3(sections), dim3(CURVE_SECTION), out, n, static_cast<const int*>(scannedSums));
        CHECK(cudaGetLastError());
        CHECK(cudaFree(scannedSums));
    }
    CHECK(cudaFree(sums));
    return levels;
}

}

std::vector<int> backlogCurveGpu(const int* delta, std::size_t slots, ScanKernel kernel, GpuTimes* times, int* levels) {
    Timer total;
    std::vector<int> curve(slots, 0);
    if (levels) *levels = 0;
    if (slots == 0) return curve;

    int *in = nullptr, *out = nullptr;
    CHECK(cudaMalloc(&in, slots * sizeof(int)));
    CHECK(cudaMalloc(&out, slots * sizeof(int)));
    CHECK(cudaMemcpy(in, delta, slots * sizeof(int), cudaMemcpyHostToDevice));
    int done = 0;
    double kernelMs = 0.0;
    {
        DeviceTimer timer;
        done = inclusiveScanOnDevice(in, static_cast<unsigned>(slots), out, kernel);
        kernelMs = timer.ms();
    }
    CHECK(cudaMemcpy(curve.data(), out, slots * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(in));
    CHECK(cudaFree(out));
    if (levels) *levels = done;
    if (times) {
        times->kernelMs = kernelMs;
        times->totalMs = total.ms();
    }
    return curve;
}

std::vector<GpuKernelInfo> backlogCurveKernelInfos() {
    return {
        kernelOccupancy("backlog_curve", "koggeStoneSectionKernel", koggeStoneSectionKernel, CURVE_SECTION),
        kernelOccupancy("backlog_curve", "brentKungSectionKernel", brentKungSectionKernel, CURVE_SECTION),
        kernelOccupancy("backlog_curve", "addPreviousSectionsKernel", addPreviousSectionsKernel, CURVE_SECTION),
    };
}
