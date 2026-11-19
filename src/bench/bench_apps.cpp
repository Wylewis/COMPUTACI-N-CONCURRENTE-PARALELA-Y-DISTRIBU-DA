#include "bench.h"

#include <cstddef>
#include <string>
#include <vector>

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

}

std::vector<BenchApp> benchApps() {
    return {
        {"vector_add", "00_reference", benchVectorAdd},
        {"travel_table", "03_memory_tiling", benchTravelTable},
    };
}
