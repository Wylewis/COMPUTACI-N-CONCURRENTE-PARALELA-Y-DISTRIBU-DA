#include "bench.h"

#include <cstddef>
#include <string>
#include <vector>

#include <memory>
#include <random>
#include <thread>

#include "gpu/eta_batch.h"
#include "gpu/gpu_backend.h"
#include "gpu/travel_table.h"
#include "gpu/travel_table_graph.h"
#include "parallel_for.h"

namespace {

void benchVectorAdd(BenchRunner& runner) {
    for (const std::size_t n : {1'000u, 10'000u, 100'000u, 1'000'000u, 10'000'000u}) {
        if (!runner.wants(n)) continue;
        std::vector<int> a(n), b(n), expected(n), out(n);
        for (std::size_t i = 0; i < n; ++i) {
            a[i] = static_cast<int>(i % 1000);
            b[i] = static_cast<int>(2 * (i % 777) + 1);
            expected[i] = a[i] + b[i];
        }
        BenchCase c;
        c.app = "vector_add";
        c.concept = "00_reference";
        c.size = std::to_string(n);
        c.items = n;
        c.cpu1 = [&] {
            Timed r;
            r.ms = timeMs([&] {
                for (std::size_t i = 0; i < n; ++i) out[i] = a[i] + b[i];
            });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            r.ms = timeMs([&] {
                parallelFor(pool, n, [&](std::size_t begin, std::size_t end, std::size_t) {
                    for (std::size_t i = begin; i < end; ++i) out[i] = a[i] + b[i];
                });
            });
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] {
            GpuOutcome r;
            r.times = gpuVectorAdd(a.data(), b.data(), out.data(), static_cast<int>(n));
            r.correct = out == expected;
            return r;
        };
        runner.run(c);
    }
}

void benchTravelTable(BenchRunner& runner) {
    const CsrGraph city = csrFromGraph(runner.graph(), runner.config().fleet.speedKmh);
    const int start = static_cast<int>(runner.graph().nodeIndex(runner.config().fleet.startNode));
    for (const int wanted : {128, 256, 512, 1024, 2048, 4096}) {
        if (wanted > city.n) break;
        const CsrGraph g = prefixSubgraph(city, start, wanted);
        const std::size_t items = static_cast<std::size_t>(g.n) * static_cast<std::size_t>(g.n);
        if (!runner.wants(items)) continue;
        const std::vector<int> expected = travelTableCpu(g);
        BenchCase c;
        c.app = "travel_table";
        c.concept = "03_memory_tiling";
        c.size = std::to_string(g.n) + "x" + std::to_string(g.n);
        c.items = items;
        c.cpu1 = [&] {
            Timed r;
            std::vector<int> out;
            r.ms = timeMs([&] { out = travelTableCpu(g); });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            std::vector<int> out;
            r.ms = timeMs([&] { out = travelTableCpuParallel(g, pool); });
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] {
            GpuOutcome r;
            const std::vector<int> out = travelTableGpu(g, &r.times);
            r.correct = out == expected;
            return r;
        };
        runner.run(c);
    }
}

EtaInput randomEtaInput(int nodes, std::size_t orders, std::size_t couriers, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> node(0, nodes - 1), time(0, 600'000), percent(0, 99);
    EtaInput in;
    in.nowMs = 300'000;
    for (std::size_t c = 0; c < couriers; ++c) {
        in.courierNode.push_back(node(rng));
        in.courierAvailableMs.push_back(time(rng));
        in.courierFree.push_back(percent(rng) < 85 ? 1 : 0);
    }
    for (std::size_t o = 0; o < orders; ++o) {
        in.orderRestaurantNode.push_back(node(rng));
        in.orderDeliveryNode.push_back(node(rng));
        in.orderReadyMs.push_back(time(rng));
    }
    return in;
}

void benchEtaBatch(BenchRunner& runner) {
    const CsrGraph city = csrFromGraph(runner.graph(), runner.config().fleet.speedKmh);
    ThreadPool builders(std::max(1u, std::thread::hardware_concurrency()));
    const TravelTable table(city.n, travelTableCpuParallel(city, builders));
    std::shared_ptr<EtaBatchGpu> device;
    const std::vector<std::pair<std::size_t, std::size_t>> sizes{{10, 50}, {50, 200}, {100, 500}, {200, 1000}, {500, 2000}, {1000, 3000}};
    for (const auto& [orders, couriers] : sizes) {
        const std::size_t items = orders * couriers;
        if (!runner.wants(items)) continue;
        const EtaInput in = randomEtaInput(city.n, orders, couriers, static_cast<unsigned>(items));
        std::vector<int> expected(items), out(items);
        etaBatchCpu(table, in, expected.data());
        BenchCase c;
        c.app = "eta_batch";
        c.concept = "04_performance";
        c.size = std::to_string(orders) + "x" + std::to_string(couriers);
        c.items = items;
        c.cpu1 = [&] {
            Timed r;
            r.ms = timeMs([&] { etaBatchCpu(table, in, out.data()); });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            r.ms = timeMs([&] { etaBatchCpuParallel(table, in, out.data(), pool); });
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] {
            if (!device) device = std::make_shared<EtaBatchGpu>(table);
            GpuOutcome r;
            r.correct = device->run(in, EtaLayout::StructureOfArrays, &r.times) == expected;
            return r;
        };
        c.gpuVariants.push_back({"aos", [&] {
            if (!device) device = std::make_shared<EtaBatchGpu>(table);
            GpuOutcome r;
            r.correct = device->run(in, EtaLayout::ArrayOfStructures, &r.times) == expected;
            return r;
        }});
        c.gpuVariants.push_back({"cold", [&] {
            EtaBatchGpu cold(table);
            GpuOutcome r;
            r.correct = cold.run(in, EtaLayout::StructureOfArrays, &r.times) == expected;
            r.times.totalMs += cold.tableUploadMs();
            return r;
        }});
        runner.run(c);
    }
}

}

std::vector<BenchApp> benchApps() {
    return {
        {"vector_add", "00_reference", benchVectorAdd},
        {"travel_table", "03_memory_tiling", benchTravelTable},
        {"eta_batch", "04_performance", benchEtaBatch},
    };
}
