#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Tamano de la linea de cache en los procesadores actuales (Apple Silicon usa
// 128 bytes: se toma el mayor para que un contador nunca comparta linea).
constexpr std::size_t kCacheLineBytes = 128;

// Contadores en vivo de una moto, en su propia linea de cache. Cada hilo de
// moto escribe solo los suyos; la ventana los lee cuando quiere. Sin la
// alineacion, los contadores de motos vecinas compartirian linea y cada
// incremento de una invalidaria la linea de la otra (false sharing).
struct alignas(kCacheLineBytes) CourierCounters {
    std::atomic<std::int64_t> delivered{0};
    std::atomic<std::int64_t> pickedUp{0};
    std::atomic<std::int64_t> breakdowns{0};
};
static_assert(sizeof(CourierCounters) % kCacheLineBytes == 0, "un contador por linea de cache");

struct CountersSnapshot {
    std::int64_t delivered = 0, pickedUp = 0, breakdowns = 0;
    std::vector<std::int64_t> deliveredPerCourier;
};

class LiveCounters {
public:
    explicit LiveCounters(std::size_t couriers);
    std::size_t size() const { return count_; }
    CourierCounters& at(std::size_t i) { return slots_[i]; }
    CountersSnapshot snapshot() const;  // lectura relajada: valores en vivo, no una foto atomica

private:
    std::size_t count_;
    std::unique_ptr<CourierCounters[]> slots_;
};
