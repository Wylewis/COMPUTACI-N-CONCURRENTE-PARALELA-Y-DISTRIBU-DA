// Pruebas del reloj simulado. No duermen: usan instantes reales construidos a
// partir del inicio, asi la prueba es exacta y rapida.
#include <chrono>
#include <iostream>
#include <stdexcept>

#include "sim_clock.h"

using namespace std::chrono_literals;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

}  // namespace

int main() {
    const auto t0 = SimClock::RealClock::now();

    // timeScale 60: un segundo real son 60 segundos simulados
    {
        SimClock clock(60.0);
        clock.start(t0);
        check(clock.simMsAt(t0) == 0, "en el inicio el tiempo simulado es 0");
        check(clock.simMsAt(t0 + 1s) == 60'000, "1 s real = 60 000 ms simulados");
        check(clock.simMsAt(t0 + 500ms) == 30'000, "0.5 s real = 30 000 ms simulados");
        check(clock.toReal(60'000) == std::chrono::duration_cast<SimClock::RealDuration>(1s),
              "60 000 ms simulados se esperan en 1 s real");
        check(clock.toReal(1'500) == std::chrono::duration_cast<SimClock::RealDuration>(25ms),
              "1 500 ms simulados son 25 ms reales");
        check(clock.toSimMs(2s) == 120'000, "2 s reales son 120 000 ms simulados");
        check(clock.realTimeFor(60'000) == t0 + 1s, "el instante real para t = 60 000 es inicio + 1 s");
        check(clock.simMsAt(clock.realTimeFor(123'456)) == 123'456, "ida y vuelta exacta");
    }

    // timeScale 1: tiempo real y simulado coinciden
    {
        SimClock clock(1.0);
        clock.start(t0);
        check(clock.simMsAt(t0 + 1500ms) == 1'500, "con escala 1, 1.5 s real = 1 500 ms simulados");
        check(clock.toReal(1'500) == std::chrono::duration_cast<SimClock::RealDuration>(1500ms),
              "con escala 1, esperar 1 500 ms simulados es 1.5 s real");
    }

    // timeScale menor que 1: la simulacion va mas lenta que el reloj real
    {
        SimClock clock(0.5);
        clock.start(t0);
        check(clock.simMsAt(t0 + 2s) == 1'000, "con escala 0.5, 2 s reales = 1 000 ms simulados");
        check(clock.toReal(1'000) == std::chrono::duration_cast<SimClock::RealDuration>(2s),
              "con escala 0.5, 1 000 ms simulados son 2 s reales");
    }

    // El tiempo simulado nunca es negativo, aunque se consulte antes del inicio
    {
        SimClock clock(60.0);
        clock.start(t0 + 10s);
        check(clock.simMsAt(t0) == 0, "antes del inicio el tiempo simulado es 0, no negativo");
    }

    // Monotonia: instantes reales crecientes dan tiempos simulados no decrecientes
    {
        SimClock clock(37.5);
        clock.start(t0);
        std::int64_t prev = -1;
        bool monotone = true;
        for (int i = 0; i < 1000; ++i) {
            const auto now = clock.simMsAt(t0 + std::chrono::milliseconds(i * 7));
            if (now < prev) monotone = false;
            prev = now;
        }
        check(monotone, "el tiempo simulado nunca retrocede");
    }

    // Escala invalida
    {
        bool threw = false;
        try {
            SimClock bad(0.0);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "timeScale 0 se rechaza");
        threw = false;
        try {
            SimClock bad(-5.0);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "timeScale negativo se rechaza");
    }

    // nowMs() avanza con el reloj real
    {
        SimClock clock(1000.0);
        clock.start();
        const auto a = clock.nowMs();
        const auto b = clock.nowMs();
        check(b >= a, "nowMs no retrocede entre dos llamadas");
    }

    if (failures == 0) {
        std::cout << "sim_clock_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "sim_clock_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
