#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "config.h"
#include "gpu/gpu_timing_types.h"
#include "gpu/gpu_worker.h"
#include "graph.h"
#include "thread_pool.h"

struct BenchOptions {
    std::string csvPath;
    int reps = 10;
    bool once = false;
    bool sweep = false;
    unsigned threads = 0;
    std::size_t maxItems = 0;
    bool wake = true;
    std::vector<std::string> apps;
};

struct Timed {
    double ms = 0.0;
    bool correct = false;
};

struct GpuOutcome {
    GpuTimes times;
    bool correct = false;
};

struct BenchCase {
    std::string app;
    std::string concept;
    std::string size;
    std::size_t items = 0;
    std::function<Timed()> cpu1;
    std::function<Timed(ThreadPool&)> cpuN;
    std::vector<std::pair<std::string, std::function<Timed()>>> cpuReferences;
    std::function<GpuOutcome()> gpu;
    std::vector<std::pair<std::string, std::function<GpuOutcome()>>> gpuVariants;
};

class BenchRunner {
public:
    BenchRunner(const BenchOptions& options, const Config& config, const Graph& graph, GpuWorker& gpuWorker,
                bool realGpu, std::ostream& csv);

    const Config& config() const { return config_; }
    const Graph& graph() const { return graph_; }
    bool wants(std::size_t items) const { return options_.maxItems == 0 || items <= options_.maxItems; }

    void run(const BenchCase& c);

    std::size_t rows() const { return rows_; }
    std::size_t wakes() const { return wakes_; }
    std::size_t unsteadyWakes() const { return unsteadyWakes_; }
    double wakeMs() const { return wakeMs_; }
    std::size_t wrong() const { return wrong_; }

private:
    ThreadPool& poolOf(unsigned threads);
    GpuOutcome onDevice(const std::function<GpuOutcome()>& measured);
    void row(const BenchCase& c, const std::string& version, unsigned threads, int rep, double ms, bool correct);

    const BenchOptions options_;
    const Config& config_;
    const Graph& graph_;
    GpuWorker& gpuWorker_;
    const bool realGpu_;
    std::ostream& csv_;
    std::vector<unsigned> threadCounts_;
    std::map<unsigned, std::unique_ptr<ThreadPool>> pools_;
    std::size_t rows_ = 0;
    std::size_t wakes_ = 0;
    std::size_t unsteadyWakes_ = 0;
    double wakeMs_ = 0.0;
    std::size_t wrong_ = 0;
};

struct BenchApp {
    std::string name;
    std::string concept;
    std::function<void(BenchRunner&)> run;
};

std::vector<BenchApp> benchApps();

int runBenchmarks(const BenchOptions& options, const Config& config, const Graph& graph, GpuWorker& gpuWorker,
                  bool realGpu);

template <class F>
double timeMs(F&& f) {
    const auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
