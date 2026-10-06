#include "sim_clock.h"

#include <cmath>
#include <stdexcept>

namespace {
constexpr double kNsPerMs = 1'000'000.0;
}

SimClock::SimClock(double timeScale) : scale_(timeScale), start_(RealClock::now()) {
    if (!(timeScale > 0.0)) {
        throw std::invalid_argument("simulation.timeScale debe ser mayor que 0");
    }
}

void SimClock::start(RealTimePoint realStart) {
    start_ = realStart;
}

std::int64_t SimClock::simMsAt(RealTimePoint realNow) const {
    if (realNow < start_) return 0;
    const auto realNs = std::chrono::duration_cast<std::chrono::nanoseconds>(realNow - start_).count();
    return static_cast<std::int64_t>(std::llround(static_cast<double>(realNs) * scale_ / kNsPerMs));
}

SimClock::RealDuration SimClock::toReal(std::int64_t simMs) const {
    const double realNs = static_cast<double>(simMs) * kNsPerMs / scale_;
    return std::chrono::duration_cast<RealDuration>(
        std::chrono::nanoseconds(static_cast<std::int64_t>(std::llround(realNs))));
}

std::int64_t SimClock::toSimMs(RealDuration real) const {
    const auto realNs = std::chrono::duration_cast<std::chrono::nanoseconds>(real).count();
    return static_cast<std::int64_t>(std::llround(static_cast<double>(realNs) * scale_ / kNsPerMs));
}

SimClock::RealTimePoint SimClock::realTimeFor(std::int64_t simMs) const {
    return start_ + toReal(simMs);
}
