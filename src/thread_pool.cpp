#include "thread_pool.h"

#include <algorithm>

ThreadPool::ThreadPool(unsigned workers) {
    const unsigned n = std::max(1u, workers);
    workers_.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

std::size_t ThreadPool::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size();
}

void ThreadPool::workerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // Duerme hasta que haya trabajo o se este parando. El predicado
            // cubre los despertares sin novedad y el despertar final.
            available_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty()) return;  // parando y sin nada pendiente
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        job();  // fuera del cerrojo: otros hilos pueden tomar trabajos mientras tanto
    }
}

void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    available_.notify_all();  // todos deben revisar la bandera, no solo uno
    for (std::thread& t : workers_) {
        if (t.joinable()) t.join();
    }
}
