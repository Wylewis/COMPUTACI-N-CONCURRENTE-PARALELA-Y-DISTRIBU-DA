#include "bench.h"

#include <cstddef>
#include <string>
#include <vector>

#include "gpu/gpu_backend.h"
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

}

std::vector<BenchApp> benchApps() {
    return {
        {"vector_add", "00_reference", benchVectorAdd},
    };
}
