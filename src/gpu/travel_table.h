#pragma once

#include <cstddef>
#include <vector>

#include "gpu_timing_types.h"
#include "thread_pool.h"

constexpr int kUnreachableMs = 1 << 29;

struct CsrGraph {
    int n = 0;
    std::vector<int> rowStart;
    std::vector<int> to;
    std::vector<int> weightMs;
};

std::vector<int> travelTableCpu(const CsrGraph& g);
std::vector<int> travelTableCpuParallel(const CsrGraph& g, ThreadPool& pool);
std::vector<int> travelTableGpu(const CsrGraph& g, GpuTimes* times = nullptr, int* squarings = nullptr);

class TravelTable {
public:
    TravelTable() = default;
    TravelTable(int n, std::vector<int> ms) : n_(n), ms_(std::move(ms)) {}

    int size() const { return n_; }
    bool empty() const { return n_ == 0; }
    int ms(int from, int to) const {
        return ms_[static_cast<std::size_t>(from) * static_cast<std::size_t>(n_) + static_cast<std::size_t>(to)];
    }
    const std::vector<int>& data() const { return ms_; }
    std::size_t unreachablePairs() const;

private:
    int n_ = 0;
    std::vector<int> ms_;
};
