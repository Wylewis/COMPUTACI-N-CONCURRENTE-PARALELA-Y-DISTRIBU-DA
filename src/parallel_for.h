#pragma once

#include <algorithm>
#include <cstddef>
#include <future>
#include <vector>

#include "thread_pool.h"

template <class Body>
void parallelFor(ThreadPool& pool, std::size_t n, Body body) {
    if (n == 0) return;
    const std::size_t tasks = std::min<std::size_t>(std::max(1u, pool.size()), n);
    const std::size_t chunk = (n + tasks - 1) / tasks;
    std::vector<std::future<void>> pending;
    pending.reserve(tasks);
    for (std::size_t t = 0; t < tasks; ++t) {
        const std::size_t begin = t * chunk;
        const std::size_t end = std::min(n, begin + chunk);
        if (begin >= end) break;
        pending.push_back(pool.submit([body, begin, end, t] { body(begin, end, t); }));
    }
    for (auto& f : pending) f.get();
}
