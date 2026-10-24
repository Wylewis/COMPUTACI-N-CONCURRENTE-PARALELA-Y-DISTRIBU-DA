#pragma once

#include <chrono>
#include <cstdint>

class SimClock {
public:
    using RealClock = std::chrono::steady_clock;
    using RealDuration = RealClock::duration;
    using RealTimePoint = RealClock::time_point;

    explicit SimClock(double timeScale);

    void start(RealTimePoint realStart = RealClock::now());

    double timeScale() const { return scale_; }
    RealTimePoint realStart() const { return start_; }

    std::int64_t nowMs() const { return simMsAt(RealClock::now()); }

    std::int64_t simMsAt(RealTimePoint realNow) const;

    RealDuration toReal(std::int64_t simMs) const;

    std::int64_t toSimMs(RealDuration real) const;

    RealTimePoint realTimeFor(std::int64_t simMs) const;

private:
    double scale_;
    RealTimePoint start_;
};
