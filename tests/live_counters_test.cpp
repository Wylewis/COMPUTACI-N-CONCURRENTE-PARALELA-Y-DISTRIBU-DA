#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "live_counters.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

struct Packed {
    std::atomic<std::int64_t> v{0};
};

template <class Slot>
double millisFor(Slot* slots, int threads, int increments) {
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t)
        ts.emplace_back([&, t] {
            for (int i = 0; i < increments; ++i) {
                if constexpr (std::is_same_v<Slot, CourierCounters>) slots[t].delivered.fetch_add(1, std::memory_order_relaxed);
                else slots[t].v.fetch_add(1, std::memory_order_relaxed);
            }
        });
    for (auto& t : ts) t.join();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

}

int main() {
    constexpr int kThreads = 8, kIncrements = 2'000'000;
    check(sizeof(CourierCounters) == kCacheLineBytes && alignof(CourierCounters) == kCacheLineBytes,
          "cada contador ocupa exactamente una linea de cache");

    LiveCounters counters(kThreads);
    check(reinterpret_cast<std::uintptr_t>(&counters.at(1)) - reinterpret_cast<std::uintptr_t>(&counters.at(0)) == kCacheLineBytes,
          "los contadores de motos vecinas estan en lineas distintas");
    const double padded = millisFor(&counters.at(0), kThreads, kIncrements);
    const CountersSnapshot s = counters.snapshot();
    bool exact = s.delivered == static_cast<std::int64_t>(kThreads) * kIncrements && s.deliveredPerCourier.size() == kThreads;
    for (std::int64_t d : s.deliveredPerCourier) exact = exact && d == kIncrements;
    check(exact && s.pickedUp == 0 && s.breakdowns == 0, "ocho hilos incrementando el suyo: totales exactos");

    std::vector<Packed> packed(kThreads);
    const double shared = millisFor(packed.data(), kThreads, kIncrements);
    std::cout << "live_counters_test: " << kThreads << " hilos x " << kIncrements << " incrementos: pegados " << shared
              << " ms, en lineas propias " << padded << " ms (x" << shared / padded << ")\n";
    check(padded > 0 && shared > 0, "las dos mediciones corrieron");

    if (failures == 0) {
        std::cout << "live_counters_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "live_counters_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
