#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>

constexpr std::int64_t kRepairMs = 5 * 60'000;

class BreakdownRoll {
public:
    BreakdownRoll(std::uint64_t seed, std::size_t courierIndex, double probabilityPerTrip);

    std::optional<std::size_t> roll(std::size_t legs);

    std::int64_t trips() const { return trips_; }
    std::int64_t breakdowns() const { return breakdowns_; }

private:
    std::mt19937_64 rng_;
    std::bernoulli_distribution breaks_;
    std::int64_t trips_ = 0;
    std::int64_t breakdowns_ = 0;
};
