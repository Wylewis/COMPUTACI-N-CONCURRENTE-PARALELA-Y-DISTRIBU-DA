#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>

// Tiempo simulado que una moto averiada tarda en volver al servicio.
constexpr std::int64_t kRepairMs = 5 * 60'000;

// Sorteo de averias de una moto. Cada moto tiene su propio generador,
// derivado de la semilla global y de su indice, asi el sorteo no comparte
// estado entre hilos y la misma semilla reproduce las mismas averias para el
// mismo ritmo de viajes. Se sortea una vez por viaje con la probabilidad de
// la configuracion y, si toca, un tramo del viaje al azar donde ocurre.
class BreakdownRoll {
public:
    BreakdownRoll(std::uint64_t seed, std::size_t courierIndex, double probabilityPerTrip);

    // Tramo (0..legs-1) en que se averia este viaje, o nullopt si llega bien.
    std::optional<std::size_t> roll(std::size_t legs);

    std::int64_t trips() const { return trips_; }
    std::int64_t breakdowns() const { return breakdowns_; }

private:
    std::mt19937_64 rng_;
    std::bernoulli_distribution breaks_;
    std::int64_t trips_ = 0;
    std::int64_t breakdowns_ = 0;
};
