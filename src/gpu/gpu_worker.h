#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

class GpuWorker {
public:
    GpuWorker();
    ~GpuWorker();

    GpuWorker(const GpuWorker&) = delete;
    GpuWorker& operator=(const GpuWorker&) = delete;

    template <class F>
    auto submit(F&& job) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(job));
        std::future<R> result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) throw std::runtime_error("el hilo de la GPU ya fue parado");
            jobs_.emplace_back([task] { (*task)(); });
        }
        available_.notify_one();
        return result;
    }

    void stop();

    bool onOwnerThread() const { return std::this_thread::get_id() == owner_; }
    std::size_t pending() const;
    std::int64_t jobsRun() const { return jobsRun_.load(std::memory_order_acquire); }

private:
    void loop();

    std::deque<std::function<void()>> jobs_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    bool stopping_ = false;
    std::atomic<std::int64_t> jobsRun_{0};
    std::thread thread_;
    std::thread::id owner_;
};
