#include "bench.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>
#include <utility>

#include "gpu/gpu_backend.h"

BenchRunner::BenchRunner(const BenchOptions& options, const Config& config, const Graph& graph, GpuWorker& gpuWorker,
                         bool realGpu, std::ostream& csv)
    : options_(options), config_(config), graph_(graph), gpuWorker_(gpuWorker), realGpu_(realGpu), csv_(csv) {
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    const unsigned top = options_.threads > 0 ? options_.threads : hardware;
    if (options_.sweep) {
        for (unsigned t = 1; t < top; t *= 2) threadCounts_.push_back(t);
    }
    threadCounts_.push_back(top);
    csv_ << "application,concept,version,threads,size,items,repetition,time_ms,correct\n";
}

ThreadPool& BenchRunner::poolOf(unsigned threads) {
    auto it = pools_.find(threads);
    if (it == pools_.end()) it = pools_.emplace(threads, std::make_unique<ThreadPool>(threads)).first;
    return *it->second;
}

void BenchRunner::row(const BenchCase& c, const std::string& version, unsigned threads, int rep, double ms,
                      bool correct) {
    csv_ << c.app << ',' << c.concept << ',' << version << ',' << threads << ',' << c.size << ',' << c.items << ','
         << rep << ',' << std::fixed << std::setprecision(6) << ms << ',' << (correct ? "yes" : "no") << '\n';
    ++rows_;
    if (!correct) ++wrong_;
}

GpuOutcome BenchRunner::onDevice(const std::function<GpuOutcome()>& measured) {
    if (!options_.wake || options_.once) return gpuWorker_.submit(measured).get();
    const std::pair<GpuWake, GpuOutcome> r = gpuWorker_.submit([&measured] {
        const GpuWake wake = gpuWake();
        return std::make_pair(wake, measured());
    }).get();
    ++wakes_;
    wakeMs_ += r.first.spentMs;
    if (!r.first.steady) ++unsteadyWakes_;
    return r.second;
}

void BenchRunner::run(const BenchCase& c) {
    if (!wants(c.items)) return;
    const bool withGpu = realGpu_ && c.gpu;
    if (!options_.once) {
        if (c.cpu1) c.cpu1();
        if (c.cpuN)
            for (unsigned t : threadCounts_) c.cpuN(poolOf(t));
        if (withGpu) (void)gpuWorker_.submit(c.gpu).get();
        if (realGpu_)
            for (const auto& variant : c.gpuVariants) (void)gpuWorker_.submit(variant.second).get();
    }
    const int reps = options_.once ? 1 : std::max(1, options_.reps);
    for (int rep = 0; rep < reps; ++rep) {
        if (c.cpu1) {
            const Timed r = c.cpu1();
            row(c, "cpu_1", 1, rep, r.ms, r.correct);
        }
        if (c.cpuN) {
            for (unsigned t : threadCounts_) {
                const Timed r = c.cpuN(poolOf(t));
                row(c, "cpu_n", t, rep, r.ms, r.correct);
            }
        }
        if (withGpu) {
            const GpuOutcome r = onDevice(c.gpu);
            row(c, "gpu_kernel", 0, rep, r.times.kernelMs, r.correct);
            row(c, "gpu_e2e", 0, rep, r.times.totalMs, r.correct);
        }
        if (realGpu_) {
            for (const auto& variant : c.gpuVariants) {
                const GpuOutcome r = onDevice(variant.second);
                row(c, "gpu_kernel_" + variant.first, 0, rep, r.times.kernelMs, r.correct);
                row(c, "gpu_e2e_" + variant.first, 0, rep, r.times.totalMs, r.correct);
            }
        }
    }
    csv_.flush();
    std::cerr << "bench: " << c.app << " " << c.size << " listo\n";
}

int runBenchmarks(const BenchOptions& options, const Config& config, const Graph& graph, GpuWorker& gpuWorker,
                  bool realGpu) {
    std::ofstream csv(options.csvPath, std::ios::out | std::ios::trunc);
    if (!csv) {
        std::cerr << "delivery_sim: no se puede escribir el CSV de mediciones: " << options.csvPath << "\n";
        return 1;
    }
    BenchRunner runner(options, config, graph, gpuWorker, realGpu, csv);
    const std::vector<BenchApp> apps = benchApps();
    for (const std::string& wanted : options.apps) {
        const bool known = std::any_of(apps.begin(), apps.end(), [&](const BenchApp& a) { return a.name == wanted; });
        if (!known) {
            std::cerr << "delivery_sim: aplicacion de medicion desconocida: " << wanted << "\n";
            return 1;
        }
    }
    if (!realGpu) std::cerr << "bench: sin GPU real, solo se miden las versiones de CPU (la emulacion no se cronometra)\n";
    for (const BenchApp& app : apps) {
        const bool selected = options.apps.empty() ||
                              std::find(options.apps.begin(), options.apps.end(), app.name) != options.apps.end();
        if (selected) app.run(runner);
    }
    if (runner.wakes() > 0)
        std::cerr << "bench: tarjeta despertada antes de " << runner.wakes() << " medidas de GPU, " << runner.wakeMs()
                  << " ms en total, " << runner.unsteadyWakes() << " sin llegar a regimen\n";
    std::cerr << "bench: " << runner.rows() << " filas en " << options.csvPath << ", " << runner.wrong()
              << " con resultado incorrecto\n";
    return runner.wrong() == 0 ? 0 : 1;
}
