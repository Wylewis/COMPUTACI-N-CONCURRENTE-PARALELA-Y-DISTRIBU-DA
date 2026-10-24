#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(unsigned workers);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    unsigned size() const { return static_cast<unsigned>(workers_.size()); }

    std::size_t pending() const;

    template <class F>
    auto submit(F&& job) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;

        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(job));
        std::future<R> result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                throw std::runtime_error("el pool de hilos ya fue parado");
            }
            jobs_.emplace_back([task] { (*task)(); });
        }
        available_.notify_one();
        return result;
    }

    void stop();

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> jobs_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    bool stopping_ = false;
};
