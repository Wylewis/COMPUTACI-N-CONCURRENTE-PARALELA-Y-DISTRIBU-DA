#include "event_logger.h"

#include <stdexcept>
#include <utility>

EventLogger::EventLogger(const std::string& path, const std::vector<std::string>& courierIds)
    : log_(path, courierIds) {
    writer_ = std::thread([this] { writerLoop(); });
}

EventLogger::~EventLogger() {
    close();
}

bool EventLogger::publish(Event event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return false;
        queue_.push_back(std::move(event));
    }
    published_.fetch_add(1, std::memory_order_acq_rel);
    available_.notify_one();
    return true;
}

void EventLogger::writerLoop() {
    std::deque<Event> batch;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            available_.wait(lock, [this] { return closed_ || !queue_.empty(); });
            if (queue_.empty()) return;
            batch.swap(queue_);
        }

        for (const Event& e : batch) {
            log_.write(e);
            written_.fetch_add(1, std::memory_order_acq_rel);
        }
        batch.clear();
    }
}

void EventLogger::close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return;
        closed_ = true;
    }
    closedFlag_.store(true, std::memory_order_release);
    available_.notify_all();
    if (writer_.joinable()) writer_.join();
    log_.close();
}

Summary EventLogger::summary() const {
    if (!closedFlag_.load(std::memory_order_acquire) || writer_.joinable()) {
        throw std::logic_error("el resumen solo es definitivo despues de close()");
    }
    return log_.summary();
}
