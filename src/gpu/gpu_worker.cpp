#include "gpu_worker.h"

GpuWorker::GpuWorker() {
    thread_ = std::thread([this] { loop(); });
    owner_ = thread_.get_id();
}

GpuWorker::~GpuWorker() { stop(); }

std::size_t GpuWorker::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size();
}

void GpuWorker::loop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            available_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty()) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        job();
        jobsRun_.fetch_add(1, std::memory_order_acq_rel);
    }
}

void GpuWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    available_.notify_all();
    if (thread_.joinable()) thread_.join();
}
