#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

class StopSignal {
public:
    StopSignal() = default;
    StopSignal(const StopSignal&) = delete;
    StopSignal& operator=(const StopSignal&) = delete;

    bool requested() const { return flag_.load(std::memory_order_acquire); }

    void request();

    void wait() const;

    template <class Clock, class Duration>
    bool waitUntil(const std::chrono::time_point<Clock, Duration>& deadline) const {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_until(lock, deadline, [this] { return flag_.load(std::memory_order_acquire); });
        return flag_.load(std::memory_order_acquire);
    }

    template <class Rep, class Period>
    bool waitFor(const std::chrono::duration<Rep, Period>& duration) const {
        return waitUntil(std::chrono::steady_clock::now() + duration);
    }

private:
    std::atomic<bool> flag_{false};
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
};

class SignalWatcher {
public:
    explicit SignalWatcher(StopSignal& stop);
    ~SignalWatcher();

    SignalWatcher(const SignalWatcher&) = delete;
    SignalWatcher& operator=(const SignalWatcher&) = delete;

    int signalsReceived() const { return received_.load(std::memory_order_acquire); }

private:
    void watch();

    StopSignal& stop_;
    int pipeFds_[2] = {-1, -1};
    std::atomic<int> received_{0};
    std::thread thread_;
};
