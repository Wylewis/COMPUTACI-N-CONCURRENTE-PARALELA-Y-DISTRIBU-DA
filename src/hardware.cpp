#include "hardware.h"

#include <algorithm>
#include <thread>

unsigned hardwareThreads() {
    return std::thread::hardware_concurrency();
}

unsigned clampPoolSize(unsigned hardware, unsigned minimum, unsigned maximum) {
    const unsigned lo = std::max(1u, minimum);
    const unsigned hi = std::max(lo, maximum);
    if (hardware == 0) return lo;
    return std::min(hi, std::max(lo, hardware));
}

unsigned recommendedPoolSize() {
    return clampPoolSize(hardwareThreads(), kPoolMinimum, kPoolMaximum);
}

unsigned defaultTaskCount(std::size_t work) {
    unsigned cores = hardwareThreads();
    if (cores == 0) cores = 2;
    const std::size_t limit = std::max<std::size_t>(1, work);
    return static_cast<unsigned>(std::min<std::size_t>(cores, limit));
}
