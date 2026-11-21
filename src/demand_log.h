#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

struct DemandPoint {
    std::int64_t tMs = 0;
    float x = 0.0f;
    float y = 0.0f;
};

class DemandLog {
public:
    void add(std::int64_t tMs, float x, float y) {
        std::lock_guard<std::mutex> lock(mutex_);
        points_.push_back({tMs, x, y});
    }

    std::vector<DemandPoint> since(std::int64_t fromMs) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<DemandPoint> recent;
        for (const DemandPoint& p : points_)
            if (p.tMs >= fromMs) recent.push_back(p);
        return recent;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return points_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<DemandPoint> points_;
};
