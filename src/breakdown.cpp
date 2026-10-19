#include "breakdown.h"

#include <algorithm>

namespace {
std::mt19937_64 seededFor(std::uint64_t seed, std::size_t courierIndex) {
    std::seed_seq seq{static_cast<std::uint64_t>(seed), static_cast<std::uint64_t>(courierIndex + 1)};
    return std::mt19937_64(seq);
}
}  // namespace

BreakdownRoll::BreakdownRoll(std::uint64_t seed, std::size_t courierIndex, double probabilityPerTrip)
    : rng_(seededFor(seed, courierIndex)), breaks_(std::clamp(probabilityPerTrip, 0.0, 1.0)) {}

std::optional<std::size_t> BreakdownRoll::roll(std::size_t legs) {
    if (legs == 0) return std::nullopt;
    ++trips_;
    if (!breaks_(rng_)) return std::nullopt;
    ++breakdowns_;
    return std::uniform_int_distribution<std::size_t>(0, legs - 1)(rng_);
}
